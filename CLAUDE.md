# CLAUDE.md, xenia-thor

Read `AGENTS.md` first. It is the single instruction file for every agent and holds all rules,
the sources, and the current state. The content of this file moved there on 2026-09-20.

Three rules that apply before `AGENTS.md` is read:

- Write in ASD-STE100 Simplified Technical English and follow the writing principle
  (`AGENTS.md` section 1). Update `AGENTS.md` when a rule or the state changes.
- Use the xenia-thor MCP tools for every device action (`AGENTS.md` section 9).
- Never commit game files, keys, dumps, screenshots, or config backups.

## The port is a multi-axis paradigm change (user, 2026-09-20)

xenia assumes x86-64, TSO memory, SSE/AVX, a desktop GPU with its own VRAM, Windows scheduling,
and a desktop build. The Thor changes every one of those at once. The bugs and the lost
performance come from code that still holds the old assumption on one axis. So the method is:
name the axes, write the desktop assumption on each, and for every bug first ask which axis it
crosses. Full table and the open items: `docs/research/20260920-port-paradigm-matrix.md`.

| axis | desktop assumption | the Thor |
|---|---|---|
| CPU ISA | two-operand, 16 GPRs, AVX2 | three-operand, 31 GPRs, NEON 128-bit |
| memory model | TSO | weak ordering; missing fences race |
| host SIMD | SSE intrinsics | NEON, sse2neon shims differ on rounding and denormals |
| GPU architecture | immediate-mode dGPU, cheap bandwidth | binning tiler with GMEM, UMA, expensive pass breaks |
| Xenos model | EDRAM flattened into one big buffer | GMEM is the natural EDRAM analogue |
| driver | NVIDIA/AMD | Turnip, strict, weekly builds |
| OS | Windows threads, timers, one CPU type | Android, 1+4+3 cores, thermal throttling near 95 C |
| memory manager | VirtualAlloc, no limits | Scudo, `vm.max_map_count` 65,530 |
| storage, present, input, build | NVMe, exclusive fullscreen, XInput, MSVC | flash and content URIs, SurfaceFlinger and a sleeping panel, gamepad events, NDK |
