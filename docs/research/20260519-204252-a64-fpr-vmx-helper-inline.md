# A64 FPR/VMX Helper Inline

Date: 2026-05-19 20:42 EDT

Branch: `master`

Target: AYN Thor Max, Android native ARM64, Blue Dragon Disc 1.

## Question

After the GPR/LR helper inline and Blue Dragon host-counter draw-wait fastpath,
the late A64 speed profile still showed compiler ABI helper churn behind the
dominant `8246B408` wait loop:

- `__savevmx_124`
- `__restvmx_124`
- `__savefpr_27`
- `__restfpr_27`

Can the current A64 backend inline the FPR and VMX save/restore helpers safely
enough to remove them from the Thor speed profile?

## Implementation

Added A64 helper recognition for:

- `__savefpr_14..31`
- `__restfpr_14..31`
- `__savevmx_14..31`
- `__restvmx_14..31`
- `__savevmx_64..127`
- `__restvmx_64..127`

Rules:

- Tail calls are rejected and fall back to the normal call path.
- Helper names must match the expected prolog/epilog behavior metadata.
- FPR helpers copy 64-bit FP context slots to/from guest stack slots with a
  byte reversal around guest memory.
- VMX helpers copy 128-bit context slots to/from guest stack slots with 32-bit
  lane byte reversal, align the guest effective address to 16 bytes, and set
  guest `r11` to `-16` after VMX helpers to mirror the compiler stub side
  effect.
- `a64_inline_fpr_helpers` and `a64_inline_vmx_helpers` are routed through
  Android intent extras and the Thor debug script. The legacy script knob
  `-A64InlineFprVmxHelpers` remains as an alias that sets both split knobs.
- Both C++ cvars are default-on for fresh configs, and the Thor debug script
  now passes both extras as `true` by default so stale device configs do not
  silently disable the speed path.

Important bug found during bring-up:

- The first version subtracted the stack offset after converting the 32-bit
  guest address to a host pointer.
- That is wrong for wrapped guest addressing. Generic HIR memory lowering first
  computes the 32-bit guest effective address, then adds membase.
- The fixed helper path subtracts from guest `w9`, then calls
  `AddGuestAddressToMembase(w9, x9)`.

## Validation

Build and deploy checks:

- `git diff --check`
- PowerShell parser check for `tools\thor\thor_xenia_debug.ps1`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`

Baseline, helpers off:

- Capture: `scratch/thor-debug/20260519-201721-*`
- APK SHA-256:
  `2B073C2AC40C9123B360994973224C015B3DC947B6BFB1799258D288620D8EEF`
- Result: reached Microsoft Game Studios scene.
- Late profile still showed:
  `__savevmx_124`, `__restvmx_124`, `__savefpr_27`, `__restfpr_27`.

Bad pre-fix probes:

- FPR-only capture: `scratch/thor-debug/20260519-202108-*`
- VMX-only capture: `scratch/thor-debug/20260519-202453-*`
- Result: both parked on black after early boot with no fatal signal. This was
  traced to doing stack offset subtraction after membase conversion.

Fixed split probes:

- VMX-only capture: `scratch/thor-debug/20260519-203233-*`
- FPR-only capture: `scratch/thor-debug/20260519-203448-*`
- APK SHA-256:
  `5977CC990F4F53F414188AEE34CAEDD71FDDD7464064E95962403487CAD2CAA3`
- Result: both stayed active and reached the Blue Dragon loading spinner route.

Fixed combined probe:

- Capture: `scratch/thor-debug/20260519-203701-*`
- APK SHA-256:
  `5977CC990F4F53F414188AEE34CAEDD71FDDD7464064E95962403487CAD2CAA3`
- Command shape:
  `LaunchBlueDragonSpeedCapture` with
  `-A64InlineFprHelpers true -A64InlineVmxHelpers true
  -A64InlinePpcThreadFieldLeafHelpers true
  -Arm64BlueDragonDrawWaitProbe true
  -Arm64BlueDragonDrawWaitFastpath true
  -Arm64BlueDragonDrawWaitFastpathHostCounterTime true
  -MountCache true`.
- Result: reached the Microsoft Game Studios opening scene.
- No fatal signal, AndroidRuntime crash, `VK_ERROR_DEVICE_LOST`,
  GPU-hung marker, tombstone, or ANR marker was found in the proof capture.
- Final thread sample was CPU-heavy: main thread about 96%, several XThreads
  high, SoundThread high, GPU Commands about 3.8%, GPU VSync about 7.6%.
- The helper names disappeared from late top A64 profile entries.

Default/config caveat:

- After flipping the C++ defaults, capture `scratch/thor-debug/20260519-204503-*`
  launched without explicit FPR/VMX extras and parked on black with
  `entry_delta=0` after early boot.
- `run-as jp.xenia.emulator.github.debug cat files/xenia.config.toml` showed
  the device already had persisted config values:
  `a64_inline_fpr_helpers = false` and `a64_inline_vmx_helpers = false`.
- Conclusion: default-on C++ values help fresh configs, but the Thor debug loop
  must pass the extras explicitly for reproducible speed runs on this device.

Script-default proof after the caveat:

- Capture: `scratch/thor-debug/20260519-205129-*`
- APK SHA-256:
  `AA97C4E1761ED2D6974755845021A0F85E7B0BB270E90C4C24CB2BFA949FE453`
- Launched without manually passing `-A64InlineFprHelpers` or
  `-A64InlineVmxHelpers`.
- Metadata recorded `a64_inline_fpr_helpers=true` and
  `a64_inline_vmx_helpers=true`, proving the script defaults override the stale
  persisted Android config.
- Screenshot reached the Blue Dragon loading spinner.
- The filtered speed profile did not contain `__savefpr`, `__restfpr`,
  `__savevmx`, or `__restvmx`.
- `8246B408` remained the top recurring guest PC, with GPU Commands also hot in
  this short loading-window sample. Longer comparable 180s captures are still
  needed before making a GPU-vs-CPU phase claim.

## Finding

This is a good default-on speed cleanup for the current A64 backend: the helper
families no longer show up as late profile hot entries on the Blue Dragon route,
and separate plus combined Thor probes survived.

It is not the title-screen fix. The wall is still CPU/A64, centered on
`8246B408` and the caller/producer around it. The final combined interval still
reported `8246B408` as top entry with `delta=104749562`.

## Next Experiment

Stay off broad GPU work for now. The GPU command thread is not the top wall in
these captures.

Next useful OODA loop:

1. Generate focused PPC disassembly for `8246B408`, its nearby callers, and the
   visible producer candidates:
   `8246A928`, `82473D78`, `82474388`, `8248B040`.
2. Build a tiny static/runtime queue around what `8246B408` waits on: token,
   timer, event, queue, or draw-completion value.
3. Add a cheap targeted counter/probe for that value instead of more generic
   helper work.

Rollback:

- Launch with `-A64InlineFprHelpers false -A64InlineVmxHelpers false`.
- Or temporarily set `a64_inline_fpr_helpers` / `a64_inline_vmx_helpers` back
  to false in `src/xenia/cpu/backend/a64/a64_emitter.cc`.
