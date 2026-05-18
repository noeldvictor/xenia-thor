# ARM64 From x64 Conversion Audit

- Generated: 2026-05-18 14:04:37 -04:00
- Repo: C:\Users\leanerdesigner\Documents\New project 8\xenia-thor
- Scope: broad backend conversion inventory before more one-by-one Thor fixes.
- HIR opcodes: 113
- x64 emitter opcode tables: 106
- ARM64 mini-JIT switch cases: 113
- ARM64 interpreter switch cases: 113
- ARM64 JIT helper symbols: 52
- ARM64 JIT helper call sites: 52
- x64 backend .cc files: 12, lines: 10006
- ARM64 backend .cc files: 5, lines: 7459

## Read This First

- ARM64 switch coverage is necessary but not enough. It only says each HIR opcode has a case label; it does not prove native ABI, PPC semantic fidelity, memory ordering, vector corner cases, exception behavior, or performance.
- The current ARM64 path is still a research mini-JIT plus interpreter fallback. It is useful for forward progress, but it should not be treated like a converted x64 backend.
- Thor log symbol/import listings can mention names such as VdSwap without proving a runtime call. Treat the explicit `GPU swap trace: VdSwap` line as runtime proof.
- The faster loop is to batch-convert and batch-test backend families, then use Thor and Blue Dragon as milestone evidence.

## Missing HIR Switch Coverage

Mini-JIT:
- None

Interpreter:
- None

x64 emitter tables missing from scan:
- `OPCODE_COMPARE_SLT`
- `OPCODE_COMPARE_SLE`
- `OPCODE_COMPARE_SGT`
- `OPCODE_COMPARE_SGE`
- `OPCODE_COMPARE_ULT`
- `OPCODE_COMPARE_ULE`
- `OPCODE_COMPARE_UGT`
- `OPCODE_COMPARE_UGE`

## Backend Capability Gaps

- ARM64 backend is missing x64 capability `HostToGuestThunk`.
- ARM64 backend is missing x64 capability `GuestToHostThunk`.
- ARM64 backend is missing x64 capability `ResolveFunctionThunk`.
- ARM64 backend is missing x64 capability `InstallBreakpoint`.
- ARM64 backend is missing x64 capability `UninstallBreakpoint`.
- ARM64 backend is missing x64 capability `ExceptionCallback`.
- ARM64 backend is missing x64 capability `capstone_handle`.
- ARM64 backend is missing x64 capability `emitter_data`.

## Code Cache Capability Gaps

- ARM64 code cache is missing x64 capability `indirection_table`.
- ARM64 code cache is missing x64 capability `AddIndirection`.
- ARM64 code cache is missing x64 capability `CommitExecutableRange`.
- ARM64 code cache is missing x64 capability `PlaceData`.
- ARM64 code cache is missing x64 capability `UnwindReservation`.
- ARM64 code cache is missing x64 capability `generated_code_commit_mark`.

## ARM64 Helper-Heavy Surface

- Unique helper symbols found in `arm64_jit.cc`: 52
- Helper call sites emitted by the mini-JIT: 52
- Interpretation: lots of helper calls are fine for bring-up, but they are an explicit backlog for native AArch64 conversion and a likely reason Thor debugging feels slow.

