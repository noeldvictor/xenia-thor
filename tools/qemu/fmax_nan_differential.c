// qemu-a64 differential for FixupVmxMaxMinNan (xenia-thor).
//
// Question: does ARM `fmax`/`fmin` alone reproduce PPC vmaxfp/vminfp NaN
// behaviour, making the 6-uOP fixup deletable?
//
// Prints EXACT bit patterns, because the two open questions are payload-level:
// which QNaN comes out, and what the both-NaN lane should be.
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef unsigned int u32;

#define QNAN1 0x7FC00001u   // quiet NaN, payload 1
#define QNAN2 0x7FD00002u   // different quiet NaN, payload 2
#define SNAN1 0x7F800001u   // signalling NaN
#define NUM_A 0x40400000u   // 3.0f
#define NUM_B 0x40000000u   // 2.0f

// hardware fmax over 4 lanes (what we emit today, before the fixup)
static void hw_fmax(const u32 *a, const u32 *b, u32 *out) {
  __asm__ volatile(
      "ld1 {v0.4s}, [%0]\n"
      "ld1 {v1.4s}, [%1]\n"
      "fmax v2.4s, v0.4s, v1.4s\n"
      "st1 {v2.4s}, [%2]\n"
      :: "r"(a), "r"(b), "r"(out) : "v0","v1","v2","memory");
}
static void hw_fmin(const u32 *a, const u32 *b, u32 *out) {
  __asm__ volatile(
      "ld1 {v0.4s}, [%0]\n"
      "ld1 {v1.4s}, [%1]\n"
      "fmin v2.4s, v0.4s, v1.4s\n"
      "st1 {v2.4s}, [%2]\n"
      :: "r"(a), "r"(b), "r"(out) : "v0","v1","v2","memory");
}

// EXACT replica of FixupVmxMaxMinNan (a64_seq_util.h:489-508) applied on top
// of the hardware result: both-NaN lanes become src1|src2.
static void fixup_fmax(const u32 *a, const u32 *b, u32 *out) {
  __asm__ volatile(
      "ld1 {v4.4s}, [%0]\n"          // src1
      "ld1 {v5.4s}, [%1]\n"          // src2
      "fmax v2.4s, v4.4s, v5.4s\n"   // v2 = hardware result
      "orr  v3.16b, v4.16b, v5.16b\n"   // v3 = src1 | src2
      "fcmeq v0.4s, v4.4s, v4.4s\n"     // v0 = src1 non-NaN mask
      "fcmeq v1.4s, v5.4s, v5.4s\n"     // v1 = src2 non-NaN mask
      "orr  v0.16b, v0.16b, v1.16b\n"   // at least one non-NaN
      "bsl  v0.16b, v2.16b, v3.16b\n"
      "mov  v2.16b, v0.16b\n"
      "st1 {v2.4s}, [%2]\n"
      :: "r"(a), "r"(b), "r"(out)
       : "v0","v1","v2","v3","v4","v5","memory");
}

static const char *cls(u32 x) {
  if ((x & 0x7F800000u) == 0x7F800000u && (x & 0x7FFFFFu)) {
    return (x & 0x400000u) ? "QNaN" : "SNaN";
  }
  return "num ";
}

static void row(const char *op, u32 a, u32 b, u32 hw, u32 fx) {
  printf("  %s  src1=%08X(%s) src2=%08X(%s) | fmax=%08X(%s) | +fixup=%08X(%s) %s\n",
         op, a, cls(a), b, cls(b), hw, cls(hw), fx, cls(fx),
         hw == fx ? "" : "  <-- FIXUP CHANGES RESULT");
}

// Match the emitter exactly: EmitWithVmxFpcr installs DEFAULT_VMX_FPCR, which is
// (1<<24) = FZ only. DN (bit 25) is CLEAR, so NaN payloads propagate rather than
// collapsing to a default NaN - that is what makes this differential applicable.
static void set_vmx_fpcr(void) {
  unsigned long v = 1UL << 24;
  __asm__ volatile("msr fpcr, %0" :: "r"(v));
}

int main(void) {
  set_vmx_fpcr();
  {
    unsigned long rb;
    __asm__ volatile("mrs %0, fpcr" : "=r"(rb));
    printf("FPCR=%08lX (FZ=%lu DN=%lu) <- matches DEFAULT_VMX_FPCR\n",
           rb, (rb >> 24) & 1UL, (rb >> 25) & 1UL);
  }
  struct { u32 a, b; const char *name; } cases[] = {
      {NUM_A, NUM_B, "(num , num )"},
      {QNAN1, NUM_B, "(QNaN, num )"},
      {NUM_A, QNAN1, "(num , QNaN)"},
      {QNAN1, QNAN2, "(QNaN, QNaN)"},
      {SNAN1, NUM_B, "(SNaN, num )"},
      {NUM_A, SNAN1, "(num , SNaN)"},
      {SNAN1, QNAN2, "(SNaN, QNaN)"},
      {SNAN1, SNAN1, "(SNaN, SNaN)"},
  };
  printf("=== FMAX ===\n");
  for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
    u32 A[4], B[4], H[4], F[4];
    for (int l = 0; l < 4; ++l) { A[l] = cases[i].a; B[l] = cases[i].b; }
    hw_fmax(A, B, H); fixup_fmax(A, B, F);
    printf("%s", cases[i].name); row("", cases[i].a, cases[i].b, H[0], F[0]);
  }
  printf("=== FMIN (hardware only) ===\n");
  for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
    u32 A[4], B[4], H[4];
    for (int l = 0; l < 4; ++l) { A[l] = cases[i].a; B[l] = cases[i].b; }
    hw_fmin(A, B, H);
    printf("%s  fmin=%08X(%s)\n", cases[i].name, H[0], cls(H[0]));
  }
  return 0;
}
