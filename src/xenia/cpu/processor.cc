/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/processor.h"

#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_set>

#include "xenia/base/assert.h"
#include "xenia/base/atomic.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/byte_stream.h"
#include "xenia/base/cvar.h"
#include "xenia/base/debugging.h"
#include "xenia/base/exception_handler.h"
#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/platform.h"
#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/breakpoint.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/module.h"
#include "xenia/cpu/ppc/ppc_decode_data.h"
#include "xenia/cpu/ppc/ppc_frontend.h"
#include "xenia/cpu/stack_walker.h"
#include "xenia/cpu/thread.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/cpu/xex_module.h"
#include "xenia/emulator.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/kernel/kernel_state.h"

#if 0 && DEBUG
#define DEFAULT_DEBUG_FLAG true
#else
#define DEFAULT_DEBUG_FLAG false
#endif

DEFINE_bool(debug, DEFAULT_DEBUG_FLAG,
            "Allow debugging and retain debug information.", "General");
DEFINE_path(trace_function_data_path, "", "File to write trace data to.",
            "CPU");
DEFINE_bool(break_on_start, false, "Break into the debugger on startup.",
            "CPU");
// GPU D3D9-HLE / general HLE foundation: comma-separated hex guest addresses
// (e.g. "82487980,823075d0") to REPLACE with a host extern handler instead of
// executing the guest code. The reusable mechanism every HLE'd XDK D3D9 function
// needs - proves interception (logs args r3-r5 + LR) and is the seam where the
// host D3D9->Vulkan handlers get planted. Default empty = no interception.
DEFINE_string(cpu_hle_intercept_addrs, "",
              "HLE: comma-separated hex guest addresses to replace with a host "
              "handler (the load-time D3D9-HLE mechanism). Logs each call; the "
              "guest body is skipped. Empty disables.",
              "CPU");
DEFINE_uint32(cpu_watch_guest_write_page, 0,
              "PAGE-WATCH (diagnostic): hex base of a guest page to host-protect "
              "read-only; the a64 ExceptionCallback logs the guest FUNCTION that "
              "writes it (find BD's BeginTiling that writes tile-state 0x40011330). "
              "Requires the walker intercept to trigger the protect. 0 = off.",
              "CPU");
DEFINE_string(cpu_hle_binonce_addr, "",
              "GPU D3D9-HLE BIN-ONCE: hex guest addr of BD's tile-binning walker "
              "(82487878). Host handler forces ONE full-surface tile + all-visible "
              "bin_select masks so the render driver replays the scene ONCE. Empty "
              "disables.",
              "CPU");
DEFINE_string(cpu_hle_tiling_replay_addr, "",
              "GPU D3D9-HLE: hex guest addr of BD's deferred-D3D-command tiling "
              "REPLAY fn (82487cc8). Host reimplements its token loop, emitting PM4 "
              "via the ring writer + forcing tile count=1 = bin-once at the source. "
              "Empty disables. (WIP - the coherent pitch/base/resolve rewrite for "
              "full-surface is the next increment.)",
              "CPU");
DEFINE_string(cpu_hle_ring_writer_addr, "",
              "GPU D3D9-HLE: hex guest addr of BD's XDK D3D9 ring writer (8246E100). "
              "HLE-replaces it with a pure-C++ ring write (no reentrant guest call): "
              "injects the {C0013F00, pkt1, pkt0} PM4 words straight into xenia's "
              "primary ring buffer + kicks the CP. The replay + tiling helpers run "
              "NATIVELY and call this at the leaf. This is the coherent hook for the "
              "RB_SURFACE_INFO pitch rewrite (bin-once). Empty disables.",
              "CPU");

namespace xe {
namespace kernel {
class XThread;
}  // namespace kernel

namespace cpu {

using xe::cpu::ppc::PPCOpcode;
using xe::kernel::XThread;

using namespace xe::literals;

class BuiltinModule : public Module {
 public:
  explicit BuiltinModule(Processor* processor)
      : Module(processor), name_("builtin") {}

  const std::string& name() const override { return name_; }
  bool is_executable() const override { return false; }

  bool ContainsAddress(uint32_t address) override {
    return (address & 0xFFFFFFF0) == 0xFFFFFFF0;
  }

 protected:
  std::unique_ptr<Function> CreateFunction(uint32_t address) override {
    return std::unique_ptr<Function>(new BuiltinFunction(this, address));
  }

