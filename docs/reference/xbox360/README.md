# Xbox 360 (Xenon / Xenos) reference

| file | what | pages |
|---|---|---|
| `xbox360-system-architecture-ieee-micro-2006.pdf` | Andrews & Baker, **IEEE Micro 2006** - the canonical public description | 13 |
| `xbox360-hotchips17-andrews-baker.pdf` | Hot Chips 17 talk by the same authors, more block diagrams | 17 |
| `xbox360-cis501-architecture-lecture.pdf` | UPenn CIS 501 teaching deck built on the above | 5 |

## Read this before hunting for more

**There is no public Xenon or Xenos manual, and there never was.** Both are custom
parts (IBM for the CPU, ATI for the GPU) documented only under NDA. The papers
above are the canonical public sources and they are architecture *overviews* -
cache hierarchy, the 3-core/2-thread layout, EDRAM bandwidth, the unified shader
design - not instruction or register references. Do not spend a session looking
for the "real" manual; it is not on the internet.

## What to use instead, by question

| question | source |
|---|---|
| **Scalar PPC instruction semantics** | `../ppc/powerpc-user-isa-book1.pdf`, `../ppc/power-isa-2.07.pdf` |
| **VMX / AltiVec semantics** (NaN rules, saturation, rounding) | `../ppc/altivec-programming-environments-manual.pdf` |
| **VMX128** (Xenon's extended vector ISA) | **Not public.** xenia's own `src/xenia/cpu/ppc/` decoder is the reference |
| **Xenos registers, packets, EDRAM** | **Not public.** `src/xenia/gpu/registers.h`, `xenos.h` and the RE'd PM4 handling are the reference |
| **What ARM64 can do about it** | `../arm/` - the four Cortex SWOGs + the Arm ARM |

The practical consequence: for CPU work the guest side is fully documented (PPC +
AltiVec are public standards), so a codegen question can be settled from manuals on
both sides. For GPU work it cannot - Xenos is RE-only, which is exactly why the
xenia-edge / XenDroid trees matter as a reference implementation there.
