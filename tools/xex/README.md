# Device-free XEX → PPC disassembly (Ghidra-free guest RE)

A standalone pipeline to decompress an Xbox 360 `default.xex` and disassemble guest
PowerPC at any guest address — **no Ghidra, no device execution**. Built to localize
hot/stuck guest code (e.g. Lost Odyssey's loading-screen spin at `0x827B6278`).

Replicates `src/xenia/cpu/xex_module.cc` exactly: XEX2 header parse → AES-128 key
derive (retail master key) → AES-128-CBC image decrypt → de-block → **basic OR LZX**
decompress → capstone PPC (`CS_ARCH_PPC`, big-endian, 32-bit).

## Tools
- `xex_disasm.py <file.xex> <guest_hex_addr> [n_insts]` — decompress + disassemble.
  Handles basic (comp=1), uncompressed (comp=0), and LZX (comp=2, via `xexlzx.exe`).
- `scan_stores.py <file.xex> <disp_hex> [more...]` — scan the whole image for D-form
  loads/stores (`lwz/stw/stb/...`) whose 16-bit displacement matches. Localizes the
  `.text` accessors of a `.bss`/struct field by its low-16 displacement when a static
  absolute xref fails (the access is `disp(rBase)` with `rBase` set elsewhere). Reuses
  `xex_disasm.load_xex()`.
- `scan_bl.py <file.xex> <target_hex> [more...]` — scan for I-form branches (`b/bl/ba/bla`)
  whose computed target equals a guest address — i.e. find every caller/jumper to a
  function (a device-free xref/callgraph probe).
- `xexlzx.c` — standalone LZX-stream decompressor (libmspack `lzxd.c` + xenia's memory
  harness). Build once:
  ```
  cl /O2 /I third_party\mspack tools\xex\xexlzx.c third_party\mspack\lzxd.c \
     third_party\mspack\system.c /Fe:tools\xex\xexlzx.exe /Fotools\xex\obj\
  ```
  (run under a VS BuildTools `vcvars64.bat` shell). `.exe` is a build artifact — gitignored.
- `remote_xex_pull.py <device_iso_path> <out.xex> [serial]` — extract just `default.xex`
  from a device-resident GDFX ISO over `adb exec-out dd` (random-access, never pulls the
  multi-GB image). Reuses the skill's `gdfx_extract` tree walk.

## Deps
`pip` libs: `cryptography` (AES), `capstone` (PPC). MSVC (for `xexlzx.exe`).

## Example (Lost Odyssey)
```
python tools/xex/remote_xex_pull.py "/storage/.../Lost Odyssey ... (Disc 1).iso" scratch/lo.xex
python tools/xex/xex_disasm.py scratch/lo.xex 0x827B6278 48     # the loading-screen spin
python tools/xex/xex_disasm.py scratch/lo.xex 0x827B6C48 40     # its graphics interrupt callback
```
Validated: BD entry `0x824669e0` (basic) + LO entry `0x827ca440` (LZX) both decode to clean
PPC prologues. See worklog B86oo and memory `lost-odyssey-spin-diagnosis`.