 private:
  std::string name_;
};

Processor::Processor(xe::Memory* memory, ExportResolver* export_resolver)
    : memory_(memory), export_resolver_(export_resolver) {}

Processor::~Processor() {
  {
    auto global_lock = global_critical_region_.Acquire();
    modules_.clear();
  }

  frontend_.reset();
  backend_.reset();

  if (functions_trace_file_) {
    functions_trace_file_->Flush();
    functions_trace_file_.reset();
  }
}

bool Processor::Setup(std::unique_ptr<backend::Backend> backend) {
  // TODO(benvanik): query mode from debugger?
  debug_info_flags_ = 0;

  auto frontend = std::make_unique<ppc::PPCFrontend>(this);
  // TODO(benvanik): set options/etc.

  // Must be initialized by subclass before calling into this.
  assert_not_null(memory_);

  std::unique_ptr<Module> builtin_module(new BuiltinModule(this));
  builtin_module_ = builtin_module.get();
  modules_.push_back(std::move(builtin_module));

  if (frontend_ || backend_) {
    return false;
  }

  if (!backend) {
    return false;
  }
  if (!backend->Initialize(this)) {
    return false;
  }
  if (!frontend->Initialize()) {
    return false;
  }

  backend_ = std::move(backend);
  frontend_ = std::move(frontend);

  // Stack walker is used when profiling, debugging, and dumping.
  // Note that creation may fail, in which case we'll have to disable those
  // features.
  // The code cache may be unavailable in case of a "null" backend.
  cpu::backend::CodeCache* code_cache = backend_->code_cache();
  if (code_cache) {
    stack_walker_ = StackWalker::Create(code_cache);
  }
  if (!stack_walker_) {
    // TODO(benvanik): disable features.
    if (cvars::debug) {
      XELOGW("Disabling --debug due to lack of stack walker");
      cvars::debug = false;
    }
  }

  // Open the trace data path, if requested.
  functions_trace_path_ = cvars::trace_function_data_path;
  if (!functions_trace_path_.empty()) {
    functions_trace_file_ =
        ChunkedMappedMemoryWriter::Open(functions_trace_path_, 32_MiB, true);
  }

  return true;
}

void Processor::PreLaunch() {
  if (cvars::break_on_start) {
    // Start paused.
    XELOGI("Breaking into debugger because of --break_on_start...");
    execution_state_ = ExecutionState::kRunning;
    Pause();
  } else {
    // Start running.
    execution_state_ = ExecutionState::kRunning;
  }
}

bool Processor::AddModule(std::unique_ptr<Module> module) {
  auto global_lock = global_critical_region_.Acquire();
  modules_.push_back(std::move(module));
  return true;
}

Module* Processor::GetModule(const std::string_view name) {
  auto global_lock = global_critical_region_.Acquire();
  for (const auto& module : modules_) {
    if (module->name() == name) {
      return module.get();
    }
  }
  return nullptr;
}

std::vector<Module*> Processor::GetModules() {
  auto global_lock = global_critical_region_.Acquire();
  std::vector<Module*> clone(modules_.size());
  for (const auto& module : modules_) {
    clone.push_back(module.get());
  }
  return clone;
}

Function* Processor::DefineBuiltin(const std::string_view name,
                                   BuiltinFunction::Handler handler, void* arg0,
                                   void* arg1) {
  uint32_t address = next_builtin_address_;
  next_builtin_address_ += 4;

  Function* function;
  builtin_module_->DeclareFunction(address, &function);
  function->set_end_address(address + 4);
  function->set_name(name);

  auto builtin_function = static_cast<BuiltinFunction*>(function);
  builtin_function->SetupBuiltin(handler, arg0, arg1);

  function->set_status(Symbol::Status::kDeclared);
  return function;
}

Function* Processor::QueryFunction(uint32_t address) {
  auto entry = entry_table_.Get(address);
  if (!entry) {
    return nullptr;
  }
  return entry->function;
}

std::vector<Function*> Processor::FindFunctionsWithAddress(uint32_t address) {
  return entry_table_.FindWithAddress(address);
}

Function* Processor::ResolveFunction(uint32_t address) {
  Entry* entry;
  Entry::Status status = entry_table_.GetOrCreate(address, &entry);
  if (status == Entry::STATUS_NEW) {
    // Needs to be generated. We have the 'lock' on it and must do so now.

    // Grab symbol declaration.
    auto function = LookupFunction(address);
    if (!function) {
      entry->status = Entry::STATUS_FAILED;
      return nullptr;
    }

    if (!DemandFunction(function)) {
      entry->status = Entry::STATUS_FAILED;
      return nullptr;
    }
    entry->function = function;
    entry->end_address = function->end_address();
    status = entry->status = Entry::STATUS_READY;
  }
  if (status == Entry::STATUS_READY) {
    // Ready to use.
    return entry->function;
  } else {
    // Failed or bad state.
    return nullptr;
  }
}

Function* Processor::LookupFunction(uint32_t address) {
  // TODO(benvanik): fast reject invalid addresses/log errors.

  // Find the module that contains the address.
  Module* code_module = nullptr;
  {
    auto global_lock = global_critical_region_.Acquire();
    // TODO(benvanik): sort by code address (if contiguous) so can bsearch.
    // TODO(benvanik): cache last module low/high, as likely to be in there.
    for (const auto& module : modules_) {
      if (module->ContainsAddress(address)) {
        code_module = module.get();
        break;
      }
    }
  }
  if (!code_module) {
    // No module found that could contain the address.
    return nullptr;
  }

  return LookupFunction(code_module, address);
}

namespace {
// GPU D3D9-HLE foundation: the set of guest addresses to replace with a host
// handler, parsed once from cpu_hle_intercept_addrs.
std::unordered_set<uint32_t> g_hle_intercept_addrs;
std::once_flag g_hle_intercept_once;
void ParseHleInterceptAddrs() {
  const std::string& s = cvars::cpu_hle_intercept_addrs;
  size_t i = 0;
  while (i < s.size()) {
    size_t comma = s.find(',', i);
    std::string tok =
        s.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
    // Trim spaces.
    while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
    while (!tok.empty() && tok.back() == ' ') tok.pop_back();
    if (!tok.empty()) {
      uint32_t v = uint32_t(strtoul(tok.c_str(), nullptr, 16));
      if (v) {
        g_hle_intercept_addrs.insert(v);
        XELOGI("HLE: intercept address registered: {:08X}", v);
      }
    }
    if (comma == std::string::npos) break;
    i = comma + 1;
  }
}
// Generic HLE intercept handler: logs the call (guest args + caller LR) and
// returns 0. Per-address host D3D9->Vulkan handlers replace this as they are
// authored; for now it proves the interception seam on a real D3D9 function.
void HleInterceptHandler(ppc::PPCContext* ppc_context,
                         kernel::KernelState* kernel_state) {
  // ⭐ D3D-HLE BIN-ONCE (tile-binning walker 0x82487878): r3 = tile-state struct
  // (count at +4, tile rects at +0xC, 0x10 stride, 4 words each). Device-decoded
  // live: count=2; rect0=[0,0x2A0,0x2D0,0x260]=[_,h672,w720,yoff608] (partial
  // tile), rect1=[0,0x500,0x2D0,0]=[_,h1280,w720,0] = the FULL surface. The host
  // has no 10MB EDRAM limit, so collapse BD's 2 predicated tiles into ONE full-
  // surface tile: copy rect1 -> rect0, set count=1. Now the guest bins + replays
  // ALL draws into ONE 720x1280 pass = the bin-once by construction (bypasses the
  // ~2x predicated-tiling cost). Then run the original binning (fall through -
  // NOT noop; noop rendered only half at 29.6fps). Cvar-gated via the intercept.
  uint8_t* base = ppc_context->virtual_membase;
  uint32_t state = uint32_t(ppc_context->r[3]);
  // PAGE-WATCH mode: if a watched page is set, protect it ONCE (read-only) so the
  // NEXT guest write to it faults + the a64 ExceptionCallback logs the writer fn
  // (= BeginTiling). Skip the bin-once reshape/marking so our OWN host writes to
  // the page don't fault. The walker's r3 confirms the live tile-state address.
  if (cvars::cpu_watch_guest_write_page && base) {
    // Protect ONCE - the a64 ExceptionCallback EMULATES each store (does the write
    // itself + keeps the page protected), so the page stays watched forever with
    // no un-protect windows = every tile-state write is caught. Any BeginTiling
    // re-write of the rects (0x40011330-48) is logged with its guest fn.
    static std::atomic<bool> pw_protected{false};
    bool expected = false;
    if (pw_protected.compare_exchange_strong(expected, true)) {
      size_t ps = xe::memory::page_size();
      uint32_t page = cvars::cpu_watch_guest_write_page & ~uint32_t(ps - 1);
      xe::memory::Protect(base + page, ps, xe::memory::PageAccess::kReadOnly);
      XELOGI("PAGE_WATCH: protected {:08X} read-only, emulate-on-fault (r3={:08X})",
             page, state);
    }
    ppc_context->r[3] = 0;
    return;
  }
  static std::atomic<int> reshape_log{0};
  if (base && state) {
    uint32_t count = xe::load_and_swap<uint32_t>(base + state + 4);
    // Sanity: only reshape a plausible tile table (2..8 tiles) so a stray r3 to
    // uninitialized data is left alone.
    if (count >= 2 && count <= 8) {
      for (int i = 0; i < 4; ++i) {  // rect1 (full surface) -> rect0
        uint32_t v = xe::load_and_swap<uint32_t>(base + state + 0x1C + i * 4);
        xe::store_and_swap<uint32_t>(base + state + 0xC + i * 4, v);
      }
      xe::store_and_swap<uint32_t>(base + state + 4, 1);  // count = 1 tile
      if (reshape_log++ < 3) {
        XELOGI("HLE BIN-ONCE: reshaped {} tiles -> 1 full-surface (state={:08X})",
               count, state);
      }
    }
  }
  // Replicate the walker's per-primitive binning for the single full tile: for
  // THIS draw (r4=param_2: end-ptr at [0], primitives from +4 stride 0x10, each
  // entry's [0] pointing at the prim whose [+8] is the tile mask), mark every
  // primitive as touching tile 0 (uVar5=3 for 1 tile | 0x80000000 processed).
  // This is what the guest walker does; doing it here makes ALL draws render in
  // the ONE full-surface pass = full-scene bin-once (vs the half-render noop).
  uint32_t draw = uint32_t(ppc_context->r[4]);
  if (base && draw && draw < 0xF0000000u) {
    uint32_t end = xe::load_and_swap<uint32_t>(base + draw);
    uint32_t p = draw + 4;
    for (int guard = 0; p < end && guard < 8192; ++guard) {
      uint32_t prim = xe::load_and_swap<uint32_t>(base + p);
      if (prim && prim < 0xF0000000u) {
        // Mark ALL bins (0xFFFFFFFF) so the draw passes WHATEVER SET_BIN_SELECT
        // value BD sets for the single tile (selects vary: 000C/80000003/FFFF..
        // - a mismatched mask skips the draw = the missing right half).
        xe::store_and_swap<uint32_t>(base + prim + 8, 0xFFFFFFFFu);
      }
      p += 0x10;
    }
  }
  // One-shot guest-stack scan to recover the tiling call chain up toward
  // EndTiling/BeginTiling (the clean full-surface HLE target). Prior scan used a
  // WRONG mask (0xFFF00000 only matched 0x820xxxxx; the callers are ~0x8248xxxx =
  // masked 0x82400000). Correct mask 0xFF000000 matches all 0x82xxxxxx guest code.
  static std::atomic<int> chain_log{0};
  if (base && chain_log++ < 2) {
    uint32_t sp = uint32_t(ppc_context->r[1]);
    for (int f = 0; f < 16 && sp; ++f) {
      uint32_t back = xe::load_and_swap<uint32_t>(base + sp);
      if (back <= sp || back >= 0xF0000000u) break;
      int hits = 0;
      for (uint32_t a = sp; a + 4 <= back && hits < 4; a += 4) {
        uint32_t v = xe::load_and_swap<uint32_t>(base + a);
        if ((v & 0xFF000000u) == 0x82000000u && v != 0x82000000u) {
          XELOGI("HLE_CHAIN[{}] +{:03X}: ret={:08X}", f, a - sp, v);
          ++hits;
        }
      }
      sp = back;
    }
  }
  ppc_context->r[3] = 0;
}
bool IsHleIntercept(uint32_t address) {
  if (cvars::cpu_hle_intercept_addrs.empty()) {
    return false;
  }
  std::call_once(g_hle_intercept_once, ParseHleInterceptAddrs);
  return g_hle_intercept_addrs.count(address) != 0;
}

uint32_t g_hle_binonce_addr = 0;
std::once_flag g_hle_binonce_once;
void ParseHleBinOnceAddr() {
  if (!cvars::cpu_hle_binonce_addr.empty()) {
    g_hle_binonce_addr =
        uint32_t(strtoul(cvars::cpu_hle_binonce_addr.c_str(), nullptr, 16));
  }
}
bool IsHleBinOnce(uint32_t address) {
  if (cvars::cpu_hle_binonce_addr.empty()) {
    return false;
  }
  std::call_once(g_hle_binonce_once, ParseHleBinOnceAddr);
  return g_hle_binonce_addr && address == g_hle_binonce_addr;
}
// GPU D3D9-HLE BIN-ONCE handler for BD's tile-binning walker (82487878). The
// guest walker computes per-draw bin_select visibility masks across N tiles;
// this replaces it, forcing ONE full-surface tile (count=1) + all-visible masks
// (0x80000003 = tile-0 bits | valid) so the render driver replays the scene ONCE
// instead of per-tile = the ~1.4x bin-once win. Mirrors the walker's stream walk
// (r3=state table: count@+4; r4=cmd stream: segend=*(p), entries at p+4 stride
// 0x10, each entry's first word -> mask slot at +8, segment chain to 0xC0000000).
void HleBinOnceHandler(ppc::PPCContext* ctx, kernel::KernelState*) {
  Memory* mem = ctx->processor->memory();
  uint32_t state = uint32_t(ctx->r[3]);
  uint32_t stream = uint32_t(ctx->r[4]);
  // Force one tile + expand tile 0's rect to the FULL surface so the single pass
  // renders the whole scene (not just the left tile). Tile-0 rect (per the walker
  // bbox test) = 4 uints at state+0x8(left) +0xC(top) +0x10(right) +0x14(bottom).
  if (state) {
    xe::store_and_swap<uint32_t>(mem->TranslateVirtual(state + 4), 1u);
    xe::store_and_swap<uint32_t>(mem->TranslateVirtual(state + 0x8), 0u);
    xe::store_and_swap<uint32_t>(mem->TranslateVirtual(state + 0xC), 0u);
    xe::store_and_swap<uint32_t>(mem->TranslateVirtual(state + 0x10), 0x7FFFFFFFu);
    xe::store_and_swap<uint32_t>(mem->TranslateVirtual(state + 0x14), 0x7FFFFFFFu);
  }
  // Walk the command stream, marking every draw visible in tile 0. Heavily
  // guarded (bounds + iteration cap) so a format mismatch degrades to a wrong
  // render, not a crash.
  uint32_t p = stream;
  int guard = 0;
  while (p && p != 0xC0000000u && guard < 300000) {
    ++guard;
    uint32_t segend = xe::load_and_swap<uint32_t>(mem->TranslateVirtual(p));
    if (segend <= p || segend - p > 0x100000u) {
      break;  // implausible segment
    }
    uint32_t e = p + 4;
    uint32_t last = e;
    while (e < segend && guard < 300000) {
      ++guard;
      uint32_t entry_ptr = xe::load_and_swap<uint32_t>(mem->TranslateVirtual(e));
      if (entry_ptr >= 0x1000u && entry_ptr < 0x40000000u) {
        xe::store_and_swap<uint32_t>(mem->TranslateVirtual(entry_ptr + 8),
                                     0x80000003u);
      }
      last = e;
      e += 0x10;
    }
    p = xe::load_and_swap<uint32_t>(mem->TranslateVirtual(last));
  }
  ctx->r[3] = 0;
}

// --- HLE tiling REPLAY (D3D BeginTiling/EndTiling = FUN_82487cc8 @0x82487cc8) ---
std::atomic<uint32_t> g_hle_tiling_replay_addr{0};
std::once_flag g_hle_tiling_replay_once;
void ParseHleTilingReplayAddr() {
  if (!cvars::cpu_hle_tiling_replay_addr.empty()) {
    g_hle_tiling_replay_addr = uint32_t(
        strtoul(cvars::cpu_hle_tiling_replay_addr.c_str(), nullptr, 16));
  }
}
bool IsHleTilingReplay(uint32_t address) {
  if (cvars::cpu_hle_tiling_replay_addr.empty()) {
    return false;
  }
  std::call_once(g_hle_tiling_replay_once, ParseHleTilingReplayAddr);
  return g_hle_tiling_replay_addr && address == g_hle_tiling_replay_addr;
}
// Host reimplementation of BD's deferred-D3D-command tiling REPLAY FUN_82487cc8:
// r3=recorded command stream, r4=ctx (cmd-list context; +0x74=tile-state struct,
// +0x78=TILE COUNT, +0x2c=tile idx, +0x28=predication flags, +0x16c=bin mask,
// +0x170/174/178=saved stream ptrs). Replays the ~13-opcode token loop, calling
// the guest ring writer (0x8246E100) + tiling helpers via Processor::Execute
// (reentrant; only ~dozens of PM4 setup emits/frame - the DRAWS go via the driver/
// walker, not here). Forces TILE COUNT=1 on the 0x80 tile-table token = bin-once at
// the SOURCE. This HLE-REPLACES the D3D tiling function (goal build-order step 3).
// GATED via cpu_hle_tiling_replay_addr. WIP: count=1 alone = half (pitch stays 360);
// the coherent RB_SURFACE_INFO pitch/base/resolve rewrite is the next increment.
void HleTilingReplayHandler(ppc::PPCContext* ctx,
                            kernel::KernelState* /*kernel_state*/) {
  auto* proc = ctx->processor;
  Memory* mem = proc->memory();
  auto* ts = ThreadState::Get();
  uint32_t stream = uint32_t(ctx->r[3]);
  uint32_t cx = uint32_t(ctx->r[4]);
  if (!ts || !stream || !cx) {
    ctx->r[3] = stream;
    return;
  }
  uint32_t device =
      xe::load_and_swap<uint32_t>(mem->TranslateVirtual(0x820005F4u));
  uint32_t scratch = uint32_t(ctx->r[1]) - 0x400u;
  auto rd = [&](uint32_t a) {
    return xe::load_and_swap<uint32_t>(mem->TranslateVirtual(a));
  };
  auto wr = [&](uint32_t a, uint32_t v) {
    xe::store_and_swap<uint32_t>(mem->TranslateVirtual(a), v);
  };
  auto call = [&](uint32_t fn, uint32_t a3, uint32_t a4, uint32_t a5, size_t n) {
    uint64_t args[3] = {a3, a4, a5};
    proc->Execute(ts, fn, args, n);
  };
  static std::atomic<int> pertok_log{0};
  int guard = 0;
  uint32_t token = rd(stream);
  while ((token & 0x80000000u) != 0 && ++guard < 200000) {
    uint32_t t = token & 0xff000000u;
    if (pertok_log < 60) {
      pertok_log++;
      XELOGI("HLE TR-STEP i={} stream={:08X} token={:08X}", guard, stream, token);
    }
    if (t == 0x80000000u) {
      call(0x826BF770u, cx + 0x74u, stream + 4u, 0xf8u, 3);  // memcpy tile table
      wr(cx + 0x2cu, 0);
      wr(cx + 0x16cu, 0x7fffffffu);
      wr(cx + 0x78u, 1u);  // ⭐ BIN-ONCE: tile count = 1
      stream += 0x3fu * 4u;
    } else if (t == 0x81000000u) {
      break;
    } else if (t == 0x82000000u) {
      wr(scratch, token & 0xffffffu);
      wr(scratch + 4u, rd(stream + 4u));
      call(0x8246E100u, device, scratch, 1u, 3);  // emit PM4 word to ring
      stream += 8u;
    } else if (t == 0x83000000u) {
      stream += 4u;
    } else if (t == 0x84000000u) {
      uint32_t u = rd(cx + 0x28u);
      wr(cx + 0x170u, stream);
      wr(cx + 0x28u, u | 0x40000000u);
      if (rd(cx + 0x2cu) != 0 && (rd(cx + 0x74u) & 2u) != 0) {
        wr(cx + 0x28u, u | 0xc0000000u);
      }
      call(0x82487B38u, cx, 0, 0, 1);
      stream += 4u;
    } else if (t == 0x85000000u) {
      uint32_t u = rd(cx + 0x28u);
      wr(cx + 0x28u, u & 0xbfffffffu);
      if ((u & 0x80000000u) == 0) {
        call(0x82487B38u, cx, 0, 0, 1);
        stream += 4u;
      } else {
        stream = rd(cx + 0x174u);
      }
    } else if (t == 0x86000000u) {
      wr(cx + 0x174u, stream);
      uint32_t u28 = rd(cx + 0x28u);
      uint32_t u4 = (u28 & 0x80000000u) ? 0u : 0x20u;
      wr(cx + 0x28u, (u4 << 0x1au) | (u28 & 0x7fffffffu));
      if (u4 == 0) {
        call(0x82487B38u, cx, 0, 0, 1);
        stream += 4u;
      } else {
        stream = rd(cx + 0x170u);
      }
    } else if (t == 0x87000000u) {
      wr(cx + 0x178u, stream);
      wr(cx + 0x28u, rd(cx + 0x28u) | 0x20000000u);
      call(0x82487B38u, cx, 0, 0, 1);
      stream += 4u;
    } else if (t == 0x88000000u) {
      uint32_t u = rd(cx + 0x2cu) + 1u;
      wr(cx + 0x2cu, u);
      wr(cx + 0x28u, rd(cx + 0x28u) & 0xdfffffffu);
      if (rd(cx + 0x78u) <= u) {
        call(0x82487B38u, cx, 0, 0, 1);
        stream += 4u;
      } else {
        stream = rd(cx + 0x178u);
      }
    } else if (t == 0x89000000u) {
      call(0x824877B8u, cx, 0, 0, 1);
      stream += 4u;
    } else if (t == 0x8a000000u) {
      call(0x82487C38u, cx, 0, 0, 1);
      call(0x8273F2D4u, 0, 0, 0, 1);
      stream += 4u;
    } else if (t == 0x8b000000u) {
      if ((rd(cx + 0x16cu) & 0x80000000u) != 0) {
        call(0x82487978u, cx + 0x74u, rd(stream + 4u), rd(stream + 8u), 3);
      }
      stream += 12u;
    } else if (t == 0x8c000000u) {
      uint32_t v = rd(stream + 4u);
      stream += 8u;
      if ((rd(cx + 0x16cu) & v) != 0) {
        wr(cx + 0x50u, stream);
        stream = cx + 0x60u;
      }
    } else {
      if (token == 0xc0000000u) {
        break;
      }
      stream = token + 4u;
    }
    token = rd(stream);
  }
  static std::atomic<int> replay_log{0};
  if (replay_log++ < 8) {
    XELOGI("HLE TILING-REPLAY: iters={} final_token={:08X} final_stream={:08X}",
           guard, token, stream);
  }
  ctx->r[3] = stream;
}

// --- HLE ring writer (XDK D3D9 = FUN_8246E100 @0x8246E100) ---
std::atomic<uint32_t> g_hle_ring_writer_addr{0};
std::once_flag g_hle_ring_writer_once;
void ParseHleRingWriterAddr() {
  if (!cvars::cpu_hle_ring_writer_addr.empty()) {
    g_hle_ring_writer_addr = uint32_t(
        strtoul(cvars::cpu_hle_ring_writer_addr.c_str(), nullptr, 16));
  }
}
bool IsHleRingWriter(uint32_t address) {
  if (cvars::cpu_hle_ring_writer_addr.empty()) {
    return false;
  }
  std::call_once(g_hle_ring_writer_once, ParseHleRingWriterAddr);
  return g_hle_ring_writer_addr && address == g_hle_ring_writer_addr;
}
// Pure-C++ HLE of BD's XDK D3D9 ring writer FUN_8246E100(device, pkt[], count):
// each guest packet = 2 words {pkt0@+0, pkt1@+4}; the native fn writes 3 words
// {0xC0013F00 (PM4 TYPE3 hdr, 2 data), pkt1, pkt0} into the ring per packet, then
// kicks CP_RB_WPTR. We replicate that DIRECTLY on xenia's primary ring buffer (no
// reentrant guest call = no CP deadlock), skipping the null-guarded optional trace
// callbacks + the ring-space reserve. The replay + tiling helpers run NATIVELY and
// land here at the leaf. Hook point for the RB_SURFACE_INFO pitch rewrite (next).
void HleRingWriterHandler(ppc::PPCContext* ctx,
                          kernel::KernelState* kernel_state) {
  Memory* mem = ctx->processor->memory();
  uint32_t pkt_array = uint32_t(ctx->r[4]);
  uint32_t count = uint32_t(ctx->r[5]);
  gpu::CommandProcessor* cp = nullptr;
  if (kernel_state && kernel_state->emulator() &&
      kernel_state->emulator()->graphics_system()) {
    cp = kernel_state->emulator()->graphics_system()->command_processor();
  }
  if (!cp || !pkt_array || !count || count > 0x4000u) {
    ctx->r[3] = 0;
    return;
  }
  uint32_t ring_base = cp->primary_buffer_ptr();
  uint32_t ring_words = cp->primary_buffer_size() / 4u;
  if (!ring_base || !ring_words) {
    ctx->r[3] = 0;
    return;
  }
  uint32_t widx = cp->write_ptr_index();
  static std::atomic<int> rw_log{0};
  if (rw_log++ < 10) {
    uint32_t p0 =
        xe::load_and_swap<uint32_t>(mem->TranslateVirtual(pkt_array));
    uint32_t p1 =
        xe::load_and_swap<uint32_t>(mem->TranslateVirtual(pkt_array + 4u));
    XELOGI(
        "HLE RING-WRITER: pkts={} pkt_array={:08X} ring_base={:08X} words={} "
        "widx={} pkt0={:08X} pkt1={:08X}",
        count, pkt_array, ring_base, ring_words, widx, p0, p1);
  }
  auto ringwr = [&](uint32_t w) {
    xe::store_and_swap<uint32_t>(mem->TranslateVirtual(ring_base + widx * 4u), w);
    widx = (widx + 1u) % ring_words;
  };
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t pkt0 =
        xe::load_and_swap<uint32_t>(mem->TranslateVirtual(pkt_array + i * 8u));
    uint32_t pkt1 = xe::load_and_swap<uint32_t>(
        mem->TranslateVirtual(pkt_array + i * 8u + 4u));
    ringwr(0xc0013f00u);
    ringwr(pkt1);
    ringwr(pkt0);
  }
  cp->UpdateWritePointer(widx);
  ctx->r[3] = 0;
}
}  // namespace

Function* Processor::LookupFunction(Module* module, uint32_t address) {
  // Atomic create/lookup symbol in module.
  // If we get back the NEW flag we must declare it now.
  Function* function = nullptr;
  auto symbol_status = module->DeclareFunction(address, &function);
  if (symbol_status == Symbol::Status::kNew) {
    // Symbol is undeclared, so declare now.
    assert_true(function->is_guest());
    // GPU D3D9-HLE: establish the host extern handler at DECLARE time (runs once
    // under the atomic declare, BEFORE any multi-threaded define/harvest touches
    // it - avoids the race that crashed boot when done in DemandFunction). Same
    // model as kernel imports (extern set up early). Skip guest codegen.
    if (IsHleIntercept(address)) {
      // Intercept via the kExtern DISPATCH ONLY (SetupExtern) - GuestFunction::
      // Call sees behavior==kExtern and invokes the host handler, WITHOUT touching
      // guest code. This is the PROVEN path (fired 3229x on BD's tile walker).
      // DO NOT patch the guest code to `sc; blr`: that model only works for real
      // XDK import STUBS the frontend recognizes; patched over arbitrary function
      // code it (a) destroys the function's real bytes and (b) the guest `sc`
      // routes to the generic syscall path, NOT this fn's extern_handler_ -> BD
      // executes garbage and hangs (device-confirmed: sc;blr = black, no VdSwap;
      // SetupExtern-only = renders + fires). INDIRECT dispatch (bctr) is handled
      // in a64 ResolveFunction's kExtern-HLE branch (returns a host trampoline),
      // not by corrupting guest code here.
      static_cast<GuestFunction*>(function)->SetupExtern(HleInterceptHandler,
                                                         nullptr);
      function->set_status(Symbol::Status::kDeclared);
      XELOGI("HLE: planted host intercept handler at guest {:08X}", address);
      return function;
    }
    // GPU D3D9-HLE BIN-ONCE: replace BD's tile-binning walker with the host
    // handler that forces one full-surface tile (SetupExtern-only, same proven
    // dispatch path as the logger).
    if (IsHleBinOnce(address)) {
      static_cast<GuestFunction*>(function)->SetupExtern(HleBinOnceHandler,
                                                         nullptr);
      function->set_status(Symbol::Status::kDeclared);
      XELOGI("HLE BIN-ONCE: planted walker handler at guest {:08X}", address);
      return function;
    }
    if (IsHleTilingReplay(address)) {
      static_cast<GuestFunction*>(function)->SetupExtern(HleTilingReplayHandler,
                                                         nullptr);
      function->set_status(Symbol::Status::kDeclared);
      XELOGI("HLE TILING-REPLAY: planted host replay at guest {:08X}", address);
      return function;
    }
    if (IsHleRingWriter(address)) {
      static_cast<GuestFunction*>(function)->SetupExtern(HleRingWriterHandler,
                                                         nullptr);
      function->set_status(Symbol::Status::kDeclared);
      XELOGI("HLE RING-WRITER: planted host ring writer at guest {:08X}", address);
      return function;
    }
    if (!frontend_->DeclareFunction(static_cast<GuestFunction*>(function))) {
      function->set_status(Symbol::Status::kFailed);
      return nullptr;
    }
    function->set_status(Symbol::Status::kDeclared);
  }
  return function;
}

bool Processor::DemandFunction(Function* function) {
  // Lock function for generation. If it's already being generated
  // by another thread this will block and return DECLARED.
  auto module = function->module();
  // GPU D3D9-HLE: intercepted functions were patched to `sc; blr` at declare time
  // (LookupFunction); DefineFunction below compiles that into the extern thunk
  // (the `sc` emits CallExtern -> HleInterceptHandler), so no special-case here.
  auto symbol_status = module->DefineFunction(function);
  if (symbol_status == Symbol::Status::kNew) {
    // Symbol is undefined, so define now.
    assert_true(function->is_guest());
    if (!frontend_->DefineFunction(static_cast<GuestFunction*>(function),
                                   debug_info_flags_)) {
      function->set_status(Symbol::Status::kFailed);
      return false;
    }

    // Before we give the symbol back to the rest, let the debugger know.
    OnFunctionDefined(function);

    function->set_status(Symbol::Status::kDefined);
    symbol_status = function->status();
  }

  if (symbol_status == Symbol::Status::kFailed) {
    // Symbol likely failed.
    return false;
  }

  return true;
}

bool Processor::Execute(ThreadState* thread_state, uint32_t address) {
  SCOPE_profile_cpu_f("cpu");

  // Attempt to get the function.
  auto function = ResolveFunction(address);
  if (!function) {
    // Symbol not found in any module.
    XELOGCPU("Execute({:08X}): failed to find function", address);
    return false;
  }

  auto context = thread_state->context();

  // Pad out stack a bit, as some games seem to overwrite the caller by about
  // 16 to 32b.
  context->r[1] -= 64 + 112;

  // This could be set to anything to give us a unique identifier to track
  // re-entrancy/etc.
  uint64_t previous_lr = context->lr;
  context->lr = 0xBCBCBCBC;

  // Execute the function.
  auto result = function->Call(thread_state, uint32_t(context->lr));

  context->lr = previous_lr;
  context->r[1] += 64 + 112;

  return result;
}

bool Processor::ExecuteRaw(ThreadState* thread_state, uint32_t address) {
  SCOPE_profile_cpu_f("cpu");

  // Attempt to get the function.
  auto function = ResolveFunction(address);
  if (!function) {
    // Symbol not found in any module.
    XELOGCPU("Execute({:08X}): failed to find function", address);
    return false;
  }

  return function->Call(thread_state, 0xBCBCBCBC);
}

uint64_t Processor::Execute(ThreadState* thread_state, uint32_t address,
                            uint64_t args[], size_t arg_count) {
  SCOPE_profile_cpu_f("cpu");

  auto context = thread_state->context();
  for (size_t i = 0; i < std::min(arg_count, static_cast<size_t>(8)); ++i) {
    context->r[3 + i] = args[i];
  }

  if (arg_count > 7) {
    // Rest of the arguments go on the stack.
    // FIXME: This assumes arguments are 32 bits!
    auto stack_arg_base =
        memory()->TranslateVirtual((uint32_t)context->r[1] + 0x54 - (64 + 112));
    for (size_t i = 0; i < arg_count - 8; i++) {
      xe::store_and_swap<uint32_t>(stack_arg_base + (i * 8),
                                   (uint32_t)args[i + 8]);
    }
  }

  if (!Execute(thread_state, address)) {
    return 0xDEADBABE;
  }
  return context->r[3];
}

uint64_t Processor::ExecuteInterrupt(ThreadState* thread_state,
                                     uint32_t address, uint64_t args[],
                                     size_t arg_count) {
  SCOPE_profile_cpu_f("cpu");

  // Hold the global lock during interrupt dispatch.
  // This will block if any code is in a critical region (has interrupts
  // disabled) or if any other interrupt is executing.
  auto global_lock = global_critical_region_.Acquire();

  auto context = thread_state->context();
  assert_true(arg_count <= 5);
  for (size_t i = 0; i < arg_count; ++i) {
    context->r[3 + i] = args[i];
  }

  // TLS ptr must be zero during interrupts. Some games check this and
  // early-exit routines when under interrupts.
  auto pcr_address =
      memory_->TranslateVirtual(static_cast<uint32_t>(context->r[13]));
  uint32_t old_tls_ptr = xe::load_and_swap<uint32_t>(pcr_address);
  xe::store_and_swap<uint32_t>(pcr_address, 0);

  if (!Execute(thread_state, address)) {
    return 0xDEADBABE;
  }

  // Restores TLS ptr.
  xe::store_and_swap<uint32_t>(pcr_address, old_tls_ptr);

  return context->r[3];
}

Irql Processor::RaiseIrql(Irql new_value) {
  return static_cast<Irql>(
      xe::atomic_exchange(static_cast<uint32_t>(new_value),
                          reinterpret_cast<volatile uint32_t*>(&irql_)));
}

void Processor::LowerIrql(Irql old_value) {
  xe::atomic_exchange(static_cast<uint32_t>(old_value),
                      reinterpret_cast<volatile uint32_t*>(&irql_));
}

bool Processor::Save(ByteStream* stream) {
  stream->Write(kProcessorSaveSignature);
  return true;
}

bool Processor::Restore(ByteStream* stream) {
  if (stream->Read<uint32_t>() != kProcessorSaveSignature) {
    XELOGE("Processor::Restore - Invalid magic value!");
    return false;
  }

  // Clear cached thread data for zombie threads.
  std::vector<uint32_t> to_delete;
  for (auto& it : thread_debug_infos_) {
    if (it.second->state == ThreadDebugInfo::State::kZombie) {
      to_delete.push_back(it.first);
    }
  }
  for (uint32_t thread_id : to_delete) {
    thread_debug_infos_.erase(thread_id);
  }

  return true;
}

uint8_t* Processor::AllocateFunctionTraceData(size_t size) {
  if (!functions_trace_file_) {
    return nullptr;
  }
  return functions_trace_file_->Allocate(size);
}

void Processor::OnFunctionDefined(Function* function) {
  auto global_lock = global_critical_region_.Acquire();
  for (auto breakpoint : breakpoints_) {
    if (breakpoint->address_type() == Breakpoint::AddressType::kGuest) {
      if (function->ContainsAddress(breakpoint->guest_address())) {
        if (breakpoint->is_installed()) {
          backend_->InstallBreakpoint(breakpoint, function);
        }
      }
    }
  }
}

void Processor::OnThreadCreated(uint32_t thread_handle,
                                ThreadState* thread_state, Thread* thread) {
  auto global_lock =
      global_critical_region_.Acquire("Processor::OnThreadCreated");
  auto thread_info = std::make_unique<ThreadDebugInfo>();
  thread_info->thread_handle = thread_handle;
  thread_info->thread_id = thread_state->thread_id();
  thread_info->system_thread_id =
      thread && thread->thread() ? thread->thread()->system_id() : 0;
  thread_info->thread = thread;
  thread_info->state = ThreadDebugInfo::State::kAlive;
  thread_info->suspended = false;
  UpdateThreadDebugHint(thread_info->system_thread_id, thread_info->thread_id,
                        thread_info->thread_handle, thread_info->state);
  thread_debug_infos_.emplace(thread_info->thread_id, std::move(thread_info));
}

void Processor::OnThreadNativeStarted(uint32_t thread_id,
                                      uint32_t system_thread_id) {
  if (!system_thread_id) {
    return;
  }

  auto global_lock =
      global_critical_region_.Acquire("Processor::OnThreadNativeStarted");
  auto it = thread_debug_infos_.find(thread_id);
  if (it == thread_debug_infos_.end()) {
    return;
  }
  auto* thread_info = it->second.get();
  thread_info->system_thread_id = system_thread_id;
  UpdateThreadDebugHint(thread_info->system_thread_id, thread_info->thread_id,
                        thread_info->thread_handle, thread_info->state);
}

void Processor::OnThreadExit(uint32_t thread_id) {
  auto global_lock =
      global_critical_region_.Acquire("Processor::OnThreadExit");
  auto it = thread_debug_infos_.find(thread_id);
  assert_true(it != thread_debug_infos_.end());
  auto thread_info = it->second.get();
  thread_info->state = ThreadDebugInfo::State::kExited;
  UpdateThreadDebugHint(thread_info->system_thread_id, thread_info->thread_id,
                        thread_info->thread_handle, thread_info->state);
}

void Processor::OnThreadDestroyed(uint32_t thread_id) {
  auto global_lock =
      global_critical_region_.Acquire("Processor::OnThreadDestroyed");
  auto it = thread_debug_infos_.find(thread_id);
  assert_true(it != thread_debug_infos_.end());
  auto thread_info = it->second.get();
  thread_info->state = ThreadDebugInfo::State::kZombie;
  UpdateThreadDebugHint(thread_info->system_thread_id, thread_info->thread_id,
                        thread_info->thread_handle, thread_info->state);
  thread_info->thread = nullptr;
}

void Processor::OnThreadEnteringWait(uint32_t thread_id) {
  auto global_lock =
      global_critical_region_.Acquire("Processor::OnThreadEnteringWait");
  auto it = thread_debug_infos_.find(thread_id);
  assert_true(it != thread_debug_infos_.end());
  auto thread_info = it->second.get();
  thread_info->state = ThreadDebugInfo::State::kWaiting;
  UpdateThreadDebugHint(thread_info->system_thread_id, thread_info->thread_id,
                        thread_info->thread_handle, thread_info->state);
}

void Processor::OnThreadLeavingWait(uint32_t thread_id) {
  auto global_lock =
      global_critical_region_.Acquire("Processor::OnThreadLeavingWait");
  auto it = thread_debug_infos_.find(thread_id);
  assert_true(it != thread_debug_infos_.end());
  auto thread_info = it->second.get();
  if (thread_info->state == ThreadDebugInfo::State::kWaiting) {
    thread_info->state = ThreadDebugInfo::State::kAlive;
    UpdateThreadDebugHint(thread_info->system_thread_id, thread_info->thread_id,
                          thread_info->thread_handle, thread_info->state);
  }
}

void Processor::UpdateThreadDebugHint(uint32_t system_thread_id,
                                      uint32_t thread_id,
                                      uint32_t thread_handle,
                                      ThreadDebugInfo::State state) {
  if (!system_thread_id) {
    return;
  }

  ThreadDebugHintSlot* empty_slot = nullptr;
  for (auto& slot : thread_debug_hint_slots_) {
    const uint32_t slot_system_thread_id =
        slot.system_thread_id.load(std::memory_order_acquire);
    const uint32_t slot_thread_id =
        slot.thread_id.load(std::memory_order_relaxed);
    if (slot_system_thread_id == system_thread_id ||
        (thread_id && slot_thread_id == thread_id)) {
      slot.thread_id.store(thread_id, std::memory_order_relaxed);
      slot.thread_handle.store(thread_handle, std::memory_order_relaxed);
      slot.state.store(static_cast<uint32_t>(state),
                       std::memory_order_relaxed);
      slot.system_thread_id.store(system_thread_id, std::memory_order_release);
      return;
    }
    if (!slot_system_thread_id && !empty_slot) {
      empty_slot = &slot;
    }
  }

  if (!empty_slot) {
    return;
  }
  empty_slot->thread_id.store(thread_id, std::memory_order_relaxed);
  empty_slot->thread_handle.store(thread_handle, std::memory_order_relaxed);
  empty_slot->state.store(static_cast<uint32_t>(state),
                          std::memory_order_relaxed);
  empty_slot->system_thread_id.store(system_thread_id,
                                     std::memory_order_release);
}

std::vector<ThreadDebugInfo*> Processor::QueryThreadDebugInfos() {
  auto global_lock = global_critical_region_.Acquire();
  std::vector<ThreadDebugInfo*> result;
  for (auto& it : thread_debug_infos_) {
    result.push_back(it.second.get());
  }
  return result;
}

std::vector<ThreadDebugInfo*> Processor::TryQueryThreadDebugInfos(
    bool* acquired) {
  auto global_lock = global_critical_region_.TryAcquire();
  bool owns_lock = global_lock.owns_lock();
  if (acquired) {
    *acquired = owns_lock;
  }
  std::vector<ThreadDebugInfo*> result;
  if (!owns_lock) {
    return result;
  }
  for (auto& it : thread_debug_infos_) {
    result.push_back(it.second.get());
  }
  return result;
}

bool Processor::TryGetThreadDebugHintBySystemThreadId(
    uint32_t system_thread_id, ThreadDebugHint* out_hint) const {
  if (!system_thread_id || !out_hint) {
    return false;
  }

  for (const auto& slot : thread_debug_hint_slots_) {
    const uint32_t slot_system_thread_id =
        slot.system_thread_id.load(std::memory_order_acquire);
    if (slot_system_thread_id != system_thread_id) {
      continue;
    }
    out_hint->system_thread_id = slot_system_thread_id;
    out_hint->thread_id = slot.thread_id.load(std::memory_order_relaxed);
    out_hint->thread_handle =
        slot.thread_handle.load(std::memory_order_relaxed);
    out_hint->state = static_cast<ThreadDebugInfo::State>(
        slot.state.load(std::memory_order_relaxed));
    return true;
  }

  return false;
}

bool Processor::TryGetThreadDebugHintByThreadIdOrHandle(
    uint32_t thread_id_or_handle, ThreadDebugHint* out_hint) const {
  if (!thread_id_or_handle || !out_hint) {
    return false;
  }

  for (const auto& slot : thread_debug_hint_slots_) {
    const uint32_t slot_system_thread_id =
        slot.system_thread_id.load(std::memory_order_acquire);
    if (!slot_system_thread_id) {
      continue;
    }
    const uint32_t slot_thread_id =
        slot.thread_id.load(std::memory_order_relaxed);
    const uint32_t slot_thread_handle =
        slot.thread_handle.load(std::memory_order_relaxed);
    if (slot_thread_id != thread_id_or_handle &&
        slot_thread_handle != thread_id_or_handle) {
      continue;
    }
    out_hint->system_thread_id = slot_system_thread_id;
    out_hint->thread_id = slot_thread_id;
    out_hint->thread_handle = slot_thread_handle;
    out_hint->state = static_cast<ThreadDebugInfo::State>(
        slot.state.load(std::memory_order_relaxed));
    return true;
  }

  return false;
}

ThreadDebugInfo* Processor::QueryThreadDebugInfo(uint32_t thread_id) {
  auto global_lock = global_critical_region_.Acquire();
  const auto& it = thread_debug_infos_.find(thread_id);
  if (it == thread_debug_infos_.end()) {
    return nullptr;
  }
  return it->second.get();
}

void Processor::AddBreakpoint(Breakpoint* breakpoint) {
  auto global_lock = global_critical_region_.Acquire();

  // Add to breakpoints map.
  breakpoints_.push_back(breakpoint);

  if (execution_state_ == ExecutionState::kRunning) {
    breakpoint->Resume();
  }
}

void Processor::RemoveBreakpoint(Breakpoint* breakpoint) {
  auto global_lock = global_critical_region_.Acquire();

  // Uninstall (if needed).
  if (execution_state_ == ExecutionState::kRunning) {
    breakpoint->Suspend();
  }

  // Remove from breakpoint map.
  auto it = std::find(breakpoints_.begin(), breakpoints_.end(), breakpoint);
  breakpoints_.erase(it);
}

Breakpoint* Processor::FindBreakpoint(uint32_t address) {
  auto global_lock = global_critical_region_.Acquire();
  for (auto breakpoint : breakpoints_) {
    if (breakpoint->address() == address) {
      return breakpoint;
    }
  }
  return nullptr;
}

void Processor::set_debug_listener(DebugListener* debug_listener) {
  if (debug_listener == debug_listener_) {
    return;
  }
  if (debug_listener_) {
    // Detach old debug listener.
    debug_listener_->OnDetached();
    debug_listener_ = nullptr;
  }
  if (debug_listener) {
    debug_listener_ = debug_listener;
  } else {
    if (execution_state_ == ExecutionState::kPaused) {
      XELOGI("Debugger detaching while execution is paused; continuing...");
      Continue();
    }
  }
}

void Processor::DemandDebugListener() {
  if (debug_listener_) {
    // Already present.
    debug_listener_->OnFocus();
    return;
  }
  if (!debug_listener_handler_) {
    XELOGE("Debugger demanded a listener but no handler was registered.");
    xe::debugging::Break();
    return;
  }
  set_debug_listener(debug_listener_handler_(this));
}

bool Processor::OnThreadBreakpointHit(Exception* ex) {
  auto global_lock = global_critical_region_.Acquire();

  // Suspend all threads (but ourselves).
  SuspendAllThreads();

  // Lookup thread info block.
  auto it = thread_debug_infos_.find(ThreadState::GetThreadID());
  if (it == thread_debug_infos_.end()) {
    // Not found - exception on a thread we don't know about?
    assert_always("UD2 on a thread we don't track");
    return false;
  }
  auto thread_info = it->second.get();

  // Run through and uninstall all breakpoint UD2s to get us back to a clean
  // state.
  if (execution_state_ != ExecutionState::kStepping) {
    SuspendAllBreakpoints();
  }

  // Update all thread states with their latest values, using the context we
  // got from the exception instead of a sampled value (as it would just show
  // the exception handler).
  UpdateThreadExecutionStates(thread_info->thread_id, ex->thread_context());

  // Walk the captured thread stack and look for breakpoints at any address in
  // the stack. We just look for the first one.
  Breakpoint* breakpoint = nullptr;
  for (size_t i = 0; i < thread_info->frames.size(); ++i) {
    auto& frame = thread_info->frames[i];
    for (auto scan_breakpoint : breakpoints_) {
      if ((scan_breakpoint->address_type() == Breakpoint::AddressType::kGuest &&
           scan_breakpoint->guest_address() == frame.guest_pc) ||
          (scan_breakpoint->address_type() == Breakpoint::AddressType::kHost &&
           scan_breakpoint->host_address() == frame.host_pc)) {
        breakpoint = scan_breakpoint;
        break;
      }
    }
    if (breakpoint) {
      breakpoint->OnHit(thread_info, frame.host_pc);
      break;
    }
  }

  // We are waiting on the debugger now. Either wait for it to continue, add a
  // new step, or direct us somewhere else.
  // The debugger will ResumeAllThreads or just resume us (depending on what
  // it wants to do).
  execution_state_ = ExecutionState::kPaused;
  thread_info->suspended = true;

  // Must unlock, or we will deadlock.
  global_lock.unlock();

  if (debug_listener_) {
    debug_listener_->OnExecutionPaused();
  }

  thread_info->thread->thread()->Suspend();

  // Apply thread context changes.
  // TODO(benvanik): apply to all threads?
#if XE_ARCH_AMD64
  ex->set_resume_pc(thread_info->host_context.rip);
#elif XE_ARCH_ARM64
  ex->set_resume_pc(thread_info->host_context.pc);
#else
#error Instruction pointer not specified for the target CPU architecture.
#endif  // XE_ARCH

  // Resume execution.
  return true;
}

void Processor::OnStepCompleted(ThreadDebugInfo* thread_info) {
  auto global_lock = global_critical_region_.Acquire();
  execution_state_ = ExecutionState::kPaused;
  if (debug_listener_) {
    debug_listener_->OnExecutionPaused();
  }

  // Note that we stay suspended.
}

bool Processor::OnUnhandledException(Exception* ex) {
  // If we have no listener return right away.
  // TODO(benvanik): DemandDebugListener()?
  if (!debug_listener_) {
    return false;
  }

  // If this isn't a managed thread, fail - let VS handle it for now.
  if (!Thread::IsInThread()) {
    return false;
  }

  auto global_lock = global_critical_region_.Acquire();

  // Suspend all guest threads (but this one).
  SuspendAllThreads();

  UpdateThreadExecutionStates(Thread::GetCurrentThreadId(),
                              ex->thread_context());

  // Stop and notify the listener.
  // This will take control.
  assert_true(execution_state_ == ExecutionState::kRunning);
  execution_state_ = ExecutionState::kPaused;

  // Notify debugger that exceution stopped.
  // debug_listener_->OnException(info);
  debug_listener_->OnExecutionPaused();

  // Suspend self.
  Thread::GetCurrentThread()->thread()->Suspend();

  return true;
}

void Processor::ShowDebugger() {
  if (debug_listener_) {
    debug_listener_->OnFocus();
  } else {
    DemandDebugListener();
  }
}

bool Processor::SuspendAllThreads() {
  auto global_lock = global_critical_region_.Acquire();
  for (auto& it : thread_debug_infos_) {
    auto thread_info = it.second.get();
    if (thread_info->suspended) {
      // Already suspended - ignore.
      continue;
    } else if (thread_info->state == ThreadDebugInfo::State::kZombie ||
               thread_info->state == ThreadDebugInfo::State::kExited) {
      // Thread is dead and cannot be suspended - ignore.
      continue;
    } else if (Thread::IsInThread() &&
               thread_info->thread_id == Thread::GetCurrentThreadId()) {
      // Can't suspend ourselves.
      continue;
    }
    auto thread = thread_info->thread;
    if (!thread->can_debugger_suspend()) {
      // Thread is a host thread, and we aren't suspending those (for now).
      continue;
    }
    bool did_suspend = thread->thread()->Suspend(nullptr);
    assert_true(did_suspend);
    thread_info->suspended = true;
  }
  return true;
}

bool Processor::ResumeThread(uint32_t thread_id) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = thread_debug_infos_.find(thread_id);
  if (it == thread_debug_infos_.end()) {
    return false;
  }
  auto thread_info = it->second.get();
  assert_true(thread_info->suspended);
  assert_false(thread_info->state == ThreadDebugInfo::State::kExited ||
               thread_info->state == ThreadDebugInfo::State::kZombie);
  thread_info->suspended = false;
  auto thread = thread_info->thread;
  return thread->thread()->Resume();
}

