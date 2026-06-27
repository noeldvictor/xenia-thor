/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/llvm/llvm_assembler.h"

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_function.h"
#include "xenia/cpu/backend/llvm/llvm_backend.h"
#include "xenia/cpu/backend/llvm/llvm_jit_context.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/hir/instr.h"
#include "xenia/cpu/hir/label.h"

#ifndef XE_LLVM_BACKEND_ENABLED
#define XE_LLVM_BACKEND_ENABLED 0
#endif

#if XE_LLVM_BACKEND_ENABLED
#include <string>
#include <unordered_map>

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#endif  // XE_LLVM_BACKEND_ENABLED

DECLARE_int32(cpu_backend_llvm_opt);

namespace xe {
namespace cpu {
namespace backend {
namespace llvm_backend {

using namespace xe::cpu::hir;

LLVMAssembler::LLVMAssembler(LLVMBackend* backend)
    : Assembler(backend), llvm_backend_(backend) {}

LLVMAssembler::~LLVMAssembler() = default;

bool LLVMAssembler::Initialize() {
  if (!Assembler::Initialize()) {
    return false;
  }
  // The fallback is a plain a64 assembler bound to the same backend (which IS-A
  // A64Backend). Qualified call to skip LLVMBackend's CreateAssembler override.
  fallback_ = llvm_backend_->a64::A64Backend::CreateAssembler();
  if (!fallback_ || !fallback_->Initialize()) {
    XELOGE("LLVMAssembler: failed to create a64 fallback assembler");
    return false;
  }
  return true;
}

void LLVMAssembler::Reset() { Assembler::Reset(); }

#if XE_LLVM_BACKEND_ENABLED

namespace {

// Per-function HIR -> LLVM IR lowering. Direct-context model (milestone 1):
// LOAD/STORE_CONTEXT lower to loads/stores on the real PPCContext pointer; the
// LLVM optimizer (GVN/SROA) gives cross-block residency. Returns false on any
// opcode it does not handle, so the caller falls back to a64.
class Lowerer {
 public:
  Lowerer(llvm::LLVMContext& ctx, llvm::Module* mod, llvm::Function* fn)
      : ctx_(ctx), mod_(mod), fn_(fn), b_(ctx) {}

  bool Run(HIRBuilder* builder);

 private:
  llvm::Type* T(TypeName t) {
    switch (t) {
      case INT8_TYPE:
        return llvm::Type::getInt8Ty(ctx_);
      case INT16_TYPE:
        return llvm::Type::getInt16Ty(ctx_);
      case INT32_TYPE:
        return llvm::Type::getInt32Ty(ctx_);
      case INT64_TYPE:
        return llvm::Type::getInt64Ty(ctx_);
      case FLOAT32_TYPE:
        return llvm::Type::getFloatTy(ctx_);
      case FLOAT64_TYPE:
        return llvm::Type::getDoubleTy(ctx_);
      case VEC128_TYPE:
        // 128-bit vector. <4 x i32> is the canonical carrier; lane-agnostic ops
        // (vand/vor/vxor/vnot, copies) are correct on it and lower to NEON.
        // Lane-typed arith, vsel, and vector mem+byteswap still fall back (the
        // type-mismatched cases are guarded in their handlers below).
        return llvm::VectorType::get(llvm::Type::getInt32Ty(ctx_), 4, false);
      default:
        return nullptr;
    }
  }

  bool IsVec(llvm::Value* v) { return v && v->getType()->isVectorTy(); }

  // Lane vector type for a VMX part_type; the <4 x i32> VEC128 carrier is
  // bitcast to/from this for lane-typed ops (element-wise ops match a64
  // bit-for-bit regardless of lane order, since both work the same 128 bits).
  llvm::Type* LaneVecTy(TypeName pt) {
    switch (pt) {
      case INT8_TYPE:
        return llvm::VectorType::get(llvm::Type::getInt8Ty(ctx_), 16, false);
      case INT16_TYPE:
        return llvm::VectorType::get(llvm::Type::getInt16Ty(ctx_), 8, false);
      case INT32_TYPE:
        return llvm::VectorType::get(llvm::Type::getInt32Ty(ctx_), 4, false);
      case FLOAT32_TYPE:
        return llvm::VectorType::get(llvm::Type::getFloatTy(ctx_), 4, false);
      default:
        return nullptr;
    }
  }

  // Emit a call to the runtime guest-call helper (resolves the target guest
  // function + invokes it). x20/x21 are AAPCS callee-saved across this C call.
  void EmitGuestCall(llvm::Value* target_i32) {
    auto* fty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                        {llvm::Type::getInt32Ty(ctx_)}, false);
    auto callee = mod_->getOrInsertFunction("xe_llvm_guest_call", fty);
    b_.CreateCall(callee, {target_i32});
  }

  bool IsInt(TypeName t) { return t <= INT64_TYPE; }
  bool IsFloat(TypeName t) { return t == FLOAT32_TYPE || t == FLOAT64_TYPE; }

