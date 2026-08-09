// qemu-a64 differential for the VPERM / OPCODE_PERMUTE INT8 lowering.
//
// QUESTION: can the LLVM backend's current
//     tbl1(a, remap) | tbl1(bb, remap-16)
// be replaced by
//     tbx1(tbl1(a, remap), bb, remap-16)
// dropping the OR - 3 uOPs -> 2 on the FP/ASIMD pipe, which is only 2 wide on
// the A710/A715 mid cores.
//
// WHY THIS IS WORTH A TEST RATHER THAN AN ARGUMENT: the whole trick rests on
// UNSIGNED WRAPAROUND. For lanes whose index is 0..15 we compute remap-16,
// which as a u8 wraps to 240..255; TBX must treat that as out-of-range and
// PRESERVE the destination. That is the documented behaviour, but "index minus
// 16 wraps to something conveniently out of range" is exactly the kind of
// reasoning that deserves hardware confirmation before it ships in the byte
// permute every vector-heavy guest function runs.
//
// It also matters that TBX and TBL differ ONLY in the out-of-range case: TBL
// zeroes, TBX preserves. The current code relies on TBL's zeroing so the OR
// reconstructs tbl2; the new form relies on TBX's preservation instead.
//
// NOTE this replaces ONE of the two TBL1s with a TBX1 - it does NOT use the
// two-table tbl2/tbx2 form, so it carries NONE of the consecutive-register-pair
// constraint that crashes our AsmPrinter (llvm_assembler.cc:2471). That is the
// entire appeal: same win direction, none of the allocator risk.
//
//   aarch64-linux-gnu-g++ -x c++ -O2 -march=armv8.2-a -static \
//       -o vtx vperm_tbx_vs_tbl_or.c && qemu-aarch64 ./vtx
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef unsigned char u8;

// What we emit TODAY: two single-table TBL1s, OR'd.
static void form_tbl_or(const u8 *a, const u8 *bb, const u8 *remap, u8 *out) {
  __asm__ volatile(
      "ld1 {v0.16b}, [%0]\n"          // v0 = a
      "ld1 {v1.16b}, [%1]\n"          // v1 = bb
      "ld1 {v2.16b}, [%2]\n"          // v2 = remap
      "movi v4.16b, #16\n"
      "sub  v3.16b, v2.16b, v4.16b\n" // v3 = remap - 16 (wraps for remap<16)
      "tbl  v5.16b, {v0.16b}, v2.16b\n"
      "tbl  v6.16b, {v1.16b}, v3.16b\n"
      "orr  v7.16b, v5.16b, v6.16b\n"
      "st1  {v7.16b}, [%3]\n"
      :: "r"(a), "r"(bb), "r"(remap), "r"(out)
      : "v0","v1","v2","v3","v4","v5","v6","v7","memory");
}

// PROPOSED: TBL1 then TBX1 - the TBX preserves the TBL's result out-of-range.
static void form_tbx(const u8 *a, const u8 *bb, const u8 *remap, u8 *out) {
  __asm__ volatile(
      "ld1 {v0.16b}, [%0]\n"
      "ld1 {v1.16b}, [%1]\n"
      "ld1 {v2.16b}, [%2]\n"
      "movi v4.16b, #16\n"
      "sub  v3.16b, v2.16b, v4.16b\n"
      "tbl  v5.16b, {v0.16b}, v2.16b\n"  // a[remap], 0 where remap>=16
      "tbx  v5.16b, {v1.16b}, v3.16b\n"  // overwrite where remap-16 in 0..15
      "st1  {v5.16b}, [%3]\n"
      :: "r"(a), "r"(bb), "r"(remap), "r"(out)
      : "v0","v1","v2","v3","v4","v5","memory");
}

// Reference: what PPC vperm means for these already-remapped indices.
static void ref_model(const u8 *a, const u8 *bb, const u8 *remap, u8 *out) {
  for (int i = 0; i < 16; i++) {
    unsigned idx = remap[i];
    out[i] = (idx < 16) ? a[idx] : bb[idx - 16];
  }
}

static int check(const char *name, const u8 *a, const u8 *bb, const u8 *remap) {
  u8 o_old[16], o_new[16], o_ref[16];
  form_tbl_or(a, bb, remap, o_old);
  form_tbx(a, bb, remap, o_new);
  ref_model(a, bb, remap, o_ref);
  int ok = !memcmp(o_old, o_ref, 16) && !memcmp(o_new, o_ref, 16);
  if (!ok) {
    printf("  FAIL %-22s\n    ref:", name);
    for (int i = 0; i < 16; i++) printf(" %02X", o_ref[i]);
    printf("\n    old:");
    for (int i = 0; i < 16; i++) printf(" %02X", o_old[i]);
    printf("\n    new:");
    for (int i = 0; i < 16; i++) printf(" %02X", o_new[i]);
    printf("\n");
  }
  return ok;
}

int main(void) {
  u8 a[16], bb[16], remap[16];
  for (int i = 0; i < 16; i++) { a[i] = (u8)(0x10 + i); bb[i] = (u8)(0xA0 + i); }

  int pass = 0, total = 0;

  // 1. all lanes from a
  for (int i = 0; i < 16; i++) remap[i] = (u8)i;
  total++; pass += check("all-from-a", a, bb, remap);

  // 2. all lanes from bb
  for (int i = 0; i < 16; i++) remap[i] = (u8)(16 + i);
  total++; pass += check("all-from-bb", a, bb, remap);

  // 3. alternating - the case the OR/TBX distinction actually exercises
  for (int i = 0; i < 16; i++) remap[i] = (u8)((i & 1) ? (16 + i) : i);
  total++; pass += check("alternating", a, bb, remap);

  // 4. reversed across the pair
  for (int i = 0; i < 16; i++) remap[i] = (u8)(31 - i);
  total++; pass += check("reversed", a, bb, remap);

  // 5. all lanes the SAME index (broadcast), both halves
  for (int i = 0; i < 16; i++) remap[i] = 7;
  total++; pass += check("broadcast-a", a, bb, remap);
  for (int i = 0; i < 16; i++) remap[i] = 23;
  total++; pass += check("broadcast-bb", a, bb, remap);

  // 6. boundary indices 15 and 16 - where the wraparound argument lives
  for (int i = 0; i < 16; i++) remap[i] = (u8)((i < 8) ? 15 : 16);
  total++; pass += check("boundary-15-16", a, bb, remap);

  // 7. exhaustive over every single-lane index 0..31, all other lanes fixed
  int exhaustive_ok = 1;
  for (int idx = 0; idx < 32; idx++) {
    for (int lane = 0; lane < 16; lane++) {
      for (int i = 0; i < 16; i++) remap[i] = (u8)((i + 3) & 0x1F);
      remap[lane] = (u8)idx;
      u8 o_old[16], o_new[16], o_ref[16];
      form_tbl_or(a, bb, remap, o_old);
      form_tbx(a, bb, remap, o_new);
      ref_model(a, bb, remap, o_ref);
      if (memcmp(o_old, o_ref, 16) || memcmp(o_new, o_ref, 16)) {
        printf("  FAIL exhaustive idx=%d lane=%d\n", idx, lane);
        exhaustive_ok = 0;
      }
    }
  }
  total++; pass += exhaustive_ok;
  printf("  exhaustive 32 indices x 16 lanes: %s\n", exhaustive_ok ? "PASS" : "FAIL");

  printf("\n%d/%d PASS - tbx1 form is %s to tbl1|tbl1\n", pass, total,
         pass == total ? "BIT-IDENTICAL" : "NOT equivalent");
  return pass == total ? 0 : 1;
}
