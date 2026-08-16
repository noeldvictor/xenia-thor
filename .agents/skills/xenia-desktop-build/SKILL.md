---
name: xenia-desktop-build
description: Reliably build + run the xenia DESKTOP Vulkan exe on this Windows machine, working around the Defender-quarantine, 32-bit-linker mmap-wedge, MSBuild relink-skip, corrupt-PDB, and space-in-path traps that cost hours on 2026-07-07. Use whenever building/running xenia.exe on desktop (native HLE iteration, RenderDoc, screenshot-on-PC).
---

# Building & running the xenia desktop exe (Windows) — the reliable procedure

The desktop build loop is the FAST iteration path (~1min incremental vs 13-min Thor cycles) for the BD native
HLE. But this machine has FIVE traps that silently break it. Do these in order.

## 0. One-time environment prep (or when the build starts failing)

**A. Windows Defender WILL quarantine `xenia.exe`** (Xbox emulator → PUA false-positive). Symptom: the link log
says `xenia-app.vcxproj -> ...xenia.exe` but the file is GONE ~1s later (or has a zeroed PE header). **This CANNOT
be fixed from PowerShell if Tamper Protection is on** — `Add-MpPreference`/`Set-MpPreference` silently no-op.

Check status (non-admin OK):
```powershell
$s = Get-MpComputerStatus; "RealTime=$($s.RealTimeProtectionEnabled) Tamper=$($s.IsTamperProtected)"
```
If `RealTime=True`, the USER must fix it in the **GUI** (Tamper Protection blocks the cmdlets):
- Windows Security → Virus & threat protection → **Manage settings** → **Tamper Protection = Off** → then
  **Real-time protection = Off** (or add **Exclusions → Folder →** `...\xenia-thor\build`).
- Re-verify `RealTime=False` before building. Ask the user; you cannot do this yourself (needs admin + GUI).
- An exe quarantined DURING a build stays quarantined even after you disable Defender → you must REBUILD.

**B. Kill zombie build processes** (they hold file locks → pdb corruption + linker wedge):
```powershell
Get-Process link,MSBuild,cl,mspdbsrv -ErrorAction SilentlyContinue | Stop-Process -Force
```

**C. Disable the fatal LNK4020** (corrupt-PDB warning-as-error) in the generated vcxproj (build artifact, safe to
edit; premake regenerates):
```powershell
$v='...\xenia-thor\build\xenia-app.vcxproj'; (Get-Content $v -Raw) -replace '<TreatLinkerWarningAsErrors>true','<TreatLinkerWarningAsErrors>false' | Set-Content $v -Encoding UTF8
```

## 1. Build (ALWAYS force the 64-bit linker)

The 32-bit linker WEDGES on `xenia-kernel.lib` ("failed to do memory mapped file I/O", CPU flatlines, exe locked
20+ min). **`/p:PreferredToolArchitecture=x64` forces the 64-bit linker and fixes it.**

```
MSBuild="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
"$MSBuild" build\xenia-app.vcxproj /p:Configuration="Release Windows" /p:Platform=x64 /p:PreferredToolArchitecture=x64 /v:minimal /nologo
```

- **Do NOT manually `rm` the exe then rebuild** — MSBuild tracks build STATE not output existence, sees "up to
  date", prints `-> xenia.exe` WITHOUT linking → no exe. To force a real link after editing nothing / after a
  delete: **touch a source** (`printf '\n// %s\n' $(date +%s) >> src\xenia\gpu\vulkan\bd_native_renderer.cc`) or
  use `/t:Rebuild`.
- The link is slow (I/O-bound, several min) — run it in the background and wait for the `-> xenia.exe` line.

## 2. Verify the exe actually survived + is valid (don't assume)

```python
import struct,os; p=r'...\build\bin\Windows\Release\xenia.exe'
f=open(p,'rb'); d=f.read(0x400); e=struct.unpack('<I',d[0x3c:0x40])[0]; f.seek(e)
print('PE VALID:', f.read(4)==b'PE\x00\x00', os.path.getsize(p))
```
If MISSING → Defender ate it (step 0A). If PE invalid/zeroed → Defender corrupted it mid-link (step 0A) or the
32-bit linker (step 1). Back it up immediately: `cp xenia.exe $CLAUDE_JOB_DIR/tmp/xenia_backup.exe`.

## 3. Run BD to the field on desktop

- **Quote the ISO path** (it has a SPACE: "New project 8"). In PowerShell `Start-Process -ArgumentList`, pass the
  ISO as its OWN element (a bare string with spaces truncates → "Unable to mount STFS container").
- BD ISO on desktop: `scratch\blue-dragon\bd_disc1.iso`. It reaches the gameplay field via the HID-nop nav in
  `scratch\blue-dragon\run_field.ps1` (alternate `start`/`a` to ~138s then `a` to ~168s; field by ~150s).
- Desktop logs reliably to `--log_file=<path>` (NO logcat rotation — grep it directly for your diagnostics).
- Native HLE flags: `--gpu=vulkan --gpu_bd_native_renderer=true [--gpu_bd_native_aux_rt=true ...]`.

## 4. Capture a screenshot of the (occludable, GPU-composited) xenia window

`CopyFromScreen` grabs whatever's on top; `MainWindowHandle` is often 0. Use WIN32 `PrintWindow(hwnd, dc, 2)`
(PW_RENDERFULLCONTENT) on the window whose title contains "Blue Dragon" (find via `EnumWindows`) — captures the
Vulkan window content regardless of focus/occlusion. See the capture helper in `scratch/thor-debug/bd_auxtest.ps1`
history / `run_field.ps1`.

## 5. RenderDoc (for per-draw NDC / viewport / RT analysis)

Skill `xenia-renderdoc-replay`, env `tools/renderdoc/`. Trigger an in-app capture with
`--gpu_bd_renderdoc_capture_frame=N` under `renderdoccmd`, then analyze per-draw (post-VS NDC, RTs, viewports)
headless: `qrenderdoc.exe --python tools/renderdoc/rd_analyze.py`. This is how you resolve "geometry lands where the
registers don't predict" (e.g. the BD field tile-placement / central-strip collapse).

## Quick failure → cause map
- exe MISSING after successful build → Defender quarantine (0A) — needs GUI, Tamper Protection off.
- link.exe stuck 20+ min, low CPU, exe locked → 32-bit linker mmap wedge → `/p:PreferredToolArchitecture=x64` (1).
- build prints `-> xenia.exe` but no exe + only ~40 log lines → MSBuild skipped relink (touch a source) (1).
- `LNK1218: warning treated as error` → TreatLinkerWarningAsErrors (0C).
- "Unable to mount STFS container … \New" → ISO path truncated at the space (3).