bool Processor::ResumeAllThreads() {
  auto global_lock = global_critical_region_.Acquire();
  for (auto& it : thread_debug_infos_) {
    auto thread_info = it.second.get();
    if (!thread_info->suspended) {
      // Not suspended by us - ignore.
      continue;
    } else if (thread_info->state == ThreadDebugInfo::State::kZombie ||
               thread_info->state == ThreadDebugInfo::State::kExited) {
      // Thread is dead and cannot be resumed - ignore.
      continue;
    } else if (Thread::IsInThread() &&
               thread_info->thread_id == Thread::GetCurrentThreadId()) {
      // Can't resume ourselves.
      continue;
    }
    auto thread = thread_info->thread;
    if (!thread->can_debugger_suspend()) {
      // Thread is a host thread, and we aren't suspending those (for now).
      continue;
    }
    thread_info->suspended = false;
    bool did_resume = thread->thread()->Resume();
    assert_true(did_resume);
  }
  return true;
}

void Processor::UpdateThreadExecutionStates(
    uint32_t override_thread_id, HostThreadContext* override_context) {
  auto global_lock = global_critical_region_.Acquire();
  uint64_t frame_host_pcs[64];
  xe::cpu::StackFrame cpu_frames[64];
  for (auto& it : thread_debug_infos_) {
    auto thread_info = it.second.get();
    auto thread = thread_info->thread;
    if (!thread) {
      continue;
    }

    // Grab PPC context.
    // Note that this is only up to date if --store_all_context_values is
    // enabled (or --debug).
    if (thread->can_debugger_suspend()) {
      std::memcpy(&thread_info->guest_context,
                  thread->thread_state()->context(),
                  sizeof(thread_info->guest_context));
    }

    // Grab stack trace and X64 context then resolve all symbols.
    uint64_t hash;
    HostThreadContext* in_host_context = nullptr;
    if (override_thread_id == thread_info->thread_id) {
      // If we were passed an override context we use that. Otherwise, ask the
      // stack walker for a new context.
      in_host_context = override_context;
    }
    size_t count = stack_walker_->CaptureStackTrace(
        thread->thread()->native_handle(), frame_host_pcs, 0,
        xe::countof(frame_host_pcs), in_host_context,
        &thread_info->host_context, &hash);
    stack_walker_->ResolveStack(frame_host_pcs, cpu_frames, count);
    thread_info->frames.resize(count);
    for (size_t i = 0; i < count; ++i) {
      auto& cpu_frame = cpu_frames[i];
      auto& frame = thread_info->frames[i];
      frame.host_pc = cpu_frame.host_pc;
      frame.host_function_address = cpu_frame.host_symbol.address;
      frame.guest_pc = cpu_frame.guest_pc;
      frame.guest_function_address = 0;
      frame.guest_function = nullptr;
      auto function = cpu_frame.guest_symbol.function;
      if (cpu_frame.type == cpu::StackFrame::Type::kGuest && function) {
        frame.guest_function_address = function->address();
        frame.guest_function = function;
      } else {
        std::strncpy(frame.name, cpu_frame.host_symbol.name,
                     xe::countof(frame.name));
        frame.name[xe::countof(frame.name) - 1] = 0;
      }
    }
  }
}

