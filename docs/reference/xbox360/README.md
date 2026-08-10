# Xbox 360 (Xenon / Xenos) reference

## CPU / system overviews

| file | what | pages |
|---|---|---|
| `xbox360-system-architecture-ieee-micro-2006.pdf` | Andrews & Baker, **IEEE Micro 2006** - the canonical public description | 13 |
| `xbox360-hotchips17-andrews-baker.pdf` | Hot Chips 17 talk by the same authors, more block diagrams | 17 |
| `xbox360-cis501-architecture-lecture.pdf` | UPenn CIS 501 teaching deck built on the above | 5 |

## ATI R400 internal design documents (PRIMARY SOURCES for the GPU)

**Xenos is ATI "C1", of the R400 family, so the R400 sequencer specification IS the
Xenos shader sequencer document.** The revision history of the sequencer spec ends
with *"Rev 2.10 - Adding R500 modifications"*, which puts the later revisions
squarely in Xenos territory.

| file | what | version / date | pages |
|---|---|---|---|
| `r400-sequencer-specification-v2.11.pdf` | **R400 Sequencer Specification (SQ)**, Laurent Lefebvre. The shader sequencer: thread arbitration, register-file allocation, control flow, constant stores, all block interfaces. **The most useful document here.** | v2.11, 2003-05-01 | 54 |
| `r400-sequencer-specification-v2.10.pdf` | Previous revision, *"Adding R500 modifications"* | v2.10, 2003-04-08 | 56 |
| `r400-sequencer-specification-v0.1.pdf` | The first draft, for provenance / comparison | v0.1, 2001-05-07 | 9 |
| `r400-top-level-specification-v0.2.pdf` | **R400 Top Level Specification**, Steve Morein. Whole-chip architecture, including the **hierarchical Z** design and the render-backend block list | v0.2, 2001-03-11 | 32 |
| `r400-architecture-proposal-v0.1.pdf` | **R400 Architecture Proposal**, Steve Morein. The earlier proposal the top-level spec replaced | v0.1, 2000-11-13 | 16 |
| `r400-shader-processor-specification-v0.1.pdf` | **Shader Processor**, Andrew Gruber & Andi Skende. GPR layout, the "microthreading" register-file model, ALU instruction encoding | v0.1, 2001-01-23 | 12 |
| `r400-sequencer-emulator-perforce-history.pdf` | Perforce **change log** for ATI's R400 Sequencer Emulator. **This is a commit history, NOT source code** - no emulator source was filed in the case. Useful because it repeatedly records integrations "from Xenos", evidencing the shared codebase | 2004-2005 | 19 |

### Provenance - these are public record

