// Emitted-code probe for cpu_backend_llvm_residency_abi (stage 3).
//
// QUESTION: does clang actually keep guest-register mirrors in callee-saved
// x19-x28 ACROSS a guest call under the reservations the LLVM backend applies
// (+reserve-x20,+reserve-x21)? That is the entire GPR premise of the lever, and
// AAPCS64 only says which registers MAY be preserved - not whether our compiler
// spends them on mirrors. The manual cannot answer this; the assembler can.
//
// RUN (no device, no qemu - this is a codegen question, read the asm):
//   NDK=~/AppData/Local/Android/Sdk/ndk/25.0.8775105/toolchains/llvm/prebuilt/windows-x86_64/bin
//   "$NDK/clang.exe" --target=aarch64-linux-android21 -O2 -S -o - //       residency_abi_probe.c -ffixed-x20 -ffixed-x21
//
// RESULT 2026-08-08 (NDK 25 / clang 14):
//   x20/x21 used ........................ 0   reservations honoured
//   reloads from the CONTEXT after call . 0   <- the whole point: no round-trip
//   mirrors kept in callee-saved regs ... 8   x19, x22, x23, x24, x25, x26, x27, x28
//   excess mirrors ...................... spill to the STACK, not the context
//
// So the GPR half of stage 3 WORKS, and its ceiling is exactly 8 - which is the
// AAPCS64 arithmetic (x19-x28 = 10, minus x20/x21) confirmed in emitted code.
// The overflow spilling to stack rather than PPCContext matters too: the stack
// is hot in L1 and does not add traffic to the 2 KB context block that review #3
// shows competing for cache.
//
// The VECTOR half is a different story and needs no probe - AAPCS64 preserves
// only the BOTTOM 64 BITS of v8-v15, so a 128-bit guest vector cannot be held
// across a call at all. See docs/reference/arm/aapcs64-callee-saved-notes.md.

typedef struct { unsigned long long r[32]; } Ctx;
extern void guest_call(void);
// 12 mirrors - more than the 8 callee-saved GPRs LLVM can have after x20/x21
// are reserved, so we can see exactly where it gives up and starts reloading.
unsigned long long probe(Ctx* c) {
  unsigned long long v[12];
  for (int k = 0; k < 12; ++k) v[k] = c->r[14 + k];
  guest_call();
  unsigned long long s = 0;
  for (int k = 0; k < 12; ++k) s += v[k];
  return s;
}