void Processor::SuspendAllBreakpoints() {
  auto global_lock = global_critical_region_.Acquire();
  for (auto breakpoint : breakpoints_) {
    breakpoint->Suspend();
  }
}

void Processor::ResumeAllBreakpoints() {
  auto global_lock = global_critical_region_.Acquire();
  for (auto breakpoint : breakpoints_) {
    breakpoint->Resume();
  }
}

void Processor::Pause() {
  {
    auto global_lock = global_critical_region_.Acquire();
    assert_true(execution_state_ == ExecutionState::kRunning);
    SuspendAllThreads();
    SuspendAllBreakpoints();
    UpdateThreadExecutionStates();
    execution_state_ = ExecutionState::kPaused;
    if (debug_listener_) {
      debug_listener_->OnExecutionPaused();
    }
  }
  DemandDebugListener();
}

void Processor::Continue() {
  auto global_lock = global_critical_region_.Acquire();
  if (execution_state_ == ExecutionState::kRunning) {
    return;
  } else if (execution_state_ == ExecutionState::kStepping) {
    assert_always("cancel stepping not done yet");
  }
  execution_state_ = ExecutionState::kRunning;
  ResumeAllBreakpoints();
  ResumeAllThreads();
  if (debug_listener_) {
    debug_listener_->OnExecutionContinued();
  }
}