  // Resolve an HIR value to an LLVM value (constant materialized inline, else
  // looked up from the def map).
  llvm::Value* V(Value* v) {
    if (!v) return nullptr;
    if (v->IsConstant()) {
      switch (v->type) {
        case INT8_TYPE:
          return b_.getInt8(uint8_t(v->constant.i8));
        case INT16_TYPE:
          return b_.getInt16(uint16_t(v->constant.i16));
        case INT32_TYPE:
          return b_.getInt32(uint32_t(v->constant.i32));
        case INT64_TYPE:
          return b_.getInt64(uint64_t(v->constant.i64));
        case FLOAT32_TYPE:
          return llvm::ConstantFP::get(b_.getFloatTy(), v->constant.f32);
        case FLOAT64_TYPE:
          return llvm::ConstantFP::get(b_.getDoubleTy(), v->constant.f64);
        default:
          return nullptr;
      }
    }
    auto it = values_.find(v->ordinal);
    return it == values_.end() ? nullptr : it->second;
  }

  void Def(Value* dest, llvm::Value* lv) { values_[dest->ordinal] = lv; }

  // Host pointer into the guest context (x20 + offset).
  llvm::Value* CtxPtr(uint64_t offset) {
    return b_.CreateGEP(b_.getInt8Ty(), ctx_ptr_, b_.getInt64(offset));
  }
  // Host pointer into guest memory (x21 + ea, ea zero-extended to 64-bit).
  // IMPORTANT: every guest LOAD/STORE through this pointer is emitted VOLATILE
  // (see OPCODE_LOAD/STORE below). Guest memory accesses can fault into xenia's
  // access-violation handler (MMIO ranges + GPU write-watches). For MMIO the
  // handler hand-decodes the faulting ARM64 instruction and only recognizes a
  // SINGLE 32-bit LDR/STR (the exact form the a64 JIT emits, mmio_handler.cc
  // TryDecodeLoadStore). LLVM's optimizer, left unconstrained, merges / pairs
  // (LDP/STP) / widens (64-bit) / vectorizes / reorders these accesses, so the
  // handler can't decode them -> the fault is never resolved -> the instruction
  // re-faults forever (the device "signal storm" = 0 fps). Volatile forbids all
  // of that, keeping each guest access a single decodable LDR/STR with its byte-
  // swap REV adjacent. This does NOT cost the residency win: context/local slots
  // (LOAD/STORE_CONTEXT, LOAD/STORE_LOCAL) stay non-volatile and are still
  // promoted to SSA registers; only true guest-memory traffic is pinned.
  llvm::Value* MemPtr(llvm::Value* ea) {
    auto* ea64 = b_.CreateZExtOrTrunc(ea, b_.getInt64Ty());
    return b_.CreateGEP(b_.getInt8Ty(), membase_, ea64);
  }
  llvm::Value* MaybeByteSwap(llvm::Value* v, llvm::Type* ty, uint16_t flags) {
    if (!(flags & LOAD_STORE_BYTE_SWAP)) return v;
    if (ty->isFloatingPointTy()) {
      auto* it = ty->isFloatTy() ? b_.getInt32Ty() : b_.getInt64Ty();
      auto* iv = b_.CreateBitCast(v, it);
      iv = b_.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, iv);
      return b_.CreateBitCast(iv, ty);
    }
    if (ty->getIntegerBitWidth() <= 8) return v;
    return b_.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, v);
  }
  // i1 truth test of an HIR boolean/scalar value.
  llvm::Value* Truth(llvm::Value* v) {
    if (v->getType()->isFloatingPointTy()) {
      return b_.CreateFCmpUNE(v, llvm::ConstantFP::get(v->getType(), 0.0));
    }
    return b_.CreateICmpNE(v, llvm::ConstantInt::get(v->getType(), 0));
  }

  llvm::BasicBlock* BlockFor(Block* hb) { return block_map_[hb->ordinal]; }

  bool LowerInstr(Instr* i);

