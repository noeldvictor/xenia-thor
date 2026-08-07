# Arm Software Optimization Guides (Snapdragon 8 Gen 2 / AYN Thor)

Primary-source microarchitecture data for the cores in the Thor's QCS8550, kept
in-repo so optimisation claims can be checked against the manual instead of
against folklore.

| file | what | role | pages |
|---|---|---|---|
| `arm-architecture-reference-manual-a-profile.pdf` | **Arm ARM, DDI 0487H.a** | the architecture itself - every instruction's exact semantics | **11,530** |
| `cortex-x3-software-optimization-guide.pdf` | Cortex-X3 SWOG | 1x prime (cpu7, 3.19GHz) | 66 |
| `cortex-a710-software-optimization-guide.pdf` | Cortex-A710 SWOG | 2x mid | 92 |

**Use the right one.** The Arm ARM (66MB, 11.5k pages) is the ARCHITECTURE: what an
instruction is *defined* to do - exact semantics, flag effects, NaN handling,
memory-ordering rules. Reach for it on correctness questions (e.g. "does ARM
`FMAX` propagate NaN the way PPC `vmaxfp` does?" - see the FixupVmxMaxMinNan note
in CLAUDE.md, which is exactly this kind of question and is still open).
The SWOGs are the MICROARCHITECTURE: how fast it is on *these* cores - latency,
throughput, which issue pipe. Reach for those on performance questions. Neither
answers the other's question.

The 8 Gen 2 is 1x X3 + 2x A715 + 2x A710 + 3x A510. The **A715 and A510 guides
are not here**: Arm publishes them only through developer.arm.com's JS portal
(`PJDOC-466751330-556347` for the A715), which does not serve a direct PDF. The
A710 is the closest available proxy for the A715 - same generation, same 13-pipe
layout - but do not quote A710 latencies as A715 fact.

## What they are good for

- **Section 2.1 / Table 2-1** - the issue pipelines and which operation classes
  each one accepts. This is what answers "which resource does my loop starve on".
- **Section 3.x** - per-instruction-group **Exec Latency** and **Execution
  Throughput** tables. Exec Latency is the minimum latency an operation dependent
  on the instruction will see; Execution Throughput is the maximum sustained rate
  in instructions per cycle.
- **Section 4.x** - optimisation guidance (dispatch constraints, branch and
  load/store behaviour).

## Reading them

The `Read` tool cannot render these (no poppler in this environment). Use pypdf,
which is installed:

```python
import pypdf
r = pypdf.PdfReader("docs/reference/arm/cortex-a710-software-optimization-guide.pdf")
print(r.pages[13].extract_text())   # 0-indexed; page 14 = Table 2-1
```

## Why this landed

CLAUDE.md carried "mid-cores have 3x 128-bit load ports but only 2x arithmetic
ports" as the basis of the whole ARM64 optimisation model. Checking it against
Table 2-1 confirmed the 3 load pipes and corrected the rest: the 2 is FP/ASIMD on
the A710, while integer ALU is 4 wide there and 6 wide on the X3. That flipped
"spend loads, save arithmetic" from a general rule into a vector-only one - see
the hardware model section of CLAUDE.md.