void Processor::StepHostInstruction(uint32_t thread_id) {
  auto global_lock = global_critical_region_.Acquire();
  assert_true(execution_state_ == ExecutionState::kPaused);
  execution_state_ = ExecutionState::kStepping;

  auto thread_info = QueryThreadDebugInfo(thread_id);
  uint64_t new_host_pc = backend_->CalculateNextHostInstruction(
      thread_info, thread_info->frames[0].host_pc);

  assert_null(thread_info->step_breakpoint.get());
  thread_info->step_breakpoint.reset(
      new Breakpoint(this, Breakpoint::AddressType::kHost, new_host_pc,
                     [this, thread_info](Breakpoint* breakpoint,
                                         ThreadDebugInfo* breaking_thread_info,
                                         uint64_t host_address) {
                       if (thread_info != breaking_thread_info) {
                         assert_always("Step in another thread?");
                       }
                       // Our step request has completed. Remove the breakpoint
                       // and fire event.
                       breakpoint->Suspend();
                       RemoveBreakpoint(breakpoint);
                       thread_info->step_breakpoint.reset();
                       OnStepCompleted(thread_info);
                     }));
  AddBreakpoint(thread_info->step_breakpoint.get());
  thread_info->step_breakpoint->Resume();

  // ResumeAllBreakpoints();
  ResumeThread(thread_id);
}