Helpers:
- `Arm64JitAtomicCompareExchange`
- `Arm64JitAtomicExchange`
- `Arm64JitCastRaw`
- `Arm64JitClampPackedComponent`
- `Arm64JitClampPackedFloatBits`
- `Arm64JitClearStackSlots`
- `Arm64JitCompareFloat`
- `Arm64JitCompareFloatRaw`
- `Arm64JitCompareSigned`
- `Arm64JitCompareUnsigned`
- `Arm64JitConvertRaw`
- `Arm64JitCopy16`
- `Arm64JitDivInteger`
- `Arm64JitDotProduct3Vec128`
- `Arm64JitDotProduct4Vec128`
- `Arm64JitExtractVec128`
- `Arm64JitFloatBinaryRaw`
- `Arm64JitFloatTernaryRaw`
- `Arm64JitInsertVec128`
- `Arm64JitInvokeGuestAddress`
- `Arm64JitInvokeGuestFunction`
- `Arm64JitInvokeHostFunction`
- `Arm64JitIsNanRaw`
- `Arm64JitLoadClock`
- `Arm64JitLoadInteger`
- `Arm64JitLoadMmio`
- `Arm64JitLoadVectorShift`
- `Arm64JitMemset`
- `Arm64JitMulHighInteger`
- `Arm64JitPack16From32`
- `Arm64JitPack8From16`
- `Arm64JitPackVec128`
- `Arm64JitPermuteVec128`
- `Arm64JitRotateLeftInteger`
- `Arm64JitRotateLeftMasked`
- `Arm64JitRoundRaw`
- `Arm64JitRoundVec128`
- `Arm64JitSplatVec128`
- `Arm64JitStoreInteger`
- `Arm64JitStoreMmio`
- `Arm64JitSwizzleVec128`
- `Arm64JitUnaryRaw`
- `Arm64JitUnpackVec128`
- `Arm64JitVec128Binary`
- `Arm64JitVec128Ternary`
- `Arm64JitVec128Unary`
- `Arm64JitVectorAddSubVec128`
- `Arm64JitVectorAverageVec128`
- `Arm64JitVectorCompareVec128`
- `Arm64JitVectorConvertVec128`
- `Arm64JitVectorMinMaxVec128`
- `Arm64JitVectorShiftVec128`

## ARM64 Reject/Error Signals In Code

- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:1065: XELOGE("ARM64 JIT unsupported unpack flags {:04X}", flags);
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:1915: XELOGE("ARM64 JIT unsupported pack flags {:04X}", flags);
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:1972: XELOGE("ARM64 JIT unsupported permute part type {}",
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2063: XELOGE("ARM64 JIT missing builtin handler for {:08X} {}",
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2080: XELOGE("ARM64 JIT missing extern handler for {:08X} {}",
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2099: XELOGE("ARM64 JIT failed to resolve direct call target {:08X}",
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2110: XELOGE("ARM64 JIT failed to resolve call target {:08X}", address);
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2180: return Fail(reject_reason, "stack frame too large for mini JIT");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2183: return Fail(reject_reason, "no blocks");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2201: return Fail(reject_reason, "instruction index out of range");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2252: return Fail(reject_reason, "program has unsupported value slot type " +
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2261: return Fail(reject_reason, "program has unsupported local slot type " +
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2283: return Fail(reject_reason, std::string(opcode_name) +
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2293: return Fail(reject_reason, std::string(opcode_name) +
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2294: " has unsupported slot type " +
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2303: return Fail(reject_reason, "invalid destination value");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2310: return Fail(reject_reason, "branch target out of range");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2442: return Fail(reject_reason, "vec128 operand has non-vector type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2453: return Fail(reject_reason, "unsupported vec128 operand kind");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2462: return Fail(reject_reason, "vec128 copy has non-vector destination");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2573: return Fail(reject_reason, "value operand out of range");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2579: return Fail(reject_reason, "unsupported constant slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2587: return Fail(reject_reason, "unsupported operand kind");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2610: return Fail(reject_reason, "local load out of range");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2628: return Fail(reject_reason, "local store out of range");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2680: return Fail(reject_reason, "unsupported context load type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2717: return Fail(reject_reason, "unsupported context store type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2727: return Fail(reject_reason, "load_mmio has unsupported type " +
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2815: return Fail(reject_reason, "unsupported memory load type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2897: return Fail(reject_reason, "unsupported swapped memory store type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2918: return Fail(reject_reason, "unsupported memory store type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2945: return Fail(reject_reason,
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:2946: "assign has unsupported slot type src " +
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3028: return Fail(reject_reason, "vector_convert has non-vector slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3048: return Fail(reject_reason, "vector_compare has non-vector slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3137: return Fail(reject_reason, "unsupported float binary opcode");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3198: return Fail(reject_reason, "unsupported binary opcode");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3212: return Fail(reject_reason, "vector_add/sub has non-vector slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3234: return Fail(reject_reason, "vector_min/max has non-vector slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3255: return Fail(reject_reason, "vector_shift has non-vector slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3276: return Fail(reject_reason, "vector_average has non-vector slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3362: return Fail(reject_reason,
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3363: "mul_add/sub has unsupported slot type " +
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3481: return Fail(reject_reason,
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3482: "sqrt has unsupported slot type " +
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3513: return Fail(reject_reason, std::string(name) +
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3514: " has unsupported slot type " +
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3533: return Fail(reject_reason, "load_vector_shift has non-vector dest");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3550: return Fail(reject_reason, "unpack has non-vector slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3567: return Fail(reject_reason, "splat has unsupported slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3583: return Fail(reject_reason, "extract has non-vector source");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3602: return Fail(reject_reason, "insert has unsupported slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3622: return Fail(reject_reason, "pack has non-vector slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3640: return Fail(reject_reason, "swizzle has non-vector slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3658: return Fail(reject_reason, "permute has unsupported slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3689: return Fail(reject_reason, "dot_product_3 has unsupported slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3707: return Fail(reject_reason, "dot_product_4 has unsupported slot type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3745: return Fail(reject_reason, "unsupported byte_swap type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3817: return Fail(reject_reason, "unsupported cntlz type");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3961: return Fail(reject_reason, "direct call missing symbol");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:3990: return Fail(reject_reason, "extern call missing symbol");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:4282: return Fail(reject_reason,
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:4283: "unsupported opcode " +
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:4314: return Reject(reject_reason, "missing ARM64 backend/code cache");
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:4323: return Reject(reject_reason,
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:4333: return Reject(reject_reason, local_reject_reason);
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:4336: return Reject(reject_reason,
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:4339: return Reject(reject_reason,
- .\src\xenia\cpu\backend\arm64\arm64_jit.cc:4347: return Reject(reject_reason, "failed to place generated ARM64 code");
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:40: "x64 default. Disable to fail fast on missing kernel exports.",
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:601: XELOGE("ARM64 interpreter unsupported atomic compare-exchange type {}",
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:1323: XELOGW("ARM64 interpreter ignoring saturate flag for vector f32 add/sub");
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:1331: XELOGW("ARM64 interpreter ignoring saturate flag for vector f64 add/sub");
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:1406: XELOGE("ARM64 interpreter unsupported vector add/sub part type {}",
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:1534: XELOGE("ARM64 interpreter unsupported vector shift part type {}",
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:1593: XELOGE("ARM64 interpreter unsupported vector average part type {}",
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:1736: XELOGE("ARM64 interpreter unsupported permute part type {}",
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:1897: XELOGE("ARM64 interpreter unsupported pack flags {:04X}", flags);
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:1992: XELOGE("ARM64 interpreter unsupported unpack flags {:04X}", flags);
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:2034: XELOGE("ARM64 interpreter missing builtin handler for {:08X} {}",
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:2051: XELOGE("ARM64 interpreter missing extern handler for {:08X} {}",
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:2084: XELOGE("ARM64 interpreter failed to resolve direct call target {:08X}",
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:2103: XELOGE("ARM64 interpreter failed to resolve call target {:08X}", address);
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:2225: XELOGE("ARM64 guest function calls need a host-to-guest thunk");
- .\src\xenia\cpu\backend\arm64\arm64_function.cc:2271: "ARM64 interpreter step limit hit in {:08X}: pc {}, block {}, guest "

## Latest Thor Evidence

- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:617: 05-18 13:59:49.462 I/xenia   (15057):    F 820005EC 8273F0B4 25B ( 603)    VdSwap
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:4352: 05-18 13:59:50.364 I/xenia   (15057): i> F800022C GPU swap trace: VdInitializeEngines unk0=0DC90000 callback=82476320
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:4354: 05-18 13:59:50.364 I/xenia   (15057): i> F800022C GPU swap trace: VdInitializeEngines unk0=0DC90000 callback=82476320
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:4357: 05-18 13:59:50.373 I/xenia   (15057): i> F800022C GPU swap trace: VdSetSystemCommandBufferGpuIdentifierAddress
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:4359: 05-18 13:59:50.373 I/xenia   (15057): i> F800022C GPU swap trace: VdInitializeRingBuffer ptr=1FC50000 size_log2=15
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:4360: 05-18 13:59:50.373 I/xenia   (15057): i> F800022C GPU swap trace: VdEnableRingBufferRPtrWriteBack ptr=1FCA403C
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:4362: 05-18 13:59:50.373 I/xenia   (15057): i> F800022C GPU swap trace: VdSetSystemCommandBufferGpuIdentifierAddress
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:4745: 05-18 14:00:31.330 I/xenia   (15057): i> F80002A0 (DbgPrint) The GPU is hung!  D3D version is 3529.0 retail, kernel is
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:4753: 05-18 14:00:31.342 I/xenia   (15057): i> F80002A0 (DbgPrint)                   CP_RB_RPTR: 0x0000001f
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:4754: 05-18 14:00:31.342 I/xenia   (15057): i> F80002A0 (DbgPrint)                   CP_RB_WPTR: 0x0000001f
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:5230: 05-18 14:00:31.344 I/xenia   (15057): i> F80002A0 (DbgPrint) Breaking into the debugger.  The GPU is hung and can't be
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:5244: 05-18 14:00:36.405 I/xenia   (15057): i> F80002A0 (DbgPrint) The GPU is hung!  D3D version is 3529.0 retail, kernel is
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:5252: 05-18 14:00:36.405 I/xenia   (15057): i> F80002A0 (DbgPrint)                   CP_RB_RPTR: 0x00000025
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:5253: 05-18 14:00:36.405 I/xenia   (15057): i> F80002A0 (DbgPrint)                   CP_RB_WPTR: 0x00000025
- .\scratch\thor-debug\20260518-140109-logcat-filtered.txt:5729: 05-18 14:00:36.413 I/xenia   (15057): i> F80002A0 (DbgPrint) Breaking into the debugger.  The GPU is hung and can't be
- .\scratch\thor-debug\20260518-140109-logcat.txt:659: 05-18 13:59:49.462 I/xenia   (15057):    F 820005EC 8273F0B4 25B ( 603)    VdSwap
- .\scratch\thor-debug\20260518-140109-logcat.txt:4130: 05-18 13:59:50.364 I/xenia   (15057): i> F800022C GPU swap trace: VdInitializeEngines unk0=0DC90000 callback=82476320 arg=00000000 pfp=00000000 me=00000000
- .\scratch\thor-debug\20260518-140109-logcat.txt:4131: 05-18 13:59:50.364 I/xenia   (15057): i> F800022C GPU swap trace: VdInitializeEngines unk0=0DC90000 callback=82476320 arg=00000000 pfp=820042E8 me=82004768
- .\scratch\thor-debug\20260518-140109-logcat.txt:4133: 05-18 13:59:50.373 I/xenia   (15057): i> F800022C GPU swap trace: VdSetSystemCommandBufferGpuIdentifierAddress addr=00000000
- .\scratch\thor-debug\20260518-140109-logcat.txt:4134: 05-18 13:59:50.373 I/xenia   (15057): i> F800022C GPU swap trace: VdInitializeRingBuffer ptr=1FC50000 size_log2=15
- .\scratch\thor-debug\20260518-140109-logcat.txt:4135: 05-18 13:59:50.373 I/xenia   (15057): i> F800022C GPU swap trace: VdEnableRingBufferRPtrWriteBack ptr=1FCA403C block_size_log2=9
- .\scratch\thor-debug\20260518-140109-logcat.txt:4136: 05-18 13:59:50.373 I/xenia   (15057): i> F800022C GPU swap trace: VdSetSystemCommandBufferGpuIdentifierAddress addr=FFCA3008
- .\scratch\thor-debug\20260518-140109-logcat.txt:5371: 05-18 14:00:31.330 I/xenia   (15057): i> F80002A0 (DbgPrint) The GPU is hung!  D3D version is 3529.0 retail, kernel is 65535, frame is 0.
- .\scratch\thor-debug\20260518-140109-logcat.txt:5378: 05-18 14:00:31.342 I/xenia   (15057): i> F80002A0 (DbgPrint)                   CP_RB_RPTR: 0x0000001f
- .\scratch\thor-debug\20260518-140109-logcat.txt:5379: 05-18 14:00:31.342 I/xenia   (15057): i> F80002A0 (DbgPrint)                   CP_RB_WPTR: 0x0000001f
- .\scratch\thor-debug\20260518-140109-logcat.txt:5833: 05-18 14:00:31.344 I/xenia   (15057): i> F80002A0 (DbgPrint) Breaking into the debugger.  The GPU is hung and can't be recovered
- .\scratch\thor-debug\20260518-140109-logcat.txt:5967: 05-18 14:00:36.405 I/xenia   (15057): i> F80002A0 (DbgPrint) The GPU is hung!  D3D version is 3529.0 retail, kernel is 65535, frame is 0.
- .\scratch\thor-debug\20260518-140109-logcat.txt:5974: 05-18 14:00:36.405 I/xenia   (15057): i> F80002A0 (DbgPrint)                   CP_RB_RPTR: 0x00000025
- .\scratch\thor-debug\20260518-140109-logcat.txt:5975: 05-18 14:00:36.405 I/xenia   (15057): i> F80002A0 (DbgPrint)                   CP_RB_WPTR: 0x00000025
- .\scratch\thor-debug\20260518-140109-logcat.txt:6429: 05-18 14:00:36.413 I/xenia   (15057): i> F80002A0 (DbgPrint) Breaking into the debugger.  The GPU is hung and can't be recovered
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:4211: 05-18 13:56:58.635 I/xenia   (13736): i> F800022C GPU swap trace: VdInitializeEngines unk0=0DC90000 callback=82476320
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:4213: 05-18 13:56:58.635 I/xenia   (13736): i> F800022C GPU swap trace: VdInitializeEngines unk0=0DC90000 callback=82476320
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:4216: 05-18 13:56:58.643 I/xenia   (13736): i> F800022C GPU swap trace: VdSetSystemCommandBufferGpuIdentifierAddress
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:4218: 05-18 13:56:58.643 I/xenia   (13736): i> F800022C GPU swap trace: VdInitializeRingBuffer ptr=1FC50000 size_log2=15
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:4219: 05-18 13:56:58.644 I/xenia   (13736): i> F800022C GPU swap trace: VdEnableRingBufferRPtrWriteBack ptr=1FCA403C
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:4221: 05-18 13:56:58.644 I/xenia   (13736): i> F800022C GPU swap trace: VdSetSystemCommandBufferGpuIdentifierAddress
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:4603: 05-18 13:57:39.383 I/xenia   (13736): i> F80002A0 (DbgPrint) The GPU is hung!  D3D version is 3529.0 retail, kernel is
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:4611: 05-18 13:57:39.391 I/xenia   (13736): i> F80002A0 (DbgPrint)                   CP_RB_RPTR: 0x0000001f
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:4612: 05-18 13:57:39.391 I/xenia   (13736): i> F80002A0 (DbgPrint)                   CP_RB_WPTR: 0x0000001f
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:5088: 05-18 13:57:39.398 I/xenia   (13736): i> F80002A0 (DbgPrint) Breaking into the debugger.  The GPU is hung and can't be
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:5103: 05-18 13:57:44.458 I/xenia   (13736): i> F80002A0 (DbgPrint) The GPU is hung!  D3D version is 3529.0 retail, kernel is
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:5111: 05-18 13:57:44.458 I/xenia   (13736): i> F80002A0 (DbgPrint)                   CP_RB_RPTR: 0x00000025
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:5112: 05-18 13:57:44.458 I/xenia   (13736): i> F80002A0 (DbgPrint)                   CP_RB_WPTR: 0x00000025
- .\scratch\thor-debug\20260518-135816-logcat-filtered.txt:5588: 05-18 13:57:44.460 I/xenia   (13736): i> F80002A0 (DbgPrint) Breaking into the debugger.  The GPU is hung and can't be
- .\scratch\thor-debug\20260518-135816-logcat.txt:4005: 05-18 13:56:58.635 I/xenia   (13736): i> F800022C GPU swap trace: VdInitializeEngines unk0=0DC90000 callback=82476320 arg=00000000 pfp=00000000 me=00000000
- .\scratch\thor-debug\20260518-135816-logcat.txt:4006: 05-18 13:56:58.635 I/xenia   (13736): i> F800022C GPU swap trace: VdInitializeEngines unk0=0DC90000 callback=82476320 arg=00000000 pfp=820042E8 me=82004768
- .\scratch\thor-debug\20260518-135816-logcat.txt:4008: 05-18 13:56:58.643 I/xenia   (13736): i> F800022C GPU swap trace: VdSetSystemCommandBufferGpuIdentifierAddress addr=00000000
- .\scratch\thor-debug\20260518-135816-logcat.txt:4009: 05-18 13:56:58.643 I/xenia   (13736): i> F800022C GPU swap trace: VdInitializeRingBuffer ptr=1FC50000 size_log2=15
- .\scratch\thor-debug\20260518-135816-logcat.txt:4010: 05-18 13:56:58.644 I/xenia   (13736): i> F800022C GPU swap trace: VdEnableRingBufferRPtrWriteBack ptr=1FCA403C block_size_log2=9
- .\scratch\thor-debug\20260518-135816-logcat.txt:4011: 05-18 13:56:58.644 I/xenia   (13736): i> F800022C GPU swap trace: VdSetSystemCommandBufferGpuIdentifierAddress addr=FFCA3008
- .\scratch\thor-debug\20260518-135816-logcat.txt:5246: 05-18 13:57:39.383 I/xenia   (13736): i> F80002A0 (DbgPrint) The GPU is hung!  D3D version is 3529.0 retail, kernel is 65535, frame is 0.
- .\scratch\thor-debug\20260518-135816-logcat.txt:5253: 05-18 13:57:39.391 I/xenia   (13736): i> F80002A0 (DbgPrint)                   CP_RB_RPTR: 0x0000001f
- .\scratch\thor-debug\20260518-135816-logcat.txt:5254: 05-18 13:57:39.391 I/xenia   (13736): i> F80002A0 (DbgPrint)                   CP_RB_WPTR: 0x0000001f
- .\scratch\thor-debug\20260518-135816-logcat.txt:5708: 05-18 13:57:39.398 I/xenia   (13736): i> F80002A0 (DbgPrint) Breaking into the debugger.  The GPU is hung and can't be recovered
- .\scratch\thor-debug\20260518-135816-logcat.txt:5840: 05-18 13:57:44.458 I/xenia   (13736): i> F80002A0 (DbgPrint) The GPU is hung!  D3D version is 3529.0 retail, kernel is 65535, frame is 0.
- .\scratch\thor-debug\20260518-135816-logcat.txt:5847: 05-18 13:57:44.458 I/xenia   (13736): i> F80002A0 (DbgPrint)                   CP_RB_RPTR: 0x00000025
- .\scratch\thor-debug\20260518-135816-logcat.txt:5848: 05-18 13:57:44.458 I/xenia   (13736): i> F80002A0 (DbgPrint)                   CP_RB_WPTR: 0x00000025
- .\scratch\thor-debug\20260518-135816-logcat.txt:6302: 05-18 13:57:44.460 I/xenia   (13736): i> F80002A0 (DbgPrint) Breaking into the debugger.  The GPU is hung and can't be recovered
- .\scratch\thor-debug\20260518-134757-logcat-filtered.txt:617: 05-18 13:45:23.656 I/xenia   ( 8213):    F 820005EC 8273F0B4 25B ( 603)    VdSwap
- .\scratch\thor-debug\20260518-134757-logcat-filtered.txt:4570: 05-18 13:46:05.437 I/xenia   ( 8213): i> F80002A0 (DbgPrint) The GPU is hung!  D3D version is 3529.0 retail, kernel is
- .\scratch\thor-debug\20260518-134757-logcat-filtered.txt:4578: 05-18 13:46:05.443 I/xenia   ( 8213): i> F80002A0 (DbgPrint)                   CP_RB_RPTR: 0x0000001f
- .\scratch\thor-debug\20260518-134757-logcat-filtered.txt:4579: 05-18 13:46:05.443 I/xenia   ( 8213): i> F80002A0 (DbgPrint)                   CP_RB_WPTR: 0x0000001f
- .\scratch\thor-debug\20260518-134757-logcat-filtered.txt:5055: 05-18 13:46:05.446 I/xenia   ( 8213): i> F80002A0 (DbgPrint) Breaking into the debugger.  The GPU is hung and can't be
- .\scratch\thor-debug\20260518-134757-logcat-filtered.txt:5068: 05-18 13:46:10.502 I/xenia   ( 8213): i> F80002A0 (DbgPrint) The GPU is hung!  D3D version is 3529.0 retail, kernel is
- .\scratch\thor-debug\20260518-134757-logcat-filtered.txt:5076: 05-18 13:46:10.502 I/xenia   ( 8213): i> F80002A0 (DbgPrint)                   CP_RB_RPTR: 0x00000025
- .\scratch\thor-debug\20260518-134757-logcat-filtered.txt:5077: 05-18 13:46:10.502 I/xenia   ( 8213): i> F80002A0 (DbgPrint)                   CP_RB_WPTR: 0x00000025
- .\scratch\thor-debug\20260518-134757-logcat-filtered.txt:5553: 05-18 13:46:10.504 I/xenia   ( 8213): i> F80002A0 (DbgPrint) Breaking into the debugger.  The GPU is hung and can't be
- .\scratch\thor-debug\20260518-134757-logcat.txt:658: 05-18 13:45:23.656 I/xenia   ( 8213):    F 820005EC 8273F0B4 25B ( 603)    VdSwap
- .\scratch\thor-debug\20260518-134757-logcat.txt:5246: 05-18 13:46:05.437 I/xenia   ( 8213): i> F80002A0 (DbgPrint) The GPU is hung!  D3D version is 3529.0 retail, kernel is 65535, frame is 0.
- .\scratch\thor-debug\20260518-134757-logcat.txt:5253: 05-18 13:46:05.443 I/xenia   ( 8213): i> F80002A0 (DbgPrint)                   CP_RB_RPTR: 0x0000001f
- .\scratch\thor-debug\20260518-134757-logcat.txt:5254: 05-18 13:46:05.443 I/xenia   ( 8213): i> F80002A0 (DbgPrint)                   CP_RB_WPTR: 0x0000001f
- .\scratch\thor-debug\20260518-134757-logcat.txt:5708: 05-18 13:46:05.446 I/xenia   ( 8213): i> F80002A0 (DbgPrint) Breaking into the debugger.  The GPU is hung and can't be recovered
- .\scratch\thor-debug\20260518-134757-logcat.txt:5828: 05-18 13:46:10.502 I/xenia   ( 8213): i> F80002A0 (DbgPrint) The GPU is hung!  D3D version is 3529.0 retail, kernel is 65535, frame is 0.
- .\scratch\thor-debug\20260518-134757-logcat.txt:5835: 05-18 13:46:10.502 I/xenia   ( 8213): i> F80002A0 (DbgPrint)                   CP_RB_RPTR: 0x00000025
- .\scratch\thor-debug\20260518-134757-logcat.txt:5836: 05-18 13:46:10.502 I/xenia   ( 8213): i> F80002A0 (DbgPrint)                   CP_RB_WPTR: 0x00000025
- .\scratch\thor-debug\20260518-134757-logcat.txt:6290: 05-18 13:46:10.504 I/xenia   ( 8213): i> F80002A0 (DbgPrint) Breaking into the debugger.  The GPU is hung and can't be recovered

## First-Pass Conversion Queue

- Build a real AArch64 emitter backbone: ABI prolog/epilog, register allocator, native integer/scalar/vector lowering, and helper-call escape hatches only for hard cases.
- Port x64 backend services before more game chasing: host-to-guest thunk, guest-to-host thunk, resolve thunk, breakpoint install/uninstall, source maps, and exception/signal recovery.
- Replace helper-heavy mini-JIT paths in broad families: scalar integer, scalar float, memory/MMIO, control flow, vector arithmetic, vector shuffle, pack/unpack, atomics.
- Add compiled-vs-interpreter differential tests for HIR microprograms so each opcode family can be validated on PC/CI before Thor.
- Add x64-vs-ARM64 replay tests for guest functions that Blue Dragon actually compiles, comparing PPC context and touched memory after bounded execution.
- Make code cache production-shaped: W^X, instruction cache flush, per-function metadata, unwind info where available, indirection/resolve table, invalidation hooks.
- Audit PowerPC semantic fidelity beyond HIR switch coverage: CR/XER/FPSCR, saturate flag, rounding, NaN, denorms, signed overflow, carry, byte order, unaligned memory.
- Make Android debug loops batchable: one command for build, deploy, launch, capture, summarize, and diff against prior known-good captures.
- Use Ghidra for focused guest function mapping only after the backend audit identifies a suspicious Blue Dragon function, not as the primary iteration loop.
- Keep Thor runs as milestone checkpoints after broad passes, not as the only unit test.

## Batch Plan

- Batch 1: backend infrastructure parity, no game-specific hacks.
- Batch 2: native lowering for scalar integer/control/memory families with interpreter differential tests.
- Batch 3: native lowering for scalar float and rounding/NaN behavior with focused microtests.
- Batch 4: native vec128 arithmetic/shuffle/pack/unpack with generated test vectors.
- Batch 5: Blue Dragon guest-function replay harness, then Thor milestone run.
- Batch 6: GPU/D3D contract work only after the CPU backend stops being the most likely source of false signals.
