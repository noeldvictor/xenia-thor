#include <stdio.h>
typedef unsigned int u32;
static void vfadd(const u32*a,const u32*b,u32*o){__asm__ volatile(
 "ld1 {v0.4s},[%0]\nld1 {v1.4s},[%1]\nfadd v2.4s,v0.4s,v1.4s\nst1 {v2.4s},[%2]\n"
 ::"r"(a),"r"(b),"r"(o):"v0","v1","v2","memory");}
static void vfmul(const u32*a,const u32*b,u32*o){__asm__ volatile(
 "ld1 {v0.4s},[%0]\nld1 {v1.4s},[%1]\nfmul v2.4s,v0.4s,v1.4s\nst1 {v2.4s},[%2]\n"
 ::"r"(a),"r"(b),"r"(o):"v0","v1","v2","memory");}
static u32 go(void(*f)(const u32*,const u32*,u32*),u32 x,u32 y){
  u32 A[4],B[4],O[4]; for(int i=0;i<4;i++){A[i]=x;B[i]=y;} f(A,B,O); return O[0];}
int main(void){
  unsigned long v=1UL<<24; __asm__ volatile("msr fpcr,%0"::"r"(v));
  u32 N1=0x7FC00001u, N2=0x7FD00002u;
  printf("Is VECTOR_ADD/MUL really commutative for FLOAT32?\n");
  printf("  fadd(N1,N2) = %08X\n  fadd(N2,N1) = %08X   -> %s\n",
    go(vfadd,N1,N2), go(vfadd,N2,N1),
    go(vfadd,N1,N2)==go(vfadd,N2,N1)?"same":"DIFFERENT - NOT COMMUTATIVE");
  printf("  fmul(N1,N2) = %08X\n  fmul(N2,N1) = %08X   -> %s\n",
    go(vfmul,N1,N2), go(vfmul,N2,N1),
    go(vfmul,N1,N2)==go(vfmul,N2,N1)?"same":"DIFFERENT - NOT COMMUTATIVE");
  return 0;}