void Processor::StepGuestInstruction(uint32_t thread_id) {
  auto global_lock = global_critical_region_.Acquire();
  assert_true(execution_state_ == ExecutionState::kPaused);
  execution_state_ = ExecutionState::kStepping;

  auto thread_info = QueryThreadDebugInfo(thread_id);

  uint32_t next_pc = CalculateNextGuestInstruction(
      thread_info, thread_info->frames[0].guest_pc);

  assert_null(thread_info->step_breakpoint.get());
  thread_info->step_breakpoint.reset(
      new Breakpoint(this, Breakpoint::AddressType::kGuest, next_pc,
                     [this, thread_info](Breakpoint* breakpoint,
                                         ThreadDebugInfo* breaking_thread_info,
                                         uint64_t host_address) {
                       if (thread_info != breaking_thread_info) {
                         assert_always("Step in another thread?");
                       }
                       // Our step request has completed. Remove the breakpoint
                       // and fire event.
                       breakpoint->Suspend();
                       RemoveBreakpoint(breakpoint);
                       thread_info->step_breakpoint.reset();
                       OnStepCompleted(thread_info);
                     }));
  AddBreakpoint(thread_info->step_breakpoint.get());
  thread_info->step_breakpoint->Resume();

  // ResumeAllBreakpoints();
  ResumeThread(thread_id);
}

