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
#include "xenia/cpu/ppc/ppc_context.h"

#ifndef XE_LLVM_BACKEND_ENABLED
#define XE_LLVM_BACKEND_ENABLED 0
#endif

#if XE_LLVM_BACKEND_ENABLED
#include <cstdlib>
#include <mutex>
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
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#endif  // XE_LLVM_BACKEND_ENABLED

DECLARE_int32(cpu_backend_llvm_opt);
DECLARE_string(cpu_backend_llvm_range_lo);
DECLARE_string(cpu_backend_llvm_range_hi);
DECLARE_bool(cpu_backend_llvm_dump_ir);
DECLARE_int32(cpu_backend_llvm_max_fns);
DECLARE_string(cpu_backend_llvm_trace_addr);

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
  Lowerer(llvm::LLVMContext& ctx, llvm::Module* mod, llvm::Function* fn,
          uint32_t guest_addr)
      : ctx_(ctx), mod_(mod), fn_(fn), b_(ctx), guest_addr_(guest_addr) {}

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

  // VMX float32x4 denormal flush on a <4 x i32> bit-pattern vector: per lane a
  // denormal (exp==0, mantissa!=0) becomes SIGNED zero, exactly as a64's
  // FlushDenormals_V128. Predicate: ((val<<1) - 1) <u 0x00FFFFFF; flushed value
  // = val & 0x80000000 (keep sign). Zeros (val<<1==0 -> 0xFFFFFFFF) and normals
  // are untouched. Branchless so it matches the a64 lane-for-lane.
  llvm::Value* VmxFlushDenorm(llvm::Value* iv) {
    auto* vty = iv->getType();  // <4 x i32>
    auto* one = llvm::ConstantInt::get(vty, 1);
    auto* shifted = b_.CreateShl(iv, one);
    auto* m1 = b_.CreateSub(shifted, one);
    auto* is_den =
        b_.CreateICmpULT(m1, llvm::ConstantInt::get(vty, 0x00FFFFFF));
    auto* sign = b_.CreateAnd(iv, llvm::ConstantInt::get(vty, 0x80000000));
    return b_.CreateSelect(is_den, sign, iv);
  }
  // PPC NaN propagation fixup on a <4 x i32> FP result, given the flushed source
  // bit-patterns in PRIORITY order (srcs[0] highest). Matches a64's
  // FixupVmxNan_V128*: where the result lane is NaN, take the first NaN source
  // (quieted: | 0x00400000); if no source is NaN it's a generated NaN -> the PPC
  // default 0xFFC00000. NaN predicate: (val<<1) >u 0xFF000000 (excludes +/-inf).
  llvm::Value* VmxNanFixup(llvm::Value* res,
                           std::initializer_list<llvm::Value*> srcs) {
    auto* vty = res->getType();  // <4 x i32>
    auto* one = llvm::ConstantInt::get(vty, 1);
    auto* nan_thr = llvm::ConstantInt::get(vty, 0xFF000000);
    auto* qbit = llvm::ConstantInt::get(vty, 0x00400000);
    auto is_nan = [&](llvm::Value* v) {
      return b_.CreateICmpUGT(b_.CreateShl(v, one), nan_thr);
    };
    llvm::Value* pick = llvm::ConstantInt::get(vty, 0xFFC00000);
    // Fold from lowest priority up so the highest-priority NaN source wins.
    for (auto it = std::rbegin(srcs); it != std::rend(srcs); ++it) {
      pick = b_.CreateSelect(is_nan(*it), b_.CreateOr(*it, qbit), pick);
    }
    return b_.CreateSelect(is_nan(res), pick, res);
  }

  // Apply a u32->u32 runtime helper to each of the 4 i32 lanes of a VEC128 and
  // return the rebuilt <4 x i32> carrier (vrsqrtefp/vlogefp/vexptefp). The
  // helper is a pure function (no x20/x21 dependency; AAPCS-callee-saved).
  llvm::Value* EmitVecLaneCall(const char* name, llvm::Value* vec) {
    auto* i32 = b_.getInt32Ty();
    auto callee = mod_->getOrInsertFunction(
        name, llvm::FunctionType::get(i32, {i32}, false));
    auto* lt = LaneVecTy(INT32_TYPE);
    auto* xv = b_.CreateBitCast(vec, lt);
    llvm::Value* r = llvm::PoisonValue::get(lt);
    for (int lane = 0; lane < 4; lane++) {
      auto* c = b_.CreateCall(
          callee, {b_.CreateExtractElement(xv, b_.getInt32(lane))});
      r = b_.CreateInsertElement(r, c, b_.getInt32(lane));
    }
    return b_.CreateBitCast(r, T(VEC128_TYPE));
  }

  // A scratch alloca placed in the function ENTRY block (allocated once, so it
  // doesn't grow the stack if the using instruction is in a loop). For passing a
  // VEC128 by pointer to a C runtime helper (e.g. xe_llvm_unpack).
  llvm::AllocaInst* EntryAlloca(llvm::Type* ty) {
    llvm::IRBuilder<> eb(&fn_->getEntryBlock(), fn_->getEntryBlock().begin());
    return eb.CreateAlloca(ty);
  }

  // Store a VEC128 as FOUR volatile 32-bit stores (base+0/4/8/12). Each is a
  // single decodable STR for the access-violation handler (a q-store that faults
  // on a GPU write-watch / MMIO page can't be decoded -> BD hangs); volatile
  // stops LLVM re-merging them into a q-store. Mirrors the 4-load vector LOAD.
  void StoreVec128AsWords(llvm::Value* val, llvm::Value* base) {
    auto* v = b_.CreateBitCast(val, LaneVecTy(INT32_TYPE));
    for (int k = 0; k < 4; k++) {
      auto* p = b_.CreateGEP(b_.getInt8Ty(), base, b_.getInt64(4 * k));
      b_.CreateStore(b_.CreateExtractElement(v, b_.getInt32(k)), p,
                     /*isVolatile=*/true);
    }
  }

  // Emit a call to the runtime guest-call helper (resolves the target guest
  // function + invokes it). x20/x21 are AAPCS callee-saved across this C call.
  void EmitGuestCall(llvm::Value* target_i32) {
    auto* i32 = llvm::Type::getInt32Ty(ctx_);
    // xe_llvm_guest_call(target, ret_addr): resolve + invoke. ret_addr (the
    // guest return address stashed by SET_RETURN_ADDRESS) is forwarded to the
    // callee's x0 via the host->guest thunk, so the callee recognizes its own
    // blr RETURN. Defaults to 0 if SET_RETURN_ADDRESS was not emitted.
    auto* fty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_), {i32, i32},
                                        false);
    auto callee = mod_->getOrInsertFunction("xe_llvm_guest_call", fty);
    auto* ret_addr = b_.CreateTrunc(
        b_.CreateLoad(b_.getInt64Ty(), next_call_ret_addr_), i32);
    b_.CreateCall(callee, {target_i32, ret_addr});
  }

  // Guest CALL_EXTERN: call the extern HANDLER (C++) via xe_llvm_call_extern,
  // NOT the guest thunk address (calling the address re-enters the `sc; bclr`
  // thunk -> infinite recursion). The symbol Function* is baked in as a constant
  // (stable across the run); the helper dispatches to handler/extern_handler.
  void EmitCallExtern(xe::cpu::Function* fn) {
    auto* fty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                        {b_.getPtrTy()}, false);
    auto callee = mod_->getOrInsertFunction("xe_llvm_call_extern", fty);
    auto* sym_ptr = b_.CreateIntToPtr(
        b_.getInt64(reinterpret_cast<uint64_t>(fn)), b_.getPtrTy());
    b_.CreateCall(callee, {sym_ptr});
  }

  // A guest TAIL call (b/bctr in tail position): resolve the target host entry
  // and `musttail`-jump to it, REUSING this frame (vs nesting a host frame per
  // call, which overflowed the host stack on a guest tail-call loop = the device
  // signal storm). Passes THIS function's return address (x0) straight through,
  // matching a64's CALL_TAIL (`ldr x0,[GUEST_RET_ADDR]; br x9`). Emits the call
  // + a `ret` terminator. Returns false if the target can't be wired for a
  // guaranteed tail call (caller falls back to a64 for this function).
  bool EmitGuestTailCall(llvm::Value* target_i32) {
    auto* i32 = llvm::Type::getInt32Ty(ctx_);
    auto* i64 = llvm::Type::getInt64Ty(ctx_);
    auto* voidTy = llvm::Type::getVoidTy(ctx_);
    // void* xe_llvm_resolve_function(uint32_t target)
    auto* rfty = llvm::FunctionType::get(b_.getPtrTy(), {i32}, false);
    auto resolve = mod_->getOrInsertFunction("xe_llvm_resolve_function", rfty);
    auto* host = b_.CreateCall(resolve, {target_i32});
    // Callee ABI == this function's: void(i64 guest_return_address).
    auto* callee_ty = llvm::FunctionType::get(voidTy, {i64}, false);
    auto* my_ret = b_.CreateLoad(i64, my_ret_addr_);
    auto* call = b_.CreateCall(callee_ty, host, {my_ret});
    call->setTailCallKind(llvm::CallInst::TCK_MustTail);
    b_.CreateRetVoid();
    return true;
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
        case VEC128_TYPE: {
          // Materialize a 128-bit constant as the <4 x i32> carrier (element k =
          // word k = vec128_t.u32[k], matching the a64 LoadV128Const byte order).
          uint32_t w[4] = {v->constant.v128.u32[0], v->constant.v128.u32[1],
                           v->constant.v128.u32[2], v->constant.v128.u32[3]};
          return llvm::ConstantDataVector::get(ctx_,
                                               llvm::ArrayRef<uint32_t>(w, 4));
        }
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
    // The guest effective address is 32-bit (Xbox 360 has a 4GB guest space;
    // PPC EAs wrap at 32 bits). CRITICAL: reduce ea to 32 bits and ZERO-extend
    // before adding membase. HIR constants for guest addresses >= 0x80000000
    // (very common - e.g. 0x82751C6C) arrive SIGN-extended to i64
    // (0xFFFFFFFF82751C6C); adding that to membase via GEP subtracts instead of
    // adds (membase - 0x7D8AE394) -> a wild host address below membase -> reads
    // garbage. Truncate-then-zext makes membase + 0x0000000082751C6C, matching
    // a64's 32-bit address computation (and correctly wraps 32-bit overflow).
    auto* ea32 = b_.CreateTrunc(b_.CreateZExtOrTrunc(ea, b_.getInt64Ty()),
                                b_.getInt32Ty());
    auto* ea64 = b_.CreateZExt(ea32, b_.getInt64Ty());
    return b_.CreateGEP(b_.getInt8Ty(), membase_, ea64);
  }
  llvm::Value* MaybeByteSwap(llvm::Value* v, llvm::Type* ty, uint16_t flags) {
    if (!(flags & LOAD_STORE_BYTE_SWAP)) return v;
    if (ty->isVectorTy()) {
      // VEC128 = rev32 (bswap each 32-bit lane).
      return b_.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, v);
    }
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
  uint32_t guest_addr_ = 0;
  bool trace_this_ = false;
  void EmitTrace(uint32_t tag) {
    auto* fty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                        {b_.getInt32Ty()}, false);
    auto callee = mod_->getOrInsertFunction("xe_llvm_trace_entry", fty);
    b_.CreateCall(callee, {b_.getInt32(tag)});
  }
  // Guest TRAP -> call the xe_llvm_trap runtime helper with the trap-type flags.
  void EmitTrapCall(uint32_t flags) {
    auto* fty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                        {b_.getInt32Ty()}, false);
    auto callee = mod_->getOrInsertFunction("xe_llvm_trap", fty);
    b_.CreateCall(callee, {b_.getInt32(flags)});
  }
  llvm::Value* ctx_ptr_ = nullptr;
  llvm::Value* membase_ = nullptr;
  // a64 guest-call ABI: x0 = this function's guest return address (saved at
  // entry into my_ret_addr_). next_call_ret_addr_ is the return address that
  // SET_RETURN_ADDRESS stashes for the NEXT guest call. A CALL_INDIRECT whose
  // target == my_ret_addr_ is a guest `blr` RETURN, lowered to `ret` (not a
  // forward call) - otherwise the host stack never unwinds and overflows (the
  // device signal-storm root cause).
  llvm::AllocaInst* my_ret_addr_ = nullptr;
  llvm::AllocaInst* next_call_ret_addr_ = nullptr;
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

  // Save the incoming guest return address (x0, per the a64 host->guest thunk:
  // "mov x0, x2 // x0 = guest return address") and init the next-call slot.
  // Entry-block allocas are promoted to SSA by mem2reg at opt>0.
  my_ret_addr_ = b_.CreateAlloca(b_.getInt64Ty(), nullptr, "my_ret_addr");
  next_call_ret_addr_ =
      b_.CreateAlloca(b_.getInt64Ty(), nullptr, "next_call_ret_addr");
  // Return-address basis for recognizing this function's OWN blr RETURN.
  // HYBRID (matches a64's GUEST_RET_ADDR slot): use x0 when non-zero, else the
  // guest LR at entry.
  //  - bl-entered: the host->guest thunk passes x0 = the guest return address
  //    (== context->lr); both agree.
  //  - TAIL-call-entered (musttail, e.g. __savegprlr/__restgprlr): the caller
  //    passes x0 = the ORIGINAL return address, but context->lr is the tail-
  //    caller's last set value (stale) - AND __restgprlr RESTORES context->lr
  //    from the stack before its blr, so a context->lr basis never matches the
  //    restored target -> the blr became a forward call -> the wrapper that
  //    tail-returns through __restgprlr recursed forever (device-pinned: callee
  //    returned correct values but never EXITed; r28-r31 working, no unwind).
  //  - bctr-entered (indirect jump, not a call): x0 == 0 -> fall back to
  //    context->lr (the original reason this was context->lr, not x0).
  {
    auto* x0 = fn_->getArg(0);
    auto* ctx_lr = b_.CreateLoad(
        b_.getInt64Ty(), CtxPtr(offsetof(xe::cpu::ppc::PPCContext, lr)));
    auto* use_x0 = b_.CreateICmpNE(x0, b_.getInt64(0));
    b_.CreateStore(b_.CreateSelect(use_x0, x0, ctx_lr), my_ret_addr_);
  }
  b_.CreateStore(b_.getInt64(0), next_call_ret_addr_);

  // Entry trace (diagnostic): if this fn matches cpu_backend_llvm_trace_addr,
  // call xe_llvm_trace_entry(addr) to log its input regs - fires for ANY caller
  // backend (unlike the call-site trace), so a callee's args can be diffed
  // across a caller being a64 vs LLVM.
  {
    const std::string& ts = cvars::cpu_backend_llvm_trace_addr;
    uint32_t ta = ts.empty() ? 0 : uint32_t(std::strtoull(ts.c_str(), nullptr, 16));
    trace_this_ = (ta != 0 && ta == guest_addr_);
    if (trace_this_) EmitTrace(guest_addr_);  // entry: tag = fn addr
  }

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
    // Block trace: log the block's guest address + register state at entry, so
    // the LLVM control-flow path (and where it diverges from the expected
    // return path) is visible on-device.
    if (trace_this_) {
      uint32_t ga = 0;
      for (Instr* i = blk->instr_head; i && !ga; i = i->next)
        ga = i->GuestAddressFor();
      // tag = block guest addr if found, else 0x9B0000|ordinal as a fallback.
      EmitTrace(ga ? ga : (0x9B0000u | (blk->ordinal & 0xFFFF)));
    }
    for (Instr* i = blk->instr_head; i; i = i->next) {
      if (!LowerInstr(i)) {
        // Diagnostic (rate-limited): which opcode forced this function to fall
        // back to a64. A histogram of these across boot pinpoints the highest-
        // value opcodes to lower next so HOT functions get the residency win
        // (BD's hottest fn 0x824694A0 falls back today).
        static std::atomic<uint32_t> s_fb{0};
        uint32_t fb = s_fb.fetch_add(1, std::memory_order_relaxed);
        if (fb < 120) {
          XELOGW("LLVMfallback fn=0x{:08X} opcode={} (#{}) -> a64", guest_addr_,
                 i->opcode->name, fb);
        }
        return false;  // unsupported -> fallback
      }
      // A handler may emit a block terminator (a `ret` from a guest tail call /
      // return) BEFORE the HIR block's last instr; anything after it in the
      // block is then unreachable. Stop so we don't append instructions after a
      // terminator (invalid IR = "Terminator found in the middle of a basic
      // block", which forced an a64 fallback).
      if (b_.GetInsertBlock()->getTerminator()) break;
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
    case OPCODE_ADD_CARRY: {
      // dest = src1 + src2 + (carry & 1). src3 (carry, i8) is provably {0,1};
      // mask defensively. No carry-OUT here. Matches a64's add-the-carry path.
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      auto* carry = V(i->src3.value);
      auto* dt = T(i->dest->type);
      if (!a || !c || !carry || !dt) return false;
      auto* cy = b_.CreateAnd(b_.CreateZExtOrTrunc(carry, dt),
                              llvm::ConstantInt::get(dt, 1));
      Def(i->dest, b_.CreateAdd(b_.CreateAdd(a, c), cy));
      return true;
    }
    case OPCODE_IS_NAN: {
      // dest(i8) = isnan(src). fcmp uno src,src (unordered = NaN). Matches a64
      // (fcmp s,s; cset VS).
      auto* a = V(i->src1.value);
      auto* dt = T(i->dest->type);
      if (!a || !dt || !a->getType()->isFloatingPointTy()) return false;
      Def(i->dest, b_.CreateZExt(b_.CreateFCmpUNO(a, a), dt));
      return true;
    }
    case OPCODE_YIELD:
      // Guest spin-wait hint. a64 emits an ARM64 `yield`, but inline asm
      // report_fatal_errors in the ORCv2 AsmPrinter (no integrated assembler),
      // and llvm.aarch64.hint risks the same path - so lower it to a NO-OP. The
      // yield is a hint only, so this is correctness-exact; it just doesn't back
      // off a spinning core. BD's hottest fn 0x824694A0 uses this, so lowering it
      // (vs falling the whole fn back to a64) is what gets it the residency win.
      return true;
    case OPCODE_TRAP:
      // Unconditional guest trap (tw/td). Call the runtime trap helper; control
      // continues after (the helpers just log; the trap is rarely reached).
      EmitTrapCall(i->flags);
      return true;
    case OPCODE_TRAP_TRUE: {
      // Conditional guest trap: if cond != 0, call the trap helper, then CONTINUE
      // (the trap doesn't branch/return). Both edges rejoin the continuation.
      auto* cond = V(i->src1.value);
      if (!cond) return false;
      bool fresh_cont = (i->next != nullptr) || (i->block->next == nullptr);
      llvm::BasicBlock* cont = fresh_cont
                                   ? llvm::BasicBlock::Create(ctx_, "c", fn_)
                                   : BlockFor(i->block->next);
      auto* trap_bb = llvm::BasicBlock::Create(ctx_, "trap", fn_);
      b_.CreateCondBr(Truth(cond), trap_bb, cont);
      b_.SetInsertPoint(trap_bb);
      EmitTrapCall(i->flags);
      b_.CreateBr(cont);
      if (fresh_cont) {
        b_.SetInsertPoint(cont);
        if (!i->next && !i->block->next) b_.CreateRetVoid();
      }
      // else: cont is the next block; leave the insert point on the terminated
      // trap_bb so Run()'s fall-through adds no spurious branch.
      return true;
    }
    case OPCODE_DELAY_EXECUTION:
      // Guest delay/spin hint (a64 emits `yield`). No architectural effect.
      return true;
    case OPCODE_SET_ROUNDING_MODE:
    case OPCODE_SET_NJM:
      // Set the a64 backend's CACHED FPCR (rounding mode / VMX flush-to-zero) -
      // backend-internal state, not guest PPCContext; no guest register changes.
      // The LLVM backend bakes in round-to-nearest + VMX denormal flush (the
      // common 360 mode, NJM=1), so this is a no-op. (Guest non-nearest rounding
      // / NJM=0 are not honored - rare; revisit only if a title needs them.)
      return true;
    case OPCODE_TO_SINGLE: {
      // frsp: round a double to single precision and back. fcvt round-to-nearest
      // (scalar FPU mode, no FZ); fcvt preserves NaN sign, so no fixup needed.
      auto* a = V(i->src1.value);
      if (!a || !a->getType()->isDoubleTy()) return false;
      Def(i->dest, b_.CreateFPExt(b_.CreateFPTrunc(a, b_.getFloatTy()),
                                  b_.getDoubleTy()));
      return true;
    }
    case OPCODE_DEBUG_BREAK:
      // Guest debug breakpoint -> trap (a64 emits BRK). Not differential-testable
      // (it halts); correct by composition with the trap helper.
      EmitTrapCall(0);
      return true;
    case OPCODE_DEBUG_BREAK_TRUE: {
      auto* cond = V(i->src1.value);
      if (!cond) return false;
      bool fresh_cont = (i->next != nullptr) || (i->block->next == nullptr);
      llvm::BasicBlock* cont = fresh_cont
                                   ? llvm::BasicBlock::Create(ctx_, "c", fn_)
                                   : BlockFor(i->block->next);
      auto* brk_bb = llvm::BasicBlock::Create(ctx_, "brk", fn_);
      b_.CreateCondBr(Truth(cond), brk_bb, cont);
      b_.SetInsertPoint(brk_bb);
      EmitTrapCall(0);
      b_.CreateBr(cont);
      if (fresh_cont) {
        b_.SetInsertPoint(cont);
        if (!i->next && !i->block->next) b_.CreateRetVoid();
      }
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
      if (a->getType()->isVectorTy()) {
        // VEC128 byteswap = a64 rev32 = byte-reverse within each 32-bit lane =
        // llvm.bswap on the <4 x i32> carrier.
        Def(i->dest, b_.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, a));
      } else if (a->getType()->getIntegerBitWidth() <= 8) {
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
      if (ty->isVectorTy()) {
        // 128-bit vector load as FOUR volatile 32-bit loads (base+0/4/8/12).
        // Each is a single decodable LDR for the access-violation handler (a
        // single q-load that faults can't be decoded). volatile => LLVM won't
        // re-merge them into a q-load. Element k = word k, matching a q-load.
        auto* base = MemPtr(ea);
        auto* i32x4 = LaneVecTy(INT32_TYPE);
        llvm::Value* vec = llvm::PoisonValue::get(i32x4);
        for (int k = 0; k < 4; k++) {
          auto* p = b_.CreateGEP(b_.getInt8Ty(), base, b_.getInt64(4 * k));
          vec = b_.CreateInsertElement(
              vec, b_.CreateLoad(b_.getInt32Ty(), p, /*isVolatile=*/true),
              b_.getInt32(k));
        }
        Def(i->dest, MaybeByteSwap(vec, ty, i->flags));
        return true;
      }
      auto* v = b_.CreateLoad(ty, MemPtr(ea), /*isVolatile=*/true);
      Def(i->dest, MaybeByteSwap(v, ty, i->flags));
      return true;
    }
    case OPCODE_STORE: {
      auto* ea = V(i->src1.value);
      auto* val = V(i->src2.value);
      if (!ea || !val) return false;
      val = MaybeByteSwap(val, val->getType(), i->flags);
      if (IsVec(val)) {
        StoreVec128AsWords(val, MemPtr(ea));
        return true;
      }
      b_.CreateStore(val, MemPtr(ea), /*isVolatile=*/true);
      return true;
    }
    case OPCODE_LOAD_OFFSET: {
      auto* ty = T(i->dest->type);
      auto* base = V(i->src1.value);
      auto* off = V(i->src2.value);
      if (!ty || !base || !off) return false;
      auto* ea = b_.CreateAdd(b_.CreateZExtOrTrunc(base, b_.getInt64Ty()),
                              b_.CreateZExtOrTrunc(off, b_.getInt64Ty()));
      if (ty->isVectorTy()) {
        auto* hp = MemPtr(ea);
        auto* i32x4 = LaneVecTy(INT32_TYPE);
        llvm::Value* vec = llvm::PoisonValue::get(i32x4);
        for (int k = 0; k < 4; k++) {
          auto* p = b_.CreateGEP(b_.getInt8Ty(), hp, b_.getInt64(4 * k));
          vec = b_.CreateInsertElement(
              vec, b_.CreateLoad(b_.getInt32Ty(), p, /*isVolatile=*/true),
              b_.getInt32(k));
        }
        Def(i->dest, MaybeByteSwap(vec, ty, i->flags));
        return true;
      }
      auto* v = b_.CreateLoad(ty, MemPtr(ea), /*isVolatile=*/true);
      Def(i->dest, MaybeByteSwap(v, ty, i->flags));
      return true;
    }
    case OPCODE_STORE_OFFSET: {
      auto* base = V(i->src1.value);
      auto* off = V(i->src2.value);
      auto* val = V(i->src3.value);
      if (!base || !off || !val) return false;
      auto* ea = b_.CreateAdd(b_.CreateZExtOrTrunc(base, b_.getInt64Ty()),
                              b_.CreateZExtOrTrunc(off, b_.getInt64Ty()));
      val = MaybeByteSwap(val, val->getType(), i->flags);
      if (IsVec(val)) {
        StoreVec128AsWords(val, MemPtr(ea));
        return true;
      }
      b_.CreateStore(val, MemPtr(ea), /*isVolatile=*/true);
      return true;
    }
    case OPCODE_ATOMIC_COMPARE_EXCHANGE: {
      // dest(i8 success) = CAS(*[membase+EA], expected, desired). Matches a64's
      // casal / ldaxr-stlxr-retry: strong CAS, dest = (old == expected). src1 =
      // guest EA, src2 = expected, src3 = desired (i32 or i64). Acquire-release
      // ordering == casal (acquire on the load, release on the store).
      auto* ea = V(i->src1.value);
      auto* expected = V(i->src2.value);
      auto* desired = V(i->src3.value);
      if (!ea || !expected || !desired) return false;
      auto* ity = expected->getType();
      if (!ity->isIntegerTy() || expected->getType() != desired->getType()) {
        return false;
      }
      unsigned bits = ity->getIntegerBitWidth();
      if (bits != 32 && bits != 64) return false;
      auto* cx = b_.CreateAtomicCmpXchg(
          MemPtr(ea), expected, desired, llvm::MaybeAlign(bits / 8),
          llvm::AtomicOrdering::AcquireRelease, llvm::AtomicOrdering::Acquire,
          llvm::SyncScope::System);
      // strong CAS (no spurious failure) = the a64 retry loop / LSE casal.
      cx->setWeak(false);
      Def(i->dest, b_.CreateZExt(b_.CreateExtractValue(cx, 1), T(i->dest->type)));
      return true;
    }
    case OPCODE_CACHE_CONTROL:
      // Prefetch / cache-flush hints only (dcbt/dcbtst/dcbf/dcbst). No
      // architectural effect in the emulator: host guest-memory is always
      // coherent, and dcbz (zero a line) is lowered as a store by the PPC
      // frontend, NOT as CACHE_CONTROL. The a64 backend emits only prfm / dc
      // civac here, neither of which changes guest-visible state -> a no-op is
      // byte-identical to the a64 result (and lets the function stay in LLVM).
      return true;
    case OPCODE_MEMSET: {
      // dcbz/dcbz128: zero `length` bytes at membase+addr (value const 0).
      auto* addr = V(i->src1.value);
      auto* val = V(i->src2.value);
      auto* len = V(i->src3.value);
      if (!addr || !val || !len) return false;
      if (!val->getType()->isIntegerTy(8)) {
        val = b_.CreateTrunc(val, b_.getInt8Ty());
      }
      b_.CreateMemSet(MemPtr(addr), val, len, llvm::MaybeAlign(1));
      return true;
    }
    // OPCODE_LVL/LVR/STVL/STVR are unemittable dead opcodes: the PPC frontend
    // lowers lvlx/lvrx/stvlx/stvrx as PERMUTE + LOAD_VECTOR_SHL/SHR + a vector
    // LOAD/STORE + BYTE_SWAP (all handled above), never these opcodes. So they
    // need no lowering (they'd just fall through to the a64 fallback if emitted).

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
      // Same insert-point rule as BRANCH_TRUE / CALL_INDIRECT_TRUE: only step
      // into a FRESH continuation; if the false edge flows to the next block's
      // existing LLVM block, leave the insert point on the terminated ret path
      // so Run() adds no spurious self-branch.
      bool fresh_cont = (i->next != nullptr) || (i->block->next == nullptr);
      llvm::BasicBlock* cont = fresh_cont
                                   ? llvm::BasicBlock::Create(ctx_, "c", fn_)
                                   : BlockFor(i->block->next);
      b_.CreateCondBr(Truth(cond), ret_bb, cont);
      b_.SetInsertPoint(ret_bb);
      b_.CreateRetVoid();
      if (fresh_cont) {
        b_.SetInsertPoint(cont);
        if (!i->next && !i->block->next) b_.CreateRetVoid();
      }
      return true;
    }

    // ---- guest calls (P4) ----
    case OPCODE_CALL: {
      auto* tgt = b_.getInt32(i->src1.symbol->address());
      if (i->flags & CALL_TAIL) {
        if (!EmitGuestTailCall(tgt)) return false;
      } else {
        EmitGuestCall(tgt);
      }
      return true;
    }
    case OPCODE_CALL_EXTERN: {
      // Call the extern HANDLER (C++), NOT the guest thunk address - calling the
      // address re-enters the `sc; bclr` thunk -> infinite recursion = the storm.
      EmitCallExtern(i->src1.symbol);
      return true;
    }
    case OPCODE_CALL_INDIRECT: {
      auto* t = V(i->src1.value);
      if (!t) return false;
      auto* t32 = b_.CreateZExtOrTrunc(t, b_.getInt32Ty());
      bool is_tail = (i->flags & CALL_TAIL) != 0;
      if (i->flags & CALL_POSSIBLE_RETURN) {
        // A guest `blr` whose target == our own guest return address (x0) is a
        // RETURN, not a forward call. Lower it to `ret` so the host stack
        // unwinds. Without this, EVERY guest return became a forward call and
        // the host stack only ever grew -> overflow = the device signal storm.
        // Mirrors A64Emitter::CallIndirect's CALL_POSSIBLE_RETURN check.
        auto* mine = b_.CreateTrunc(
            b_.CreateLoad(b_.getInt64Ty(), my_ret_addr_), b_.getInt32Ty());
        auto* is_ret =
            b_.CreateICmpEQ(b_.CreateZExtOrTrunc(t, b_.getInt32Ty()), mine);
        auto* ret_bb = llvm::BasicBlock::Create(ctx_, "blr_ret", fn_);
        auto* call_bb = llvm::BasicBlock::Create(ctx_, "blr_call", fn_);
        b_.CreateCondBr(is_ret, ret_bb, call_bb);
        b_.SetInsertPoint(ret_bb);
        b_.CreateRetVoid();
        b_.SetInsertPoint(call_bb);
        if (is_tail) {
          // Not a return -> a tail call; reuse the frame (musttail + ret).
          if (!EmitGuestTailCall(t32)) return false;
        } else {
          EmitGuestCall(t32);
          // Continue after a (non-return) forward call. Leave call_bb
          // terminated for the block->next case so Run()'s fall-through adds
          // no self-branch.
          if (i->next) {
            auto* cont_bb = llvm::BasicBlock::Create(ctx_, "c", fn_);
            b_.CreateBr(cont_bb);
            b_.SetInsertPoint(cont_bb);
          } else if (i->block->next) {
            b_.CreateBr(BlockFor(i->block->next));
          } else {
            b_.CreateRetVoid();
          }
        }
      } else if (is_tail) {
        if (!EmitGuestTailCall(t32)) return false;
      } else {
        EmitGuestCall(t32);
      }
      return true;
    }
    case OPCODE_SET_RETURN_ADDRESS: {
      // Stash the return address for the NEXT guest call (a64 stores it to its
      // GUEST_CALL_RET_ADDR stack slot; EmitGuestCall forwards it as the
      // callee's x0). Required so the callee can recognize its own blr RETURN.
      auto* v = V(i->src1.value);
      if (!v) return false;
      b_.CreateStore(b_.CreateZExtOrTrunc(v, b_.getInt64Ty()),
                     next_call_ret_addr_);
      return true;
    }
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
      bool is_tail = (i->flags & CALL_TAIL) != 0;
      bool poss_ret = (i->flags & CALL_POSSIBLE_RETURN) != 0;
      auto* taken_bb = llvm::BasicBlock::Create(ctx_, "ctrue", fn_);
      // The cond-FALSE (not-taken) destination. CRITICAL (mirrors BRANCH_TRUE):
      // when this conditional call is the LAST instr of the block and the block
      // has a successor, the false edge flows DIRECTLY to that successor's LLVM
      // block and we must NOT leave the IRBuilder insert point there - otherwise
      // Run()'s end-of-block fall-through sees that (terminator-less) successor
      // and emits `CreateBr(successor)` INTO it = an empty `br self` infinite
      // loop (device-pinned: strncpy 0x826C0D98's copy-loop block became
      // `b1: br b1` -> livelock). `fresh_cont` = the false block is a fresh
      // continuation we keep emitting into.
      bool fresh_cont = (i->next != nullptr) || (i->block->next == nullptr);
      llvm::BasicBlock* cont_bb =
          fresh_cont ? llvm::BasicBlock::Create(ctx_, "c", fn_)
                     : BlockFor(i->block->next);
      b_.CreateCondBr(Truth(cond), taken_bb, cont_bb);
      b_.SetInsertPoint(taken_bb);
      // When the condition is TRUE this is a conditional guest blr/bctr. Honor
      // POSSIBLE_RETURN (a RETURN when target == our entry LR) and TAIL, exactly
      // like CALL_INDIRECT. Without that, a conditional RETURN became a forward
      // call (e.g. strncpy's `bclr if count==0`).
      if (poss_ret) {
        auto* mine = b_.CreateTrunc(
            b_.CreateLoad(b_.getInt64Ty(), my_ret_addr_), b_.getInt32Ty());
        auto* is_ret = b_.CreateICmpEQ(target, mine);
        auto* ret_bb = llvm::BasicBlock::Create(ctx_, "ctrue_ret", fn_);
        auto* fwd_bb = llvm::BasicBlock::Create(ctx_, "ctrue_fwd", fn_);
        b_.CreateCondBr(is_ret, ret_bb, fwd_bb);
        b_.SetInsertPoint(ret_bb);
        b_.CreateRetVoid();
        b_.SetInsertPoint(fwd_bb);
        if (is_tail) {
          if (!EmitGuestTailCall(target)) return false;  // terminates fwd_bb
        } else {
          EmitGuestCall(target);
          b_.CreateBr(cont_bb);  // taken (non-return) rejoins the false path
        }
      } else if (is_tail) {
        if (!EmitGuestTailCall(target)) return false;  // terminates taken_bb
      } else {
        EmitGuestCall(target);
        b_.CreateBr(cont_bb);  // taken rejoins the false path
      }
      // Position for the rest of lowering. Only step into cont_bb when it is a
      // fresh continuation; otherwise leave the insert point on the (terminated)
      // taken/fwd block so Run() adds no spurious fall-through branch.
      if (fresh_cont) {
        b_.SetInsertPoint(cont_bb);
        if (!i->next && !i->block->next) b_.CreateRetVoid();
      }
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
    case OPCODE_LOAD_VECTOR_SHL:
    case OPCODE_LOAD_VECTOR_SHR: {
      // lvsl/lvsr permute-control vector: a base byte pattern +/- splat(sh & 0xF)
      // (8-bit wrapping), exactly matching the a64 sequence. The base is in PPC
      // byte order (byte-swapped within each 32-bit word): SHL indices 0..15 ->
      // {3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12}; SHR is indices 16..31. The
      // <16 x i8> element k maps to memory byte k (little-endian carrier), so it
      // is byte-identical to the a64 store. Pure integer => exact.
      auto* sh = V(i->src1.value);  // i8 shift amount
      if (!sh) return false;
      bool shl = (op == OPCODE_LOAD_VECTOR_SHL);
      const uint8_t shl_base[16] = {3, 2, 1, 0,  7,  6,  5,  4,
                                    11, 10, 9, 8, 15, 14, 13, 12};
      const uint8_t shr_base[16] = {19, 18, 17, 16, 23, 22, 21, 20,
                                    27, 26, 25, 24, 31, 30, 29, 28};
      auto* base = llvm::ConstantDataVector::get(
          ctx_, llvm::ArrayRef<uint8_t>(shl ? shl_base : shr_base, 16));
      auto* amt = b_.CreateVectorSplat(
          16, b_.CreateAnd(sh, b_.getInt8(0xF)));  // <16 x i8>
      auto* r = shl ? b_.CreateAdd(base, amt) : b_.CreateSub(base, amt);
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_INSERT: {
      // dest = src1 with element[idx] = src3. The logical lane index is remapped
      // to the byte-swapped physical lane exactly as a64's VEC128_B/W/D macros:
      // i8 ^ 3, i16 ^ 1, i32 unchanged (byte/halfword swap within 32-bit words).
      auto* vec = V(i->src1.value);
      auto* idxv = V(i->src2.value);
      auto* val = V(i->src3.value);
      if (!vec || !idxv || !val) return false;
      TypeName et = i->src3.value->type;
      unsigned xorm;
      switch (et) {
        case INT8_TYPE: xorm = 3; break;
        case INT16_TYPE: xorm = 1; break;
        case INT32_TYPE: xorm = 0; break;
        default: return false;  // f32/i64 element inserts -> a64
      }
      auto* lt = LaneVecTy(et);
      if (!lt || val->getType() != lt->getScalarType()) return false;
      auto* idx = b_.CreateZExtOrTrunc(idxv, b_.getInt32Ty());
      if (xorm) idx = b_.CreateXor(idx, b_.getInt32(xorm));
      auto* r = b_.CreateInsertElement(b_.CreateBitCast(vec, lt), val, idx);
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_EXTRACT: {
      // dest(iN) = src1.element[idx], same byte-swapped lane remap as INSERT.
      // Mask the (possibly dynamic) physical index to the lane count, matching
      // a64's `and w, lanes-1` so an out-of-range index can't be UB.
      auto* vec = V(i->src1.value);
      auto* idxv = V(i->src2.value);
      if (!vec || !idxv) return false;
      TypeName dt = i->dest->type;
      unsigned xorm, mask;
      switch (dt) {
        case INT8_TYPE: xorm = 3; mask = 0xF; break;
        case INT16_TYPE: xorm = 1; mask = 0x7; break;
        case INT32_TYPE: xorm = 0; mask = 0x3; break;
        default: return false;
      }
      auto* lt = LaneVecTy(dt);
      if (!lt) return false;
      auto* idx = b_.CreateZExtOrTrunc(idxv, b_.getInt32Ty());
      if (xorm) idx = b_.CreateXor(idx, b_.getInt32(xorm));
      idx = b_.CreateAnd(idx, b_.getInt32(mask));
      Def(i->dest, b_.CreateExtractElement(b_.CreateBitCast(vec, lt), idx));
      return true;
    }
    case OPCODE_MUL_ADD:
    case OPCODE_MUL_SUB: {
      // VMX float32x4 fused multiply-add/sub (vmaddfp / vnmsubfp): dest =
      // s1*s2 (+/-) s3, single-rounded (llvm.fma), with the full PPC semantics =
      // flush denormal inputs -> fma -> PPC NaN fixup -> flush denormal output.
      // Byte-identical to a64 MUL_ADD_V128 / MUL_SUB_V128. Scalar f32/f64 FMA
      // still falls back to a64.
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      auto* d = V(i->src3.value);
      if (!a || !c || !d || !IsVec(a) || !IsVec(c) || !IsVec(d)) return false;
      auto* i32x4 = T(VEC128_TYPE);
      auto* f32x4 = LaneVecTy(FLOAT32_TYPE);
      auto* s1i = VmxFlushDenorm(b_.CreateBitCast(a, i32x4));
      auto* s2i = VmxFlushDenorm(b_.CreateBitCast(c, i32x4));
      auto* s3i = VmxFlushDenorm(b_.CreateBitCast(d, i32x4));
      auto* f3 = b_.CreateBitCast(s3i, f32x4);
      if (op == OPCODE_MUL_SUB) f3 = b_.CreateFNeg(f3);  // s1*s2 - s3
      auto* res = b_.CreateIntrinsic(
          llvm::Intrinsic::fma, {f32x4},
          {b_.CreateBitCast(s1i, f32x4), b_.CreateBitCast(s2i, f32x4), f3});
      auto* resi = b_.CreateBitCast(res, i32x4);
      // NaN fixup uses the UN-negated flushed sources (a64 saves un-negated s3).
      resi = VmxNanFixup(resi, {s1i, s2i, s3i});
      resi = VmxFlushDenorm(resi);
      Def(i->dest, b_.CreateBitCast(resi, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_DOT_PRODUCT_3:
    case OPCODE_DOT_PRODUCT_4: {
      // vmsum3fp / vmsum4fp: dot product in DOUBLE precision (so f32*f32 is
      // exact), summed in the exact a64 lane order, narrowed to f32, with VMX
      // denormal flush (in + out, via the FZ the a64 path relies on) and the
      // a64 overflow rule (|result| == +inf -> 0x7FC00000 QNaN). Result is a
      // scalar (F32 dest) or splatted to all 4 lanes (V128 dest).
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c || !IsVec(a) || !IsVec(c)) return false;
      auto* i32x4 = T(VEC128_TYPE);
      auto* f32x4 = LaneVecTy(FLOAT32_TYPE);
      auto* dblx4 = llvm::VectorType::get(b_.getDoubleTy(), 4, false);
      auto* fa = b_.CreateBitCast(VmxFlushDenorm(b_.CreateBitCast(a, i32x4)),
                                  f32x4);
      auto* fb = b_.CreateBitCast(VmxFlushDenorm(b_.CreateBitCast(c, i32x4)),
                                  f32x4);
      auto* prod = b_.CreateFMul(b_.CreateFPExt(fa, dblx4),
                                 b_.CreateFPExt(fb, dblx4));  // exact products
      auto* p0 = b_.CreateExtractElement(prod, b_.getInt32(0));
      auto* p1 = b_.CreateExtractElement(prod, b_.getInt32(1));
      auto* p2 = b_.CreateExtractElement(prod, b_.getInt32(2));
      llvm::Value* sum;
      if (op == OPCODE_DOT_PRODUCT_3) {
        sum = b_.CreateFAdd(b_.CreateFAdd(p0, p1), p2);  // (p0+p1)+p2
      } else {
        auto* p3 = b_.CreateExtractElement(prod, b_.getInt32(3));
        sum = b_.CreateFAdd(b_.CreateFAdd(p0, p2),
                            b_.CreateFAdd(p1, p3));  // (p0+p2)+(p1+p3)
      }
      auto* sumi = b_.CreateBitCast(b_.CreateFPTrunc(sum, b_.getFloatTy()),
                                    b_.getInt32Ty());
      // Flush a denormal output to signed zero.
      auto* den = b_.CreateICmpULT(
          b_.CreateSub(b_.CreateShl(sumi, b_.getInt32(1)), b_.getInt32(1)),
          b_.getInt32(0x00FFFFFF));
      sumi = b_.CreateSelect(
          den, b_.CreateAnd(sumi, b_.getInt32(0x80000000)), sumi);
      // |result| == +inf -> PPC QNaN 0x7FC00000.
      auto* isinf = b_.CreateICmpEQ(
          b_.CreateAnd(sumi, b_.getInt32(0x7FFFFFFF)), b_.getInt32(0x7F800000));
      sumi = b_.CreateSelect(isinf, b_.getInt32(0x7FC00000), sumi);
      if (i->dest->type == VEC128_TYPE) {
        Def(i->dest,
            b_.CreateBitCast(b_.CreateVectorSplat(4, sumi), T(VEC128_TYPE)));
      } else if (i->dest->type == FLOAT32_TYPE) {
        Def(i->dest, b_.CreateBitCast(sumi, b_.getFloatTy()));
      } else {
        return false;
      }
      return true;
    }
    case OPCODE_UNPACK: {
      // VMX unpack via the xe_llvm_unpack C helper (reuses xenos_half_to_float +
      // the magic-float math). 8_IN_16 / 16_IN_32 (intricate lane juggling) still
      // fall back to a64. Pass the VEC128 by pointer through an entry-block
      // scratch alloca.
      uint32_t mode = i->flags & PACK_TYPE_MODE;
      if (mode == PACK_TYPE_8_IN_16 || mode == PACK_TYPE_16_IN_32) return false;
      auto* val = V(i->src1.value);
      if (!val) return false;
      auto* i32x4 = T(VEC128_TYPE);
      auto* scratch = EntryAlloca(i32x4);
      b_.CreateStore(b_.CreateBitCast(val, i32x4), scratch);
      auto callee = mod_->getOrInsertFunction(
          "xe_llvm_unpack",
          llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                  {b_.getPtrTy(), b_.getInt32Ty()}, false));
      b_.CreateCall(callee, {scratch, b_.getInt32(i->flags)});
      Def(i->dest, b_.CreateLoad(i32x4, scratch));
      return true;
    }
    case OPCODE_PACK: {
      // VMX pack via xe_llvm_pack (clamp + float_to_xenos_half + bit-pack) for
      // the single-input float formats. 8_IN_16/16_IN_32 (2-input + saturate)
      // fall back to a64. src1 = the float vector; src2 is the const-zero second
      // operand the single-input packs ignore.
      uint32_t mode = i->flags & PACK_TYPE_MODE;
      if (mode == PACK_TYPE_8_IN_16 || mode == PACK_TYPE_16_IN_32) return false;
      auto* val = V(i->src1.value);
      if (!val) return false;
      auto* i32x4 = T(VEC128_TYPE);
      auto* scratch = EntryAlloca(i32x4);
      b_.CreateStore(b_.CreateBitCast(val, i32x4), scratch);
      auto callee = mod_->getOrInsertFunction(
          "xe_llvm_pack",
          llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                  {b_.getPtrTy(), b_.getInt32Ty()}, false));
      b_.CreateCall(callee, {scratch, b_.getInt32(i->flags)});
      Def(i->dest, b_.CreateLoad(i32x4, scratch));
      return true;
    }
    case OPCODE_VECTOR_DENORMFLUSH: {
      // Per-lane: exp==0 (zero or denormal) -> keep only the sign bit (signed
      // zero); else unchanged. Identical result to a64 VECTOR_DENORMFLUSH and
      // exactly the VmxFlushDenorm helper.
      auto* v = V(i->src1.value);
      if (!v) return false;
      Def(i->dest, VmxFlushDenorm(b_.CreateBitCast(v, T(VEC128_TYPE))));
      return true;
    }
    case OPCODE_VECTOR_AVERAGE: {
      // Rounding halving add (a+b+1)>>1 per lane = a64 urhadd/srhadd. Integer
      // lanes only; signed vs unsigned from the ARITHMETIC_UNSIGNED flag.
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c) return false;
      TypeName pt = static_cast<TypeName>(i->flags & 0xFF);
      bool uns = ((i->flags >> 8) & ARITHMETIC_UNSIGNED) != 0;
      auto* lt = LaneVecTy(pt);
      if (!lt || pt == FLOAT32_TYPE) return false;
      auto id = uns ? llvm::Intrinsic::aarch64_neon_urhadd
                    : llvm::Intrinsic::aarch64_neon_srhadd;
      auto* r = b_.CreateIntrinsic(id, {lt},
                                   {b_.CreateBitCast(a, lt),
                                    b_.CreateBitCast(c, lt)});
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_SWIZZLE: {
      // INT32/FLOAT32 word swizzle within one vector: dest[k] = src[(mask>>2k)&3]
      // (32-bit lanes aren't byte-swap-remapped, VEC128_D(n)=n) -> a constant
      // shufflevector. The mask is the src2 offset; identity is folded to Assign
      // by the builder so it never reaches here.
      auto* v = V(i->src1.value);
      if (!v) return false;
      TypeName pt = static_cast<TypeName>(i->flags);
      if (pt != INT32_TYPE && pt != FLOAT32_TYPE) return false;
      uint32_t mask = static_cast<uint32_t>(i->src2.offset);
      auto* lt = LaneVecTy(INT32_TYPE);
      int m[4] = {int(mask & 3), int((mask >> 2) & 3), int((mask >> 4) & 3),
                  int((mask >> 6) & 3)};
      auto* r = b_.CreateShuffleVector(b_.CreateBitCast(v, lt),
                                       llvm::PoisonValue::get(lt),
                                       llvm::ArrayRef<int>(m, 4));
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_PERMUTE: {
      auto* s2 = V(i->src2.value);
      auto* s3 = V(i->src3.value);
      if (!s2 || !s3) return false;
      if (i->src1.value->type == INT32_TYPE) {
        // I32-control word permute (vmrghw/vmrglw): each control byte k selects a
        // dword (sel&3) from src2 (bit2=0) or src3 (bit2=1) -> 2-input shuffle.
        if (!i->src1.value->IsConstant()) return false;
        uint32_t ctrl = i->src1.value->constant.i32;
        auto* lt = LaneVecTy(INT32_TYPE);
        int m[4];
        for (int idx = 0; idx < 4; idx++) {
          uint8_t sel = (ctrl >> (idx * 8)) & 0xFF;
          m[idx] = int(sel & 3) + (((sel >> 2) & 1) ? 4 : 0);
        }
        Def(i->dest, b_.CreateBitCast(
                         b_.CreateShuffleVector(b_.CreateBitCast(s2, lt),
                                                b_.CreateBitCast(s3, lt),
                                                llvm::ArrayRef<int>(m, 4)),
                         T(VEC128_TYPE)));
        return true;
      }
      // V128-control byte/halfword permute across {src2, src3}.
      auto* i8x16 = LaneVecTy(INT8_TYPE);
      auto* a = b_.CreateBitCast(s2, i8x16);
      auto* bb = b_.CreateBitCast(s3, i8x16);
      TypeName pt = static_cast<TypeName>(i->flags);
      if (pt == INT8_TYPE) {
        // remap = (control ^ 3) & 0x1F (PPC byte index -> LE, 5-bit table range)
        // then a 2-table TBL (tbl2). Works for dynamic AND constant control.
        auto* c = V(i->src1.value);
        if (!c) return false;
        auto* remap = b_.CreateAnd(
            b_.CreateXor(b_.CreateBitCast(c, i8x16),
                         llvm::ConstantInt::get(i8x16, 3)),
            llvm::ConstantInt::get(i8x16, 0x1F));
        Def(i->dest,
            b_.CreateBitCast(
                b_.CreateIntrinsic(llvm::Intrinsic::aarch64_neon_tbl2, {i8x16},
                                   {a, bb, remap}),
                T(VEC128_TYPE)));
        return true;
      }
      if (pt == INT16_TYPE) {
        // Constant halfword control -> precomputed byte-level shuffle mask: PPC
        // halfword H maps to NEON u16 (H&7)^1, +16 bytes if H>=8 (from src3).
        if (!i->src1.value->IsConstant()) return false;
        auto& cv = i->src1.value->constant.v128;
        int m[16];
        for (int k = 0; k < 8; k++) {
          uint16_t h = cv.u16[k] & 0xF;
          int base = (h >= 8) ? 16 : 0;
          int neon_hw = (h & 7) ^ 1;
          m[2 * k] = base + 2 * neon_hw;
          m[2 * k + 1] = base + 2 * neon_hw + 1;
        }
        Def(i->dest,
            b_.CreateBitCast(
                b_.CreateShuffleVector(a, bb, llvm::ArrayRef<int>(m, 16)),
                T(VEC128_TYPE)));
        return true;
      }
      return false;
    }
    case OPCODE_ROUND: {
      // Round-to-integral, scalar f32/f64 or V128 f32x4. Mode from flags maps to
      // the frint variant: zero=trunc, nearest=roundeven (round-half-to-EVEN, not
      // llvm.round), -inf=floor, +inf=ceil, dynamic->nearest (baked-in mode).
      auto* a = V(i->src1.value);
      if (!a) return false;
      llvm::Intrinsic::ID id;
      switch (i->flags) {
        case ROUND_TO_ZERO: id = llvm::Intrinsic::trunc; break;
        case ROUND_TO_NEAREST: id = llvm::Intrinsic::roundeven; break;
        case ROUND_TO_MINUS_INFINITY: id = llvm::Intrinsic::floor; break;
        case ROUND_TO_POSITIVE_INFINITY: id = llvm::Intrinsic::ceil; break;
        default: id = llvm::Intrinsic::roundeven; break;
      }
      if (IsVec(a)) {
        auto* f32x4 = LaneVecTy(FLOAT32_TYPE);
        Def(i->dest, b_.CreateBitCast(
                         b_.CreateUnaryIntrinsic(id, b_.CreateBitCast(a, f32x4)),
                         T(VEC128_TYPE)));
      } else {
        if (!a->getType()->isFloatingPointTy()) return false;
        Def(i->dest, b_.CreateUnaryIntrinsic(id, a));
      }
      return true;
    }
    case OPCODE_RECIP: {
      // vrefp / fres: FULL-precision 1.0/x (xenia implements the estimate as a
      // real divide). Scalar (FPU mode) = no flush; V128 (VMX) = denormal flush
      // in + out. Plain fdiv NaN (no PPC NaN fixup).
      auto* a = V(i->src1.value);
      if (!a) return false;
      if (IsVec(a)) {
        auto* f32x4 = LaneVecTy(FLOAT32_TYPE);
        auto* xi = VmxFlushDenorm(b_.CreateBitCast(a, T(VEC128_TYPE)));
        auto* r = b_.CreateFDiv(llvm::ConstantFP::get(f32x4, 1.0),
                                b_.CreateBitCast(xi, f32x4));
        Def(i->dest, VmxFlushDenorm(b_.CreateBitCast(r, T(VEC128_TYPE))));
      } else {
        if (!a->getType()->isFloatingPointTy()) return false;
        Def(i->dest,
            b_.CreateFDiv(llvm::ConstantFP::get(a->getType(), 1.0), a));
      }
      return true;
    }
    case OPCODE_RSQRT: {
      // Scalar f32: full-precision 1/sqrt(x) (a64 fsqrt+fdiv). F64 (frsqrte) and
      // V128 (vrsqrtefp) use the 360 lookup-table estimate via runtime helpers.
      auto* a = V(i->src1.value);
      if (!a) return false;
      if (a->getType()->isFloatTy()) {
        Def(i->dest,
            b_.CreateFDiv(llvm::ConstantFP::get(a->getType(), 1.0),
                          b_.CreateUnaryIntrinsic(llvm::Intrinsic::sqrt, a)));
        return true;
      }
      if (a->getType()->isDoubleTy()) {
        auto* i64 = b_.getInt64Ty();
        auto callee = mod_->getOrInsertFunction(
            "xe_llvm_frsqrte", llvm::FunctionType::get(i64, {i64}, false));
        auto* res = b_.CreateCall(callee, {b_.CreateBitCast(a, i64)});
        Def(i->dest, b_.CreateBitCast(res, b_.getDoubleTy()));
        return true;
      }
      if (IsVec(a)) {
        Def(i->dest, EmitVecLaneCall("xe_llvm_vrsqrte_lane", a));
        return true;
      }
      return false;
    }
    case OPCODE_LOG2: {
      // vlogefp: per-lane log2(float) via libm helper (V128 only).
      auto* a = V(i->src1.value);
      if (!a || !IsVec(a)) return false;
      Def(i->dest, EmitVecLaneCall("xe_llvm_log2_lane", a));
      return true;
    }
    case OPCODE_POW2: {
      // vexptefp: per-lane exp2(float) via libm helper (V128 only).
      auto* a = V(i->src1.value);
      if (!a || !IsVec(a)) return false;
      Def(i->dest, EmitVecLaneCall("xe_llvm_exp2_lane", a));
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

  // Serialize ALL LLVM compilation across guest threads. Each guest thread has
  // its own LLVMAssembler, but they share ONE LLJIT; BD starts several guest
  // threads (Main XThread, MainThread, ...) that hit uncompiled functions at the
  // same time, driving the LLJIT's codegen (AsmPrinter/MCAssembler) concurrently
  // -> heap corruption -> non-deterministic SIGBUS deep inside libLLVM
  // (MCAssembler::computeFragmentSize, device-pinned) or a bogus-ctx storm.
  // The lock is acquired AFTER xenia's compile/global lock (consistent order, no
  // deadlock) and released before the JIT'd code ever runs (helpers run lock-
  // free at runtime). Correctness-first; per-fn compile is one-time.
  static std::mutex s_llvm_compile_mutex;
  std::lock_guard<std::mutex> compile_guard(s_llvm_compile_mutex);

  auto ctx_owner = std::make_unique<llvm::LLVMContext>();
  auto& ctx = *ctx_owner;
  auto mod = std::make_unique<llvm::Module>("guest", ctx);
  mod->setDataLayout(jit.getDataLayout());
  mod->setTargetTriple(jit.getTargetTriple().str());

  std::string name = "guest_" + std::to_string(function->address());
  // void guest_<addr>(i64 x0_guest_return_address): matches the a64 ABI - the
  // host->guest thunk passes the guest return address in x0, and it's used to
  // recognize a guest `blr` RETURN in CALL_INDIRECT.
  auto* fn_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
                                        {llvm::Type::getInt64Ty(ctx)}, false);
  auto* fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, name,
                                    mod.get());
  // Reserve x20 (guest ctx) / x21 (membase) for this function's codegen so the
  // allocator never clobbers them (set by the host->guest thunk, read via
  // @llvm.read_register). Per-function attribute, not the JTMB (which hangs
  // create() under qemu).
  // ALSO disable SVE/SVE2/SME: detectHost enables SVE2 on the Cortex-X3, but it
  // is NOT usable on the Thor's Android (executing an SVE instruction faults
  // SIGILL) - DEVICE-CONFIRMED 2026-06-27: llvm.memset codegen emitted an SVE
  // instruction (insn=0x04A84D02, code=2 SIGILL storm). Forcing -sve keeps all
  // codegen on NEON/scalar (NEON = +fp-armv8/+neon, unaffected), the only SIMD
  // the device supports. This is what unblocks MEMSET (and any future op LLVM
  // would otherwise vectorize via SVE) instead of falling back to a64.
  fn->addFnAttr("target-features",
                "+reserve-x20,+reserve-x21,-sve,-sve2,-sve2-bitperm,-sme,-sme2");

  Lowerer lowerer(ctx, mod.get(), fn, function->address());
  if (!lowerer.Run(builder)) {
    return false;  // unsupported opcode -> caller falls back to a64
  }

  {
    // Capture the verifier's reason (errs() isn't in logcat) so invalid-IR
    // codegen bugs are diagnosable instead of a silent a64 fallback.
    std::string verr;
    llvm::raw_string_ostream vos(verr);
    if (llvm::verifyFunction(*fn, &vos)) {
      XELOGE("LLVMAssembler: verifyFunction failed for {}: {}", name, verr);
      return false;
    }
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

  if (cvars::cpu_backend_llvm_dump_ir) {
    // Post-opt IR, module still owned locally (before addIRModule moves it).
    // Per-line so logcat doesn't truncate. Read off codegen bugs device-free.
    std::string ir;
    llvm::raw_string_ostream os(ir);
    fn->print(os);
    size_t pos = 0;
    while (pos < ir.size()) {
      size_t nl = ir.find('\n', pos);
      if (nl == std::string::npos) nl = ir.size();
      XELOGI("LLVMir {}: {}", name, ir.substr(pos, nl - pos));
      pos = nl + 1;
    }
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
  // Host<->guest map for localizing on-device faults in JIT'd code: grep
  // "LLVMmap" and find the entry whose host addr is the greatest <= a faulting
  // pc. (Use `adb logcat -G 16M` so early entries don't wrap before the fault.)
  XELOGI("LLVMmap guest=0x{:08X} host=0x{:016X}", function->address(),
         reinterpret_cast<uint64_t>(code));
  return true;
}

#else  // !XE_LLVM_BACKEND_ENABLED

bool LLVMAssembler::LowerAndJit(GuestFunction*, HIRBuilder*) { return false; }

#endif  // XE_LLVM_BACKEND_ENABLED

bool LLVMAssembler::Assemble(GuestFunction* function, hir::HIRBuilder* builder,
                             uint32_t debug_info_flags,
                             std::unique_ptr<FunctionDebugInfo> debug_info) {
#if XE_LLVM_BACKEND_ENABLED
  // Range gate (bisection): only LLVM-compile functions in [range_lo, range_hi);
  // the rest use a64. Used to localize which function's LLVM codegen corrupts
  // state. Empty (default) bounds = no restriction = compile everything.
  uint32_t addr = function->address();
  const std::string& lo_s = cvars::cpu_backend_llvm_range_lo;
  const std::string& hi_s = cvars::cpu_backend_llvm_range_hi;
  uint32_t lo = lo_s.empty() ? 0 : uint32_t(std::strtoull(lo_s.c_str(), nullptr, 16));
  uint32_t hi = hi_s.empty() ? 0 : uint32_t(std::strtoull(hi_s.c_str(), nullptr, 16));
  bool in_range = addr >= lo && (hi == 0 || addr < hi);
  // Count gate (monotonic bisection): LLVM-compile at most the first N functions
  // (in compile order). The counter increments only on SUCCESSFUL LLVM compiles,
  // so Set(N) is a strict subset of Set(N+1) -> binary-searching N pins the fn
  // whose LLVM codegen breaks boot. LLVMseq logs seq->addr for the map.
  static std::atomic<int32_t> s_llvm_seq{0};
  int32_t max_fns = cvars::cpu_backend_llvm_max_fns;
  bool under_cap = (max_fns <= 0) || (s_llvm_seq.load(std::memory_order_relaxed) <
                                      max_fns);
  if (in_range && under_cap && LowerAndJit(function, builder)) {
    int32_t seq = s_llvm_seq.fetch_add(1, std::memory_order_relaxed);
    XELOGI("LLVMseq {} guest=0x{:08X}", seq, addr);
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
