#include <stdint.h>
#include <stdio.h>
// Old branch form.
static uint32_t branch_form(uint32_t addr) {
  uint32_t out;
  __asm__ volatile(
      "mov  w1, #0xE0000000\n"
      "cmp  %w[a], w1\n"
      "b.lo 1f\n"
      "add  %w[a], %w[a], #1, lsl #12\n"
      "1:\n"
      "mov  %w[o], %w[a]\n"
      : [o] "=r"(out), [a] "+r"(addr)::"w1", "cc");
  return out;
}
// New branch-free form.
static uint32_t csel_form(uint32_t addr) {
  uint32_t out;
  __asm__ volatile(
      "mov  w1, #0xE0000000\n"
      "cmp  %w[a], w1\n"
      "add  w1, %w[a], #1, lsl #12\n"
      "csel %w[o], %w[a], w1, lo\n"
      : [o] "=r"(out)
      : [a] "r"(addr)
      : "w1", "cc");
  return out;
}
int main(void) {
  uint64_t checked = 0; int bad = 0;
  // Every value near the boundary, plus a full sweep at a stride.
  for (uint64_t v = 0xDFFFF000ull; v <= 0xE0001000ull; v++) {
    uint32_t a = (uint32_t)v;
    if (branch_form(a) != csel_form(a)) { if (bad<5) printf("MISMATCH %08X: %08X vs %08X\n", a, branch_form(a), csel_form(a)); bad++; }
    checked++;
  }
  for (uint64_t v = 0; v <= 0xFFFFFFFFull; v += 4093) {
    uint32_t a = (uint32_t)v;
    if (branch_form(a) != csel_form(a)) { if (bad<5) printf("MISMATCH %08X: %08X vs %08X\n", a, branch_form(a), csel_form(a)); bad++; }
    checked++;
  }
  printf(bad ? "FAILED: %d mismatches of %llu\n" : "ALL PASS: %d mismatches of %llu\n", bad, (unsigned long long)checked);
  return bad ? 1 : 0;
}