// Bounds so the save-state / debugger safe-point machinery can never hang on a
// guest thread that is spinning or blocked and will never reach the stepped-to
// address (e.g. a frozen-scene worker). Each only trips in the genuinely
// non-advancing case; healthy threads converge orders of magnitude faster.
static constexpr std::chrono::milliseconds kStepWaitTimeout{1500};
static constexpr int kMaxSyncScan = 256;       // words; a BB boundary is near
static constexpr int kMaxSafePointDepth = 64;  // MMIO/mfmsr tail-recursion cap

bool Processor::StepToGuestAddress(uint32_t thread_id, uint32_t pc) {
  auto functions = FindFunctionsWithAddress(pc);
  if (functions.empty()) {
    // Function hasn't been generated yet. Generate it.
    if (!ResolveFunction(pc)) {
      XELOGE(
          "Processor::StepToAddress({:08X}) - Function could not be resolved",
          pc);
      return false;
    }
  }

  // Instruct the thread to step forwards.
  threading::Fence fence;
  cpu::Breakpoint bp(
      this, Breakpoint::AddressType::kGuest, pc,
      [&fence](Breakpoint* breakpoint, ThreadDebugInfo* thread_info,
               uint64_t host_address) { fence.Signal(); });
  bp.Resume();

  // HACK
  auto thread_info = QueryThreadDebugInfo(thread_id);
  if (!thread_info) {
    bp.Suspend();
    return false;
  }
  uint32_t suspend_count = 1;
  while (suspend_count) {
    thread_info->thread->thread()->Resume(&suspend_count);
  }

  if (!fence.WaitFor(kStepWaitTimeout)) {
    // Target thread never reached pc (spinning/blocked guest thread). Un-patch
    // the UD2 and re-suspend the thread to restore the paused invariant the
    // caller (e.g. SaveToFile) relies on, then bail. Only trips on a genuinely
    // stuck thread - a healthy step signals on the first wait iteration.
    bp.Suspend();
    thread_info->thread->thread()->Suspend();
    XELOGW("StepToGuestAddress({:08X}) timed out; thread {:08X} not advancing",
           pc, thread_id);
    return false;
  }
  bp.Suspend();

  return true;
}

uint32_t Processor::StepIntoGuestBranchTarget(uint32_t thread_id, uint32_t pc) {
  xe::cpu::ppc::PPCDecodeData d;
  d.address = pc;
  auto* d_host_ptr = memory()->TranslateVirtual(d.address);
  if (!d_host_ptr) {
    return 0;  // unmapped address -> can't decode; signal failure to caller
  }
  d.code = xe::load_and_swap<uint32_t>(d_host_ptr);
  auto opcode = xe::cpu::ppc::LookupOpcode(d.code);

  // Must be on a branch.
  assert_true(xe::cpu::ppc::GetOpcodeInfo(opcode).group ==
              xe::cpu::ppc::PPCOpcodeGroup::kB);

  auto thread_info = QueryThreadDebugInfo(thread_id);
  auto thread = thread_info->thread;
  auto context = thread->thread_state()->context();

  if (d.code == 0x4E800020) {
    // blr
    uint32_t nia = uint32_t(context->lr);
    // Propagate a step timeout (previously the return was ignored; on a healthy
    // thread StepToGuestAddress succeeds and this is identical to before).
    if (!StepToGuestAddress(thread_id, nia)) return 0;
    pc = nia;
  } else if (d.code == 0x4E800420) {
    // bctr
    uint32_t nia = uint32_t(context->ctr);
    if (!StepToGuestAddress(thread_id, nia)) return 0;
    pc = nia;
  } else if (opcode == PPCOpcode::bx) {
    // bx
    uint32_t nia = d.I.ADDR();
    if (!StepToGuestAddress(thread_id, nia)) return 0;
    pc = nia;
  } else if (opcode == PPCOpcode::bcx || opcode == PPCOpcode::bcctrx ||
             opcode == PPCOpcode::bclrx) {
    threading::Fence fence;
    auto callback = [&fence, &pc](Breakpoint* breakpoint,
                                  ThreadDebugInfo* thread_info,
                                  uint64_t host_address) {
      pc = breakpoint->guest_address();
      fence.Signal();
    };

    cpu::Breakpoint bpf(this, Breakpoint::AddressType::kGuest, pc + 4,
                        callback);
    bpf.Resume();

    uint32_t nia = 0;
    if (opcode == PPCOpcode::bcx) {
      // bcx
      nia = d.B.ADDR();
    } else if (opcode == PPCOpcode::bcctrx) {
      // bcctrx
      nia = uint32_t(context->ctr);
    } else if (opcode == PPCOpcode::bclrx) {
      // bclrx
      nia = uint32_t(context->lr);
    }

    cpu::Breakpoint bpt(this, Breakpoint::AddressType::kGuest, nia, callback);
    bpt.Resume();

    // HACK
    uint32_t suspend_count = 1;
    while (suspend_count) {
      thread->thread()->Resume(&suspend_count);
    }

    if (!fence.WaitFor(kStepWaitTimeout)) {
      // Thread never reached either branch target (spinning/blocked). Un-patch
      // both UD2s, re-suspend, and signal failure (0) to the caller.
      bpt.Suspend();
      bpf.Suspend();
      thread->thread()->Suspend();
      XELOGW(
          "StepIntoGuestBranchTarget({:08X}) timed out; thread {:08X} not "
          "advancing",
          pc, thread_id);
      return 0;
    }
    bpt.Suspend();
    bpf.Suspend();
  }

  return pc;
}

uint32_t Processor::StepToGuestSafePoint(uint32_t thread_id, bool ignore_host,
                                         int depth) {
  // This cannot be done if we're the calling thread!
  if (thread_id == ThreadState::GetThreadID()) {
    assert_always(
        "Processor::StepToSafePoint(): target thread is the calling thread!");
    return 0;
  }
  // Bound the MMIO/mfmsr tail-recursion below (each level steps one instruction
  // and re-enters) so a thread that keeps landing on non-sync host code can't
  // recurse forever. `depth` is internal bookkeeping; callers pass nothing.
  if (depth > kMaxSafePointDepth) {
    XELOGW("StepToGuestSafePoint: recursion depth cap hit for thread {:08X}",
           thread_id);
    return 0;
  }
  auto thread_info = QueryThreadDebugInfo(thread_id);
  if (!thread_info) {
    return 0;
  }
  auto thread = thread_info->thread;

  // Now the fun part begins: Registers are only guaranteed to be synchronized
  // with the PPC context at a basic block boundary. Unfortunately, we most
  // likely stopped the thread at some point other than a boundary. We need to
  // step forward until we reach a boundary, and then perform the save.
  uint64_t frame_host_pcs[64];
  cpu::StackFrame cpu_frames[64];
  size_t count = stack_walker_->CaptureStackTrace(
      thread->thread()->native_handle(), frame_host_pcs, 0,
      xe::countof(frame_host_pcs), nullptr, nullptr);
  stack_walker_->ResolveStack(frame_host_pcs, cpu_frames, count);
  if (count == 0) {
    return 0;
  }

  auto& first_frame = cpu_frames[0];
  if (ignore_host) {
    for (size_t i = 0; i < count; i++) {
      if (cpu_frames[i].type == cpu::StackFrame::Type::kGuest &&
          cpu_frames[i].guest_pc) {
        first_frame = cpu_frames[i];
        break;
      }
    }
  }

  // Check if we're in guest code or host code.
  uint32_t pc = 0;
  if (first_frame.type == cpu::StackFrame::Type::kGuest) {
    auto& frame = first_frame;
    if (!frame.guest_pc) {
      // Lame. The guest->host thunk is a "guest" function.
      frame = cpu_frames[1];
    }

    pc = frame.guest_pc;

    // We're in guest code.
    // First: Find a synchronizing instruction and go to it.
    xe::cpu::ppc::PPCDecodeData d;
    const xe::cpu::ppc::PPCOpcodeInfo* sync_info = nullptr;
    d.address = cpu_frames[0].guest_pc - 4;
    for (int scan = 0; scan < kMaxSyncScan; ++scan) {
      d.address += 4;
      auto* host_ptr = memory()->TranslateVirtual(d.address);
      if (!host_ptr) {
        break;  // ran off mapped memory - no safe point here
      }
      d.code = xe::load_and_swap<uint32_t>(host_ptr);
      auto& opcode_info = xe::cpu::ppc::LookupOpcodeInfo(d.code);
      if (opcode_info.type == cpu::ppc::PPCOpcodeType::kSync) {
        sync_info = &opcode_info;
        break;
      }
    }
    if (!sync_info) {
      // No basic-block boundary within range (corrupt PC / unmapped page /
      // genuinely no branch nearby). Can't capture a coherent context here.
      XELOGW("StepToGuestSafePoint: no sync op within {} words of {:08X}",
             kMaxSyncScan, pc);
      return 0;
    }

    if (d.address != pc) {
      if (!StepToGuestAddress(thread_id, d.address)) {
        return 0;
      }
      pc = d.address;
    }

    // Okay. Now we're on a synchronizing instruction but we need to step
    // past it in order to get a synchronized context.
    // If we're on a branching instruction, it's guaranteed only going to have
    // two possible targets. For non-branching instructions, we can just step
    // over them.
    if (sync_info->group == xe::cpu::ppc::PPCOpcodeGroup::kB) {
      pc = StepIntoGuestBranchTarget(thread_id, d.address);
      // 0 is the failure sentinel (a real guest branch target is never 0).
      if (!pc) {
        return 0;
      }
    }
  } else {
    // We're in host code. Search backwards til we can get an idea of where
    // we are.
    cpu::GuestFunction* thunk_func = nullptr;
    cpu::Export* export_data = nullptr;
    uint32_t first_pc = 0;
    for (int i = 0; i < count; i++) {
      auto& frame = cpu_frames[i];
      if (frame.type == cpu::StackFrame::Type::kGuest && frame.guest_pc) {
        auto func = frame.guest_symbol.function;
        assert_true(func->is_guest());

        if (!first_pc) {
          first_pc = frame.guest_pc;
        }

        thunk_func = reinterpret_cast<cpu::GuestFunction*>(func);
        export_data = thunk_func->export_data();
        if (export_data) {
          break;
        }
      }
    }

    // If the export is blocking, we wrap up and save inside the export thunk.
    // When we're restored, we'll call the blocking export again.
    // Otherwise, we return from the thunk and save.
    if (export_data && export_data->tags & cpu::ExportTag::kBlocking) {
      pc = thunk_func->address();
    } else if (export_data) {
      // Non-blocking. Run until we return from the thunk.
      pc = static_cast<uint32_t>(thread->thread_state()->context()->lr);
      if (!StepToGuestAddress(thread_id, pc)) {
        return 0;
      }
    } else if (first_pc) {
      // We're in the MMIO handler/mfmsr/something calling out of the guest
      // that doesn't use an export. If the current instruction is
      // synchronizing, we can just save here. Otherwise, step forward
      // (and call ourselves again so we run the correct logic).
      auto* first_host_ptr = memory()->TranslateVirtual(first_pc);
      if (!first_host_ptr) {
        return 0;
      }
      uint32_t code = xe::load_and_swap<uint32_t>(first_host_ptr);
      auto& opcode_info = xe::cpu::ppc::LookupOpcodeInfo(code);
      if (opcode_info.type == xe::cpu::ppc::PPCOpcodeType::kSync) {
        // Good to go.
        pc = first_pc;
      } else {
        // Step forward and run this logic again (depth-bounded recursion).
        if (!StepToGuestAddress(thread_id, first_pc + 4)) {
          return 0;
        }
        return StepToGuestSafePoint(thread_id, true, depth + 1);
      }
    } else {
      // We've managed to catch a thread before it called into the guest.
      // Set a breakpoint on its startup procedure and capture it there.
      // TODO(DrChat): Reimplement
      assert_always("Unimplemented");
      /*
      auto creation_params = thread->creation_params();
      pc = creation_params->xapi_thread_startup
               ? creation_params->xapi_thread_startup
               : creation_params->start_address;
      StepToGuestAddress(thread_id, pc);
      */
    }
  }

  return pc;
}

bool TestPpcCondition(const xe::cpu::ppc::PPCContext* context, uint32_t bo,
                      uint32_t bi, bool check_ctr, bool check_cond) {
  bool ctr_ok = true;
  if (check_ctr) {
    if (select_bits(bo, 2, 2)) {
      ctr_ok = true;
    } else {
      uint32_t new_ctr_value = static_cast<uint32_t>(context->ctr - 1);
      if (select_bits(bo, 1, 1)) {
        ctr_ok = new_ctr_value == 0;
      } else {
        ctr_ok = new_ctr_value != 0;
      }
    }
  }
  bool cond_ok = true;
  if (check_cond) {
    if (select_bits(bo, 4, 4)) {
      cond_ok = true;
    } else {
      uint8_t cr = *(reinterpret_cast<const uint8_t*>(&context->cr0) +
                     (4 * (bi >> 2)) + (bi & 3));
      if (select_bits(bo, 3, 3)) {
        cond_ok = cr != 0;
      } else {
        cond_ok = cr == 0;
      }
    }
  }
  return ctr_ok && cond_ok;
}

uint32_t Processor::CalculateNextGuestInstruction(ThreadDebugInfo* thread_info,
                                                  uint32_t current_pc) {
  xe::cpu::ppc::PPCDecodeData d;
  d.address = current_pc;
  d.code = xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(d.address));
  auto opcode = xe::cpu::ppc::LookupOpcode(d.code);
  if (d.code == 0x4E800020) {
    // blr -- unconditional branch to LR.
    uint32_t target_pc = static_cast<uint32_t>(thread_info->guest_context.lr);
    return target_pc;
  } else if (d.code == 0x4E800420) {
    // bctr -- unconditional branch to CTR.
    uint32_t target_pc = static_cast<uint32_t>(thread_info->guest_context.ctr);
    return target_pc;
  } else if (opcode == PPCOpcode::bx) {
    // b/ba/bl/bla
    uint32_t target_pc = d.I.ADDR();
    return target_pc;
  } else if (opcode == PPCOpcode::bcx) {
    // bc/bca/bcl/bcla
    uint32_t target_pc = d.B.ADDR();
    bool test_passed = TestPpcCondition(&thread_info->guest_context, d.B.BO(),
                                        d.B.BI(), true, true);
    return test_passed ? target_pc : current_pc + 4;
  } else if (opcode == PPCOpcode::bclrx) {
    // bclr/bclrl
    uint32_t target_pc = static_cast<uint32_t>(thread_info->guest_context.lr);
    bool test_passed = TestPpcCondition(&thread_info->guest_context, d.XL.BO(),
                                        d.XL.BI(), true, true);
    return test_passed ? target_pc : current_pc + 4;
  } else if (opcode == PPCOpcode::bcctrx) {
    // bcctr/bcctrl
    uint32_t target_pc = static_cast<uint32_t>(thread_info->guest_context.ctr);
    bool test_passed = TestPpcCondition(&thread_info->guest_context, d.XL.BO(),
                                        d.XL.BI(), false, true);
    return test_passed ? target_pc : current_pc + 4;
  } else {
    return current_pc + 4;
  }
}

}  // namespace cpu
}  // namespace xe
