# PowerPC / AltiVec reference (Xenon guest)

| file | what | pages |
|---|---|---|
| `altivec-programming-environments-manual.pdf` | AltiVec PEM - instruction semantics, NaN rules, rounding | 350 |
| `altivec-programming-interface-manual.pdf` | AltiVec PIM - intrinsics/programming model | 262 |
| `powerpc-user-isa-book1.pdf` | PowerPC User ISA Book I - scalar instruction semantics | 246 |
| `power-isa-2.07.pdf` | Power ISA 2.07 - the full architecture | 1526 |

**Scalar PPC questions go to Book I / ISA 2.07; VMX questions go to the AltiVec PEM.** Between them the
guest side is covered - `rlwinm` mask semantics, carry/overflow, CR field behaviour, and the VMX float rules.

The Xbox 360's Xenon is PowerPC with VMX128, so the guest side of every VMX
codegen question lives here. Pair with `docs/reference/arm/` - that answers what
ARM does, this answers what the guest EXPECTS, and a correctness question needs
both.

**It has already earned its place.** `FixupVmxMaxMinNan` was flagged as a possible
correctness bug on the strength of a remembered claim that PPC `vmaxfp` returns
the second operand on NaN. PEM p85 says the opposite, in one line:

    max(NaN,x) -> QNaN where x = any value
    min(NaN,x) -> QNaN where x = any value

which agrees with ARM `FMAX` and disagrees with x86 `MAXPS`. Read the manual
before believing a remembered ISA detail - including one of mine.

**Not obtained:** Xenon-specific VMX128 docs and anything on the Xenos GPU are not
public. For those, xenia's own RE'd register definitions and the XenDroid/xenia-edge
trees remain the reference.
