# 82281D28 Metadata Capture Blocker

## Summary

The deployed block-profile metadata path works, but it does not yet make
`82281D28` weighted HIR analysis safe. The hot runtime block has metadata, but
the printed OptHIR stream still omits its source span, so behavior patches from
weighted `82281D28` HIR remain blocked.

## Capture

- Commit: `d39f3460e`
- FullDeploy: passed on Thor `c3ca0370`.
- Capture prefix: `scratch/thor-debug/20260526-021159-*`
- APK SHA256:
  `963CE83618F947C979BFAB3558CC2ED44A86055C6F8AC5077F5F0E1A7DDF5504`
- Route: Blue Dragon Disc 1 from the known Thor SD-card path.
- Screenshot: visible opening sky / dragon-wing route.
- Fatal-marker search: no Xenia/AndroidRuntime/tombstone/assert/backtrace/
  fatal exception/SIGSEGV/SIGABRT/crash/ANR rows found by the focused search.
- This was trace-heavy with disassembly and block body-time enabled; it is not
  a quiet speed proof.

## Performance Context

Final sampled threads stayed CPU/JIT-heavy:

- Main Thread: `92.5%`
- XMA Decoder: `48.1%`
- GPU Commands: `11.1%`
- Draw Thread: `3.7%`

Final delayed function body-time rows:

- `82281D28=66528341`
- `82282490=36273569`
- `82287788=3535771`

Final hot block row:

```text
fn 82281D28 block=20 guest=8228233C
body_ticks_total=47409053 entries_delta=20628 ticks_per_entry=708
first_source=8228233C last_source=82282370
first_guest=8228233C last_guest=82282370
first_comment=8228233C last_comment=82282370
label=8228233C hir_instrs=34
```

The printed OptHIR stream still starts at `8228238C` and then `loc_82282490`;
it does not include a printed label or PPC comment for `8228233C-82282370`.

## Tooling Fix

`tools/thor/thor_hir_block_profile_join_audit.ps1` was fixed after this
capture because its metadata regex overwrote PowerShell `$Matches`, causing
parsed totals to print as zero. The tool now stores profile matches before
metadata parsing and reports active unmappable metadata rows.

Corrected audit output for the new capture:

```text
metadata_rows=88
metadata_mappable_rows=0
active_rows=88
active_metadata_unmappable_rows=88
top_active_metadata_unmappable=block=20,guest=8228233C,total=47409053,
  first_source=8228233C,last_source=82282370,
  first_comment=8228233C,last_comment=82282370,label=8228233C
join_status=unsafe
```

Old-log validation still reports the prior capture as unsafe and metadata-free.

## Decision

Do not patch `82281D28` behavior from weighted HIR/profile joins yet. The
metadata proves which source span is hot, but the HIR text needed for a safe
behavior decision is still missing from the filtered dump.

Next useful slice: emit file-backed or log-backed per-block HIR text/source
spans for active unmappable rows, starting with `82281D28` block `20`
(`8228233C-82282370`). The join audit should only allow behavior work after
that per-block text is available and `join_status` is no longer `unsafe`.
