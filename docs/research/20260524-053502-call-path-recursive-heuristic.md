# 2026-05-24 05:35 EDT - Call-Path Recursive Heuristic

## Question

After the `82281D28` call-edge split, the next decision is whether recursive
call-heavy blocks should be treated as local generated-code targets. The
manual math was useful but too easy to repeat by hand. Can the existing
call-path report compute recursive edge coverage and parent-exclusive estimates
directly from the same quiet logs?

## Tool Change

`tools/thor/thor_hir_call_path_report.ps1` now parses function-level body-time
rows from the supplied `-BlockProfileLog` and prints a
`Dynamic Recursive/Exclusive Heuristic` section:

- final function body total, delta, entries, ticks per entry, and code size;
- total dynamic direct-edge body time and share of the function body;
- self-recursive edge body time/calls and share of the function body;
- approximate parent-exclusive ticks after reported direct edges;
- per-target body share table.

This is an attribution aid only. It does not change emulator behavior and it
does not prove a speed win.

## Validation

Commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_call_path_report.ps1 -LogPath scratch\thor-debug\20260524-052031-speed-logcat.txt -Function 82281D28 -BlockProfileLog scratch\thor-debug\20260524-052031-speed-logcat.txt -Top 20
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_call_path_report.ps1 -LogPath scratch\thor-debug\20260524-030450-speed-logcat.txt -Function 82282490 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 12
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_call_path_report.ps1 -LogPath scratch\thor-debug\20260523-152754-speed-logcat.txt -Function 8227F1D8 -BlockProfileLog scratch\thor-debug\20260523-152754-speed-logcat.txt -Top 8
```

Results:

- `82281D28`: function body `10212419`; dynamic edge body `9096992`
  (`89.08%`); self-recursive body `7350722` over `69445` calls (`71.98%`);
  approximate parent-exclusive body `1115427` (`10.92%`).
- `82282490`: function body `101054711`; dynamic edge body `84969304`
  (`84.08%`); self-recursive body `72194274` over `379570` calls (`71.44%`);
  approximate parent-exclusive body `16085407` (`15.92%`).
- `8227F1D8`: function body `4117139`; dynamic edge body `4054641`
  (`98.48%`) to `82490030`; approximate parent-exclusive body `62498`
  (`1.52%`).

## Decision

The recursive SCC read is now deterministic enough to use before patching a
call-heavy block. For this route, `82281D28` should stay off the local codegen
patch list: it is mostly recursive callee body. `82282490` is still the larger
wall, but its dominant self-recursive edge also means the next patch should not
target generic call/prolog overhead. Entry/exit profiling already showed that
overhead is tiny.

Use the updated call-path report first when a new call-edge capture includes
body-time rows. Then pick codegen experiments from body-backed local/exclusive
spans such as the previously mapped `8228252C-822825C4` area, not from
inclusive recursive block totals alone.