All seven documents are exhibits in **PTAB case IPR2015-00325**, *LG Electronics,
Inc. v. ATI Technologies ULC* (U.S. Patent 7,742,053, "Multi-thread Graphics
Processing System"). ATI filed them under seal; **the Board DENIED the motions to
seal** in Paper 63, entered 2016-04-14:

> "There is a strong public policy interest in making all information filed in an
> inter partes review publicly available, especially evidence in support of a
> [petition to cancel claims of] an issued patent and thus affects the rights of
> the public."

Paper 63 explicitly ordered "Exhibits 2007, 2009-2018, and 2020-2042" and
"Exhibits 2043-2052 and 2107" to "be made public immediately". Every file above is
in that set. Exhibit numbers, for citation:

| file | exhibit |
|---|---|
| `r400-sequencer-specification-v0.1.pdf` | 2007 |
| `r400-sequencer-specification-v2.10.pdf` | 2038 |
| `r400-sequencer-specification-v2.11.pdf` | 2039 |
| `r400-architecture-proposal-v0.1.pdf` | 2040 |
| `r400-top-level-specification-v0.2.pdf` | 2041 |
| `r400-shader-processor-specification-v0.1.pdf` | 2042 |
| `r400-sequencer-emulator-perforce-history.pdf` | 2048 |

The full docket is downloadable without authentication from Azure blob storage:
`https://ptabdata.blob.core.windows.net/files/2015/IPR2015-00325/Exhibit-<N>.pdf`
(list the container with `?restype=container&comp=list&prefix=2015/IPR2015-00325/`).
**Exhibits 2009-2037 are the intermediate sequencer revisions v0.3 through v2.09**,
not copied here because they are Word-redline renders and v2.11 supersedes them -
fetch them from the URL above if a specific revision is ever needed.

### What the sequencer spec actually answers

Read `r400-sequencer-specification-v2.11.pdf` sections 1, 6.3 and 8, plus section
1.1.1 of the shader-processor spec.

| question | answer | where |
|---|---|---|
| wave size | **64** - "vectors of 64 vertices ... and vectors of 16 quads (64 pixels)". Predicate and valid bit-vectors are 64 bits wide | seq p6, p22 |
| waves in flight | **16 vertex + 48 pixel** thread-buffer entries | seq p21 |
| GPR budget | **128 registers**, 128 bits each, numbered 0..127, **shared between pixel and vertex threads**. GPR read/write addresses on the SQ-to-SP interface are 7 bits | seq p25-26, p44; SP p5 |
| register allocation | Two round-robins (one pixel, one vertex) over one file; the boundary between them moves dynamically, or is static at `128 - VERTEX_REG_SIZE`. A program declares its GPR count and **the sequencer will not start the next vector until that space is free** | seq p6, p25-26 |
| occupancy | Stated explicitly as a design tradeoff - see the quote below | SP p5 |
| early-Z / hierarchical-Z | **NOT in the sequencer spec at all** (zero mentions). Hierarchical Z is described in the **top-level spec**, which supports occlusion-query culling against a "heir-Z memory" held in the rasterizer | top-level p27-28 |
| fixed-function resolve | **NOT described in any of these documents.** The top-level spec's Render Backend section (8.18) is a heading with no body | top-level p32 |

The occupancy passage is the reason these documents were fetched, from the shader
processor spec p5:

> "To hide the latency of memory accesses the shader pipe will switch between
> different vectors. This is the same as the idea of 'microthreading' ... **There is
> a direct tradeoff between the number of registers each program/vector needs and
> the number of vectors that can be simultaneously resident. If there are too few
> vectors resident, then the latency of memory accesses can no longer be hidden and
> performance suffers.** There are a total of 128 registers. We do not yet know how
> many registers per vector is too many, and performance starts suffering. ... Most
> pixel programs are expected to have less than eight registers, vertex programs are
> expected to have less than sixteen registers."

## Read this before hunting for more

**There is still no public Xenon CPU manual and no Xenos register reference.** Both
are custom parts (IBM for the CPU, ATI for the GPU) documented only under NDA. The
IPR exhibits above are ATI's *internal design* documents for the R400 family - they
describe the sequencer, the shader pipe and the top-level block structure, and they
are architecturally authoritative - but they are **not** a register/packet reference
for the shipped Xenos part, and they predate the C1 tapeout. Do not expect to find a
"Xenos programming guide"; it is not on the internet.

## What to use instead, by question

| question | source |
|---|---|
| **Scalar PPC instruction semantics** | `../ppc/powerpc-user-isa-book1.pdf`, `../ppc/power-isa-2.07.pdf` |
| **VMX / AltiVec semantics** (NaN rules, saturation, rounding) | `../ppc/altivec-programming-environments-manual.pdf` |
| **VMX128** (Xenon's extended vector ISA) | **Not public.** xenia's own `src/xenia/cpu/ppc/` decoder is the reference |
| **Shader sequencer: wave size, GPR budget, occupancy, control flow** | `r400-sequencer-specification-v2.11.pdf` + `r400-shader-processor-specification-v0.1.pdf` |
| **Hierarchical Z** | `r400-top-level-specification-v0.2.pdf` section 8.15 |
| **Xenos registers, packets, EDRAM, resolve** | **Still not public.** `src/xenia/gpu/registers.h`, `xenos.h` and the RE'd PM4 handling are the reference |
| **What ARM64 can do about it** | `../arm/` - the four Cortex SWOGs + the Arm ARM |

The practical consequence: for CPU work the guest side is fully documented (PPC +
AltiVec are public standards), so a codegen question can be settled from manuals on
both sides. For GPU work it is now *partly* documented - the shader sequencer and
shader pipe have primary sources here, while registers, PM4 packets, EDRAM and the
resolve hardware remain RE-only, which is why the xenia-edge / XenDroid trees still
matter as a reference implementation there.
