// qemu-a64 correctness check for a64_fpcr_single_mode.
//
// That lever stops emitting `msr FPCR` on every scalar-FP <-> VMX transition,
// because A710 SWOG Table 4-3 note 2 makes a control-field-changing FPCR write
// a pipeline barrier. It was committed with the claim that this is
// "CORRECTNESS-PRESERVING": with FPCR.FZ never set, VMX denormal handling falls
// back to the SOFTWARE flush that PrepareVmxFpSources already emits.
//
// THAT CLAIM WAS NEVER TESTED. If the software flush does not match what the
// hardware FZ bit does, the lever silently changes VMX denormal semantics -
// which is a correctness regression wearing a performance cvar.
//
// This compares, per lane:
//   A) FPCR.FZ = 1, plain fadd            (what ships today)
//   B) FPCR.FZ = 0, FlushDenormals_V128 on both operands, then fadd
//      (what the lever does)
//
// FlushDenormals_V128 replicated exactly from a64_seq_util.h:424 -
//   shl sa, v, #1 ; movi sb,#1 ; sub sa,sa,sb ; mvni sb,#0xFF lsl 24 ;
//   cmhi sb, sb, sa ; ushr sa, sb, #1 ; bic v, v, sa
// i.e. denormal iff ((val<<1)-1) <u 0x00FFFFFF, and clearing bits 30:0 keeps
// the sign so -denormal -> -0.
//
// Build/run (no device):
//   aarch64-linux-gnu-g++ -x c++ -O2 -march=armv8.2-a -static \
//       -o fsme fpcr_single_mode_equiv.c && qemu-aarch64 ./fsme
#include <stdio.h>
#include <stdint.h>

typedef unsigned int u32;

#define FZ (1UL << 24)

static void set_fpcr(unsigned long v) {
  __asm__ volatile("msr fpcr, %0" :: "r"(v));
}

// A) what ships: hardware FZ does the flushing
static void fz_add(const u32 *a, const u32 *b, u32 *o) {
  set_fpcr(FZ);
  __asm__ volatile(
      "ld1 {v4.4s},[%0]\nld1 {v5.4s},[%1]\n"
      "fadd v2.4s, v4.4s, v5.4s\n"
      "st1 {v2.4s},[%2]\n"
      :: "r"(a), "r"(b), "r"(o) : "v2","v4","v5","memory");
}

// B) what a64_fpcr_single_mode does: FZ off, software flush both operands
static void sw_add(const u32 *a, const u32 *b, u32 *o) {
  set_fpcr(0);
  __asm__ volatile(
      "ld1 {v4.4s},[%0]\nld1 {v5.4s},[%1]\n"
      // FlushDenormals_V128(v4)
      "shl  v2.4s, v4.4s, #1\n"
      "movi v3.4s, #1\n"
      "sub  v2.4s, v2.4s, v3.4s\n"
      "mvni v3.4s, #0xFF, lsl #24\n"
      "cmhi v3.4s, v3.4s, v2.4s\n"
      "ushr v2.4s, v3.4s, #1\n"
      "bic  v4.16b, v4.16b, v2.16b\n"
      // FlushDenormals_V128(v5)
      "shl  v2.4s, v5.4s, #1\n"
      "movi v3.4s, #1\n"
      "sub  v2.4s, v2.4s, v3.4s\n"
      "mvni v3.4s, #0xFF, lsl #24\n"
      "cmhi v3.4s, v3.4s, v2.4s\n"
      "ushr v2.4s, v3.4s, #1\n"
      "bic  v5.16b, v5.16b, v2.16b\n"
      "fadd v2.4s, v4.4s, v5.4s\n"
      "st1 {v2.4s},[%2]\n"
      :: "r"(a), "r"(b), "r"(o) : "v2","v3","v4","v5","memory");
}

int main(void) {
  struct { u32 a, b; const char *name; } cases[] = {
      {0x00000001u, 0x00000000u, "min +denormal + 0    "},
      {0x007FFFFFu, 0x00000000u, "max +denormal + 0    "},
      {0x80000001u, 0x00000000u, "min -denormal + 0    "},
      {0x807FFFFFu, 0x00000000u, "max -denormal + 0    "},
      {0x00000001u, 0x00000001u, "denorm + denorm      "},
      {0x007FFFFFu, 0x00800000u, "max denorm + min norm"},
      {0x00400000u, 0x3F800000u, "denorm + 1.0         "},
      {0x80400000u, 0x3F800000u, "-denorm + 1.0        "},
      {0x00800000u, 0x00000000u, "min NORMAL + 0 (keep)"},
      {0x3F800000u, 0x40000000u, "1.0 + 2.0 (normal)   "},
      {0x00000000u, 0x80000000u, "+0 + -0              "},
      {0x7F800000u, 0x00000001u, "inf + denorm         "},
  };
  int bad = 0;
  printf("%-22s %-9s %-9s\n", "case", "FZ(hw)", "software");
  for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
    u32 A[4], B[4], X[4], Y[4];
    for (int l = 0; l < 4; ++l) { A[l] = cases[i].a; B[l] = cases[i].b; }
    fz_add(A, B, X);
    sw_add(A, B, Y);
    int ok = X[0] == Y[0];
    if (!ok) ++bad;
    printf("%-22s %08X  %08X  %s\n", cases[i].name, X[0], Y[0],
           ok ? "ok" : "DIFFERS");
  }
  printf("\n%s\n", bad
      ? "FAIL - a64_fpcr_single_mode CHANGES VMX denormal semantics"
      : "PASS - software flush matches hardware FZ; the lever is correctness-preserving");
  return bad != 0;
}