  llvm::LLVMContext& ctx_;
  llvm::Module* mod_;
  llvm::Function* fn_;
  llvm::IRBuilder<> b_;
  llvm::Value* ctx_ptr_ = nullptr;
  llvm::Value* membase_ = nullptr;
  std::unordered_map<uint32_t, llvm::Value*> values_;
  std::unordered_map<uint32_t, llvm::AllocaInst*> locals_;
  std::unordered_map<uint16_t, llvm::BasicBlock*> block_map_;
};

// Reads a reserved AArch64 register (x20=ctx, x21=membase) set up by the
// host->guest thunk. Matches the a64 ABI so the same thunk + a64<->LLVM calls
// all interoperate.
static llvm::Value* ReadReg(llvm::IRBuilder<>& b, llvm::Module* mod,
                            const char* name) {
  auto& ctx = b.getContext();
  auto* i64 = llvm::Type::getInt64Ty(ctx);
  auto* decl = llvm::Intrinsic::getOrInsertDeclaration(
      mod, llvm::Intrinsic::read_register, {i64});
  auto* md = llvm::MDNode::get(ctx, {llvm::MDString::get(ctx, name)});
  return b.CreateCall(decl, {llvm::MetadataAsValue::get(ctx, md)});
}

bool Lowerer::Run(HIRBuilder* builder) {
  auto* entry = llvm::BasicBlock::Create(ctx_, "entry", fn_);
  b_.SetInsertPoint(entry);
  ctx_ptr_ = b_.CreateIntToPtr(ReadReg(b_, mod_, "x20"), b_.getPtrTy(), "ctx");
  membase_ =
      b_.CreateIntToPtr(ReadReg(b_, mod_, "x21"), b_.getPtrTy(), "membase");

  // One llvm BB per HIR block, created up front so branches can target them.
  for (Block* blk = builder->first_block(); blk; blk = blk->next) {
    block_map_[blk->ordinal] =
        llvm::BasicBlock::Create(ctx_, "b", fn_);
  }
  Block* first = builder->first_block();
  if (!first) {
    b_.CreateRetVoid();
    return true;
  }
  b_.CreateBr(block_map_[first->ordinal]);

  for (Block* blk = first; blk; blk = blk->next) {
    b_.SetInsertPoint(block_map_[blk->ordinal]);
    for (Instr* i = blk->instr_head; i; i = i->next) {
      if (!LowerInstr(i)) {
        return false;  // unsupported -> fallback
      }
    }
    // Fall through to the next sequential block if no terminator was emitted.
    if (!b_.GetInsertBlock()->getTerminator()) {
      if (blk->next) {
        b_.CreateBr(block_map_[blk->next->ordinal]);
      } else {
        b_.CreateRetVoid();
      }
    }
  }
  return true;
}

bool Lowerer::LowerInstr(Instr* i) {
  const Opcode op = i->opcode->num;
  switch (op) {
    // ---- ignorable / meta ----
    case OPCODE_COMMENT:
    case OPCODE_NOP:
    case OPCODE_SOURCE_OFFSET:
    case OPCODE_CONTEXT_BARRIER:  // direct-ctx model: every access hits memory
      return true;
    case OPCODE_MEMORY_BARRIER:
      b_.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent);
      return true;

    // ---- context ----
    case OPCODE_LOAD_CONTEXT: {
      auto* ty = T(i->dest->type);
      if (!ty) return false;
      Def(i->dest, b_.CreateLoad(ty, CtxPtr(i->src1.offset)));
      return true;
    }
    case OPCODE_STORE_CONTEXT: {
      auto* val = V(i->src2.value);
      if (!val) return false;
      b_.CreateStore(val, CtxPtr(i->src1.offset));
      return true;
    }

    // ---- locals (function-private stack slots) ----
    case OPCODE_LOAD_LOCAL: {
      auto* ty = T(i->dest->type);
      if (!ty) return false;
      auto key = i->src1.value->ordinal;
      auto it = locals_.find(key);
      if (it == locals_.end()) return false;  // store-before-load expected
      Def(i->dest, b_.CreateLoad(ty, it->second));
      return true;
    }
    case OPCODE_STORE_LOCAL: {
      auto* val = V(i->src2.value);
      if (!val) return false;
      auto key = i->src1.value->ordinal;
      auto it = locals_.find(key);
      llvm::AllocaInst* slot;
      if (it == locals_.end()) {
        llvm::IRBuilder<> eb(&fn_->getEntryBlock(),
                             fn_->getEntryBlock().begin());
        slot = eb.CreateAlloca(val->getType());
        locals_[key] = slot;
      } else {
        slot = it->second;
      }
      b_.CreateStore(val, slot);
      return true;
    }

    // ---- moves / conversions ----
    case OPCODE_ASSIGN:
      Def(i->dest, V(i->src1.value));
      return true;
    case OPCODE_CAST: {
      // Reinterpret bits between same-size int/float.
      auto* ty = T(i->dest->type);
      auto* src = V(i->src1.value);
      if (!ty || !src) return false;
      Def(i->dest, b_.CreateBitCast(src, ty));
      return true;
    }
    case OPCODE_ZERO_EXTEND: {
      auto* ty = T(i->dest->type);
      auto* src = V(i->src1.value);
      if (!ty || !src) return false;
      Def(i->dest, b_.CreateZExt(src, ty));
      return true;
    }
    case OPCODE_SIGN_EXTEND: {
      auto* ty = T(i->dest->type);
      auto* src = V(i->src1.value);
      if (!ty || !src) return false;
      Def(i->dest, b_.CreateSExt(src, ty));
      return true;
    }
    case OPCODE_TRUNCATE: {
      auto* ty = T(i->dest->type);
      auto* src = V(i->src1.value);
      if (!ty || !src) return false;
      Def(i->dest, b_.CreateTrunc(src, ty));
      return true;
    }
    case OPCODE_CONVERT: {
      // int<->float numeric conversion (not bit reinterpret).
      auto dt = i->dest->type;
      auto st = i->src1.value->type;
      auto* ty = T(dt);
      auto* src = V(i->src1.value);
      if (!ty || !src) return false;
      if (IsFloat(dt) && IsInt(st)) {
        Def(i->dest, b_.CreateSIToFP(src, ty));
      } else if (IsInt(dt) && IsFloat(st)) {
        Def(i->dest, b_.CreateFPToSI(src, ty));
      } else if (IsFloat(dt) && IsFloat(st)) {
        Def(i->dest, dt > st ? b_.CreateFPExt(src, ty)
                             : b_.CreateFPTrunc(src, ty));
      } else {
        return false;
      }
      return true;
    }

    // ---- integer / float arithmetic ----
    case OPCODE_ADD: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, IsFloat(i->dest->type) ? b_.CreateFAdd(a, c)
                                          : b_.CreateAdd(a, c));
      return true;
    }
    case OPCODE_SUB: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, IsFloat(i->dest->type) ? b_.CreateFSub(a, c)
                                          : b_.CreateSub(a, c));
      return true;
    }
    case OPCODE_MUL: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, IsFloat(i->dest->type) ? b_.CreateFMul(a, c)
                                          : b_.CreateMul(a, c));
      return true;
    }
    case OPCODE_DIV: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      if (IsFloat(i->dest->type)) {
        Def(i->dest, b_.CreateFDiv(a, c));
      } else {
        Def(i->dest, (i->flags & ARITHMETIC_UNSIGNED) ? b_.CreateUDiv(a, c)
                                                       : b_.CreateSDiv(a, c));
      }
      return true;
    }
    case OPCODE_MUL_HI: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c || !IsInt(i->dest->type)) return false;
      unsigned w = a->getType()->getIntegerBitWidth();
      auto* wide = llvm::Type::getIntNTy(ctx_, w * 2);
      bool uns = (i->flags & ARITHMETIC_UNSIGNED) != 0;
      auto* aw = uns ? b_.CreateZExt(a, wide) : b_.CreateSExt(a, wide);
      auto* cw = uns ? b_.CreateZExt(c, wide) : b_.CreateSExt(c, wide);
      auto* prod = b_.CreateMul(aw, cw);
      auto* hi = b_.CreateLShr(prod, w);
      Def(i->dest, b_.CreateTrunc(hi, a->getType()));
      return true;
    }
    case OPCODE_NEG: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      Def(i->dest, IsFloat(i->dest->type) ? b_.CreateFNeg(a)
                                          : b_.CreateNeg(a));
      return true;
    }
    case OPCODE_ABS: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      if (IsFloat(i->dest->type)) {
        Def(i->dest, b_.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, a));
      } else {
        Def(i->dest, b_.CreateBinaryIntrinsic(llvm::Intrinsic::abs, a,
                                              b_.getInt1(false)));
      }
      return true;
    }
    case OPCODE_SQRT: {
      auto* a = V(i->src1.value);
      if (!a || !IsFloat(i->dest->type)) return false;
      Def(i->dest, b_.CreateUnaryIntrinsic(llvm::Intrinsic::sqrt, a));
      return true;
    }
    case OPCODE_MAX: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      if (IsFloat(i->dest->type)) {
        Def(i->dest, b_.CreateBinaryIntrinsic(llvm::Intrinsic::maxnum, a, c));
      } else {
        Def(i->dest, b_.CreateBinaryIntrinsic(llvm::Intrinsic::smax, a, c));
      }
      return true;
    }
    case OPCODE_MIN: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      if (IsFloat(i->dest->type)) {
        Def(i->dest, b_.CreateBinaryIntrinsic(llvm::Intrinsic::minnum, a, c));
      } else {
        Def(i->dest, b_.CreateBinaryIntrinsic(llvm::Intrinsic::smin, a, c));
      }
      return true;
    }

    // ---- bitwise / shifts ----
    case OPCODE_AND: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateAnd(a, c));
      return true;
    }
    case OPCODE_AND_NOT: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateAnd(a, b_.CreateNot(c)));
      return true;
    }
    case OPCODE_OR: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateOr(a, c));
      return true;
    }
    case OPCODE_XOR: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateXor(a, c));
      return true;
    }
    case OPCODE_NOT: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      Def(i->dest, b_.CreateNot(a));
      return true;
    }
    case OPCODE_SHL: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateShl(a, b_.CreateZExtOrTrunc(c, a->getType())));
      return true;
    }
    case OPCODE_SHR: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateLShr(a, b_.CreateZExtOrTrunc(c, a->getType())));
      return true;
    }
    case OPCODE_SHA: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateAShr(a, b_.CreateZExtOrTrunc(c, a->getType())));
      return true;
    }
    case OPCODE_ROTATE_LEFT: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      auto* amt = b_.CreateZExtOrTrunc(c, a->getType());
      Def(i->dest, b_.CreateIntrinsic(llvm::Intrinsic::fshl, {a->getType()},
                                      {a, a, amt}));
      return true;
    }
    case OPCODE_CNTLZ: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      auto* r = b_.CreateBinaryIntrinsic(llvm::Intrinsic::ctlz, a,
                                         b_.getInt1(false));
      Def(i->dest, b_.CreateZExtOrTrunc(r, T(i->dest->type)));
      return true;
    }
    case OPCODE_BYTE_SWAP: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      if (a->getType()->getIntegerBitWidth() <= 8) {
        Def(i->dest, a);
      } else {
        Def(i->dest, b_.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, a));
      }
      return true;
    }

    // ---- compares / selects ----
    case OPCODE_COMPARE_EQ:
    case OPCODE_COMPARE_NE:
    case OPCODE_COMPARE_SLT:
    case OPCODE_COMPARE_SLE:
    case OPCODE_COMPARE_SGT:
    case OPCODE_COMPARE_SGE:
    case OPCODE_COMPARE_ULT:
    case OPCODE_COMPARE_ULE:
    case OPCODE_COMPARE_UGT:
    case OPCODE_COMPARE_UGE: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      llvm::Value* r;
      bool fp = a->getType()->isFloatingPointTy();
      switch (op) {
        case OPCODE_COMPARE_EQ:
          r = fp ? b_.CreateFCmpOEQ(a, c) : b_.CreateICmpEQ(a, c);
          break;
        case OPCODE_COMPARE_NE:
          r = fp ? b_.CreateFCmpUNE(a, c) : b_.CreateICmpNE(a, c);
          break;
        case OPCODE_COMPARE_SLT:
          r = fp ? b_.CreateFCmpOLT(a, c) : b_.CreateICmpSLT(a, c);
          break;
        case OPCODE_COMPARE_SLE:
          r = fp ? b_.CreateFCmpOLE(a, c) : b_.CreateICmpSLE(a, c);
          break;
        case OPCODE_COMPARE_SGT:
          r = fp ? b_.CreateFCmpOGT(a, c) : b_.CreateICmpSGT(a, c);
          break;
        case OPCODE_COMPARE_SGE:
          r = fp ? b_.CreateFCmpOGE(a, c) : b_.CreateICmpSGE(a, c);
          break;
        case OPCODE_COMPARE_ULT:
          r = fp ? b_.CreateFCmpOLT(a, c) : b_.CreateICmpULT(a, c);
          break;
        case OPCODE_COMPARE_ULE:
          r = fp ? b_.CreateFCmpOLE(a, c) : b_.CreateICmpULE(a, c);
          break;
        case OPCODE_COMPARE_UGT:
          r = fp ? b_.CreateFCmpOGT(a, c) : b_.CreateICmpUGT(a, c);
          break;
        default:
          r = fp ? b_.CreateFCmpOGE(a, c) : b_.CreateICmpUGE(a, c);
          break;
      }
      Def(i->dest, b_.CreateZExt(r, T(i->dest->type)));
      return true;
    }
    case OPCODE_IS_TRUE: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      Def(i->dest, b_.CreateZExt(Truth(a), T(i->dest->type)));
      return true;
    }
    case OPCODE_IS_FALSE: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      Def(i->dest, b_.CreateZExt(b_.CreateNot(Truth(a)), T(i->dest->type)));
      return true;
    }
    case OPCODE_SELECT: {
      auto* cond = V(i->src1.value);
      auto* tv = V(i->src2.value);
      auto* fv = V(i->src3.value);
      if (!cond || !tv || !fv) return false;
      if (IsVec(tv) || IsVec(cond)) return false;  // vsel is per-bit -> a64 (P3)
      Def(i->dest, b_.CreateSelect(Truth(cond), tv, fv));
      return true;
    }

    // ---- guest memory ----
    case OPCODE_LOAD: {
      auto* ty = T(i->dest->type);
      auto* ea = V(i->src1.value);
      if (!ty || !ea) return false;
      if (ty->isVectorTy()) return false;  // vector mem+byteswap -> a64 (P3)
      auto* v = b_.CreateLoad(ty, MemPtr(ea), /*isVolatile=*/true);
      Def(i->dest, MaybeByteSwap(v, ty, i->flags));
      return true;
    }
    case OPCODE_STORE: {
      auto* ea = V(i->src1.value);
      auto* val = V(i->src2.value);
      if (!ea || !val) return false;
      if (IsVec(val)) return false;  // vector mem+byteswap -> a64 (P3)
      val = MaybeByteSwap(val, val->getType(), i->flags);
      b_.CreateStore(val, MemPtr(ea), /*isVolatile=*/true);
      return true;
    }
    case OPCODE_LOAD_OFFSET: {
      auto* ty = T(i->dest->type);
      auto* base = V(i->src1.value);
      auto* off = V(i->src2.value);
      if (!ty || !base || !off) return false;
      if (ty->isVectorTy()) return false;  // vector mem+byteswap -> a64 (P3)
      auto* ea = b_.CreateAdd(b_.CreateZExtOrTrunc(base, b_.getInt64Ty()),
                              b_.CreateZExtOrTrunc(off, b_.getInt64Ty()));
      auto* v = b_.CreateLoad(ty, MemPtr(ea), /*isVolatile=*/true);
      Def(i->dest, MaybeByteSwap(v, ty, i->flags));
      return true;
    }
    case OPCODE_STORE_OFFSET: {
      auto* base = V(i->src1.value);
      auto* off = V(i->src2.value);
      auto* val = V(i->src3.value);
      if (!base || !off || !val) return false;
      if (IsVec(val)) return false;  // vector mem+byteswap -> a64 (P3)
      auto* ea = b_.CreateAdd(b_.CreateZExtOrTrunc(base, b_.getInt64Ty()),
                              b_.CreateZExtOrTrunc(off, b_.getInt64Ty()));
      val = MaybeByteSwap(val, val->getType(), i->flags);
      b_.CreateStore(val, MemPtr(ea), /*isVolatile=*/true);
      return true;
    }

    // ---- control flow ----
    case OPCODE_BRANCH: {
      b_.CreateBr(BlockFor(i->src1.label->block));
      return true;
    }
    case OPCODE_BRANCH_TRUE:
    case OPCODE_BRANCH_FALSE: {
      auto* cond = V(i->src1.value);
      if (!cond) return false;
      auto* c1 = Truth(cond);
      auto* target = BlockFor(i->src2.label->block);
      llvm::BasicBlock* other;
      if (i->next) {
        other = llvm::BasicBlock::Create(ctx_, "c", fn_);
      } else {
        other = i->block->next ? BlockFor(i->block->next) : nullptr;
        if (!other) other = llvm::BasicBlock::Create(ctx_, "c", fn_);
      }
      if (op == OPCODE_BRANCH_TRUE) {
        b_.CreateCondBr(c1, target, other);
      } else {
        b_.CreateCondBr(c1, other, target);
      }
      if (i->next || !i->block->next) {
        b_.SetInsertPoint(other);
        if (!i->next) b_.CreateRetVoid();  // dangling guard block
      }
      return true;
    }
    case OPCODE_RETURN:
      b_.CreateRetVoid();
      return true;
    case OPCODE_RETURN_TRUE: {
      auto* cond = V(i->src1.value);
      if (!cond) return false;
      auto* ret_bb = llvm::BasicBlock::Create(ctx_, "ret", fn_);
      llvm::BasicBlock* cont;
      if (i->next) {
        cont = llvm::BasicBlock::Create(ctx_, "c", fn_);
      } else {
        cont = i->block->next ? BlockFor(i->block->next)
                              : llvm::BasicBlock::Create(ctx_, "c", fn_);
      }
      b_.CreateCondBr(Truth(cond), ret_bb, cont);
      b_.SetInsertPoint(ret_bb);
      b_.CreateRetVoid();
      b_.SetInsertPoint(cont);
      if (!i->next && !i->block->next) b_.CreateRetVoid();
      return true;
    }

    // ---- guest calls (P4) ----
    case OPCODE_CALL:
    case OPCODE_CALL_EXTERN: {
      EmitGuestCall(b_.getInt32(i->src1.symbol->address()));
      return true;
    }
    case OPCODE_CALL_INDIRECT: {
      auto* t = V(i->src1.value);
      if (!t) return false;
      EmitGuestCall(b_.CreateZExtOrTrunc(t, b_.getInt32Ty()));
      return true;
    }
    case OPCODE_SET_RETURN_ADDRESS:
      // Return flows via the host stack (helper + thunk) in this model; the
      // guest return-address slot isn't needed for correctness.
      return true;
    case OPCODE_CALL_TRUE:
    case OPCODE_CALL_INDIRECT_TRUE: {
      auto* cond = V(i->src1.value);
      if (!cond) return false;
      llvm::Value* target;
      if (op == OPCODE_CALL_TRUE) {
        target = b_.getInt32(i->src2.symbol->address());
      } else {
        auto* t = V(i->src2.value);
        if (!t) return false;
        target = b_.CreateZExtOrTrunc(t, b_.getInt32Ty());
      }
      auto* call_bb = llvm::BasicBlock::Create(ctx_, "call", fn_);
      llvm::BasicBlock* cont_bb;
      if (i->next) {
        cont_bb = llvm::BasicBlock::Create(ctx_, "c", fn_);
      } else {
        cont_bb = i->block->next ? BlockFor(i->block->next)
                                 : llvm::BasicBlock::Create(ctx_, "c", fn_);
      }
      b_.CreateCondBr(Truth(cond), call_bb, cont_bb);
      b_.SetInsertPoint(call_bb);
      EmitGuestCall(target);
      b_.CreateBr(cont_bb);
      b_.SetInsertPoint(cont_bb);
      if (!i->next && !i->block->next) b_.CreateRetVoid();
      return true;
    }

    // ---- lane-typed vector arithmetic ----
    case OPCODE_VECTOR_ADD:
    case OPCODE_VECTOR_SUB: {
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c) return false;
      TypeName pt = static_cast<TypeName>(i->flags & 0xFF);
      auto* lt = LaneVecTy(pt);
      if (!lt) return false;
      bool add = (op == OPCODE_VECTOR_ADD);
      auto* av = b_.CreateBitCast(a, lt);
      auto* cv = b_.CreateBitCast(c, lt);
      llvm::Value* r;
      if (pt == FLOAT32_TYPE) {
        r = add ? b_.CreateFAdd(av, cv) : b_.CreateFSub(av, cv);
      } else {
        uint32_t arith = i->flags >> 8;
        if (arith & ARITHMETIC_SATURATE) {
          bool uns = (arith & ARITHMETIC_UNSIGNED) != 0;
          auto id = add ? (uns ? llvm::Intrinsic::uadd_sat
                               : llvm::Intrinsic::sadd_sat)
                        : (uns ? llvm::Intrinsic::usub_sat
                               : llvm::Intrinsic::ssub_sat);
          r = b_.CreateBinaryIntrinsic(id, av, cv);
        } else {
          r = add ? b_.CreateAdd(av, cv) : b_.CreateSub(av, cv);
        }
      }
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_VECTOR_MAX:
    case OPCODE_VECTOR_MIN: {
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c) return false;
      TypeName pt = static_cast<TypeName>(i->flags >> 8);  // part_type in high
      auto* lt = LaneVecTy(pt);
      if (!lt) return false;
      bool mx = (op == OPCODE_VECTOR_MAX);
      auto* av = b_.CreateBitCast(a, lt);
      auto* cv = b_.CreateBitCast(c, lt);
      llvm::Value* r;
      if (pt == FLOAT32_TYPE) {
        r = b_.CreateBinaryIntrinsic(
            mx ? llvm::Intrinsic::maxnum : llvm::Intrinsic::minnum, av, cv);
      } else {
        bool uns = (i->flags & ARITHMETIC_UNSIGNED) != 0;
        auto id = mx ? (uns ? llvm::Intrinsic::umax : llvm::Intrinsic::smax)
                     : (uns ? llvm::Intrinsic::umin : llvm::Intrinsic::smin);
        r = b_.CreateBinaryIntrinsic(id, av, cv);
      }
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_VECTOR_COMPARE_EQ:
    case OPCODE_VECTOR_COMPARE_SGT:
    case OPCODE_VECTOR_COMPARE_SGE:
    case OPCODE_VECTOR_COMPARE_UGT:
    case OPCODE_VECTOR_COMPARE_UGE: {
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c) return false;
      TypeName pt = static_cast<TypeName>(i->flags);  // whole flags = part_type
      auto* lt = LaneVecTy(pt);
      if (!lt) return false;
      bool fp = (pt == FLOAT32_TYPE);
      auto* av = b_.CreateBitCast(a, lt);
      auto* cv = b_.CreateBitCast(c, lt);
      llvm::Value* m;  // <N x i1>
      switch (op) {
        case OPCODE_VECTOR_COMPARE_EQ:
          m = fp ? b_.CreateFCmpOEQ(av, cv) : b_.CreateICmpEQ(av, cv);
          break;
        case OPCODE_VECTOR_COMPARE_SGT:
          m = fp ? b_.CreateFCmpOGT(av, cv) : b_.CreateICmpSGT(av, cv);
          break;
        case OPCODE_VECTOR_COMPARE_SGE:
          m = fp ? b_.CreateFCmpOGE(av, cv) : b_.CreateICmpSGE(av, cv);
          break;
        case OPCODE_VECTOR_COMPARE_UGT:
          m = fp ? b_.CreateFCmpOGT(av, cv) : b_.CreateICmpUGT(av, cv);
          break;
        default:  // UGE
          m = fp ? b_.CreateFCmpOGE(av, cv) : b_.CreateICmpUGE(av, cv);
          break;
      }
      // All-1s/0s per lane: sext the i1 mask to the integer lane width.
      auto* int_lt = fp ? llvm::VectorType::get(llvm::Type::getInt32Ty(ctx_), 4,
                                                false)
                        : lt;
      Def(i->dest, b_.CreateBitCast(b_.CreateSExt(m, int_lt), T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_VECTOR_SHL:
    case OPCODE_VECTOR_SHR:
    case OPCODE_VECTOR_SHA: {
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c) return false;
      TypeName pt = static_cast<TypeName>(i->flags);  // whole flags = part_type
      auto* lt = LaneVecTy(pt);
      if (!lt || pt == FLOAT32_TYPE) return false;  // integer lanes only
      auto* av = b_.CreateBitCast(a, lt);
      auto* cv = b_.CreateBitCast(c, lt);
      // VMX masks the per-lane shift amount to (lane_width-1) (matches a64).
      unsigned w = lt->getScalarSizeInBits();
      auto* amt = b_.CreateAnd(cv, llvm::ConstantInt::get(lt, w - 1));
      llvm::Value* r = (op == OPCODE_VECTOR_SHL)   ? b_.CreateShl(av, amt)
                       : (op == OPCODE_VECTOR_SHR) ? b_.CreateLShr(av, amt)
                                                   : b_.CreateAShr(av, amt);
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_VECTOR_ROTATE_LEFT: {
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c) return false;
      TypeName pt = static_cast<TypeName>(i->flags);  // whole flags = part_type
      auto* lt = LaneVecTy(pt);
      if (!lt || pt == FLOAT32_TYPE) return false;
      auto* av = b_.CreateBitCast(a, lt);
      auto* cv = b_.CreateBitCast(c, lt);
      // fshl(x, x, amt) = rotate left by (amt mod width) == a64's amt & (width-1).
      auto* r = b_.CreateIntrinsic(llvm::Intrinsic::fshl, {lt}, {av, av, cv});
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_VECTOR_CONVERT_I2F: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      auto* iv = b_.CreateBitCast(a, LaneVecTy(INT32_TYPE));
      auto* fv = (i->flags & ARITHMETIC_UNSIGNED)
                     ? b_.CreateUIToFP(iv, LaneVecTy(FLOAT32_TYPE))
                     : b_.CreateSIToFP(iv, LaneVecTy(FLOAT32_TYPE));
      Def(i->dest, b_.CreateBitCast(fv, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_VECTOR_CONVERT_F2I: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      auto* fv = b_.CreateBitCast(a, LaneVecTy(FLOAT32_TYPE));
      // fptosi/fptoui.sat: NaN->0, saturate on overflow (matches a64 fcvtzs/zu).
      auto id = (i->flags & ARITHMETIC_UNSIGNED) ? llvm::Intrinsic::fptoui_sat
                                                 : llvm::Intrinsic::fptosi_sat;
      auto* iv = b_.CreateIntrinsic(LaneVecTy(INT32_TYPE), id, {fv});
      Def(i->dest, b_.CreateBitCast(iv, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_SPLAT: {
      // Broadcast a scalar to all lanes (a64 dup). All lanes equal => lane order
      // is irrelevant; element count follows the scalar's type.
      auto* s = V(i->src1.value);
      if (!s) return false;
      unsigned lanes;
      switch (i->src1.value->type) {
        case INT8_TYPE: lanes = 16; break;
        case INT16_TYPE: lanes = 8; break;
        case INT32_TYPE:
        case FLOAT32_TYPE: lanes = 4; break;
        default: return false;
      }
      Def(i->dest,
          b_.CreateBitCast(b_.CreateVectorSplat(lanes, s), T(VEC128_TYPE)));
      return true;
    }

    default:
      // Unsupported (calls, other vectors, atomics, packs, ...) -> a64 fallback.
      return false;
  }
}

}  // namespace

bool LLVMAssembler::LowerAndJit(GuestFunction* function, HIRBuilder* builder) {
  auto* jit_holder = llvm_backend_->jit();
  if (!jit_holder || !jit_holder->jit) return false;
  auto& jit = *jit_holder->jit;

  auto ctx_owner = std::make_unique<llvm::LLVMContext>();
  auto& ctx = *ctx_owner;
  auto mod = std::make_unique<llvm::Module>("guest", ctx);
  mod->setDataLayout(jit.getDataLayout());
  mod->setTargetTriple(jit.getTargetTriple().str());

  std::string name = "guest_" + std::to_string(function->address());
  auto* fn_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false);
  auto* fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, name,
                                    mod.get());
  // Reserve x20 (guest ctx) / x21 (membase) for this function's codegen so the
  // allocator never clobbers them (set by the host->guest thunk, read via
  // @llvm.read_register). Per-function attribute, not the JTMB (which hangs
  // create() under qemu).
  fn->addFnAttr("target-features", "+reserve-x20,+reserve-x21");

  Lowerer lowerer(ctx, mod.get(), fn);
  if (!lowerer.Run(builder)) {
    return false;  // unsupported opcode -> caller falls back to a64
  }

  if (llvm::verifyFunction(*fn, &llvm::errs())) {
    XELOGE("LLVMAssembler: verifyFunction failed for {}", name);
    return false;
  }

  // Optimize (mem2reg/SROA/GVN/instcombine/...) for residency before codegen.
  // O2/O3 give the win but are very slow emulated under qemu, so the opt level
  // is a cvar (0 = skip = fast device-free correctness tests).
  int opt = cvars::cpu_backend_llvm_opt;
  if (opt > 0) {
    llvm::PassBuilder pb;
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);
    auto level = opt >= 3   ? llvm::OptimizationLevel::O3
                 : opt == 1 ? llvm::OptimizationLevel::O1
                            : llvm::OptimizationLevel::O2;
    auto mpm = pb.buildPerModuleDefaultPipeline(level);
    mpm.run(*mod, mam);
  }

  if (auto err = jit.addIRModule(
          llvm::orc::ThreadSafeModule(std::move(mod), std::move(ctx_owner)))) {
    XELOGE("LLVMAssembler: addIRModule failed: {}",
           llvm::toString(std::move(err)));
    return false;
  }
  auto sym = jit.lookup(name);
  if (!sym) {
    XELOGE("LLVMAssembler: lookup failed: {}",
           llvm::toString(sym.takeError()));
    return false;
  }
  auto* code = reinterpret_cast<uint8_t*>(sym->getValue());
  static_cast<a64::A64Function*>(function)->Setup(code, 0);
  return true;
}

#else  // !XE_LLVM_BACKEND_ENABLED

bool LLVMAssembler::LowerAndJit(GuestFunction*, HIRBuilder*) { return false; }

#endif  // XE_LLVM_BACKEND_ENABLED

bool LLVMAssembler::Assemble(GuestFunction* function, hir::HIRBuilder* builder,
                             uint32_t debug_info_flags,
                             std::unique_ptr<FunctionDebugInfo> debug_info) {
#if XE_LLVM_BACKEND_ENABLED
  if (LowerAndJit(function, builder)) {
    function->set_debug_info(std::move(debug_info));
    return true;
  }
#endif
  // Fall back to the a64 per-block JIT for anything the LLVM path can't lower.
  return fallback_->Assemble(function, builder, debug_info_flags,
                             std::move(debug_info));
}

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe
