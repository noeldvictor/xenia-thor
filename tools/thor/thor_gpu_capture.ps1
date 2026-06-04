# thor_gpu_capture.ps1 - Lane A GPU frame capture for xenia-thor on the AYN Thor.
# Captures the in-tree attachment-readback + draw-outcome trace for one BD frame
# (works on Turnip or the system driver), thermal-gated, one launch. Then inspect
# with: python tools\thor\thor_inspect_capture.py <out>\<driver>_logcat.txt
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\thor\thor_gpu_capture.ps1 -Driver turnip
#   powershell -ExecutionPolicy Bypass -File tools\thor\thor_gpu_capture.ps1 -Driver system
param(
  [string]$DeviceSerial = "c3ca0370",
  [ValidateSet("turnip","system")][string]$Driver = "turnip",
  [int]$DurationSec = 150,
  [string]$OutDir = "scratch\thor-debug",
  # Optional Turnip TU_DEBUG flags (set via the gpu_vulkan_driver_debug cvar),
  # e.g. -TuDebug sysmem (force direct rendering, bypass GMEM tiling) or nolrz.
  # Only meaningful with -Driver turnip. Tags the output files so a plain-turnip
  # reference is not clobbered.
  [string]$TuDebug = "",
  # Optional Turnip ir3-compiler flags (set via gpu_vulkan_driver_ir3_debug =
  # IR3_SHADER_DEBUG), e.g. -Ir3Debug nofp16 (disable fp16/mediump lowering),
  # noopt, nocp. Only meaningful with -Driver turnip. Also tags the output files.
  [string]$Ir3Debug = "",
  # Arbitrary extra launch extras appended verbatim to the am start command, e.g.
  # '--ez spirv_debug_force_fullscreen_position true --ei vulkan_debug_pixel_shader_output_mode 1'.
  # Use -Tag to label the output files for the experiment.
  [string]$ExtraArgs = "",
  [string]$Tag = "",
  # Skip the RT/depth/EDRAM readback dump cvars (which add a vkCmdCopyImageToBuffer
  # every resolve) and keep only the cheap draw-outcomes line - for clean fps /
  # gpu_frame_us perf measurement that isn't inflated by the diagnostic readbacks.
  [switch]$NoDump,
  # Capture per-THREAD CPU (top -H) during the heavy scene to see whether a single
  # guest/JIT thread is pegged (~100% of one core = throughput-bound) or no thread
  # is pegged (= latency/serialization-bound). Saves <label>_top.txt.
  [switch]$TopProfile,
  # Game ISO to launch (default Blue Dragon). Pass the full device path of another
  # title (e.g. a Lost Odyssey disc) to capture it instead.
  [string]$Iso = '/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso',
  # Intro-skip button sequence (hid_nop_button_sequence): mashes start/a to push
  # past intros into a steady scene. Default tuned for Blue Dragon; override per
  # title.
  [string]$Seq = 'start@20000:300;a@26000:300;start@32000:300;a@38000:300;start@45000:300;a@52000:300;start@60000:300;a@70000:300;start@82000:300;a@92000:300;start@102000:300;a@112000:300'
)
$ErrorActionPreference = "Stop"
$adb = "C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe"
$pkg = "jp.xenia.emulator.github.debug"
$iso = $Iso
$seq = $Seq
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# 0. Never-thrash gate.
& $adb -s $DeviceSerial reconnect | Out-Null; Start-Sleep -Seconds 1
$pid0 = "$(& $adb -s $DeviceSerial shell pidof $pkg)".Trim()
if ($pid0) {
  # The Thor frontend (launcher) auto-restarts after a force-stop, so don't abort
  # on a running instance - force-stop it (launcher or a previous capture) and
  # proceed. Thermal safety is still enforced by the temp pre-flight + watchdog.
  Write-Output "force-stopping running instance (pid $pid0) before capture"
  & $adb -s $DeviceSerial shell am force-stop $pkg
  Start-Sleep -Seconds 2
}
$temp0 = [int]("0$(& $adb -s $DeviceSerial shell cat /sys/class/kgsl/kgsl-3d0/temp)".Trim())
Write-Output ("pre-flight temp={0}C" -f ($temp0/1000.0))
if ($temp0 -eq 0) { Write-Output "ABORT: could not read GPU temp (device offline?)."; exit 1 }
if ($temp0 -ge 55000) { Write-Output "ABORT: GPU too hot (>=55C); let it cool."; exit 1 }

# 1. Turnip cvars (re-derive hooks_path - it changes on every install).
$drvArgs = "--es gpu vulkan"
if ($Driver -eq "turnip") {
  $base = ((& $adb -s $DeviceSerial shell pm path $pkg) -replace '^package:','' -replace '/base\.apk\s*$','').Trim()
  $hooks = "$base/lib/arm64"
  Write-Output "hooks_path=$hooks"
  $drvArgs = "--es gpu vulkan --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path /data/data/$pkg/files/turnip/ --es gpu_vulkan_driver_lib vulkan.ad07xx.so --es gpu_vulkan_driver_hooks_path '$hooks'"
}
if ($NoDump) {
  # Clean perf run: only the cheap per-frame draw-outcomes line, no readbacks.
  $dumpArgs = "--ez vulkan_trace_draw_outcomes_per_frame true"
} else {
  $dumpArgs = "--ez vulkan_trace_dump_rt_image true --ez vulkan_trace_dump_depth_image true --ez vulkan_trace_edram_checksum true --ei vulkan_trace_edram_checksum_budget 9000 --ez vulkan_trace_draw_outcomes_per_frame true"
}

# Optional Turnip TU_DEBUG / IR3_SHADER_DEBUG flags + output label so tagged runs
# are saved apart from the plain reference.
$dbgArgs = ""
$label = $Driver
if ($TuDebug) {
  $dbgArgs += " --es gpu_vulkan_driver_debug $TuDebug"
  $label += "_$($TuDebug -replace '[^A-Za-z0-9]','')"
  Write-Output "TU_DEBUG=$TuDebug"
}
if ($Ir3Debug) {
  $dbgArgs += " --es gpu_vulkan_driver_ir3_debug $Ir3Debug"
  $label += "_ir3$($Ir3Debug -replace '[^A-Za-z0-9]','')"
  Write-Output "IR3_SHADER_DEBUG=$Ir3Debug"
}
if ($ExtraArgs) {
  $dbgArgs += " $ExtraArgs"
  Write-Output "ExtraArgs=$ExtraArgs"
}
if ($Tag) { $label += "_$($Tag -replace '[^A-Za-z0-9]','')" }
if ($dbgArgs) { Write-Output "label=$label" }

# 2. Wake + clear log + launch.
& $adb -s $DeviceSerial shell input keyevent KEYCODE_WAKEUP
& $adb -s $DeviceSerial shell svc power stayon true
& $adb -s $DeviceSerial logcat -G 64M
& $adb -s $DeviceSerial logcat -c
$cmd = "am start -W -n $pkg/jp.xenia.emulator.EmulatorActivity $drvArgs $dbgArgs --es cpu arm64 --es apu android --es hid nop --es hid_nop_button_sequence '$seq' --ez arm64_enable_mini_jit true --ez android_hide_osd true --ez mount_cache true $dumpArgs --es target '$iso'"
& $adb -s $DeviceSerial shell $cmd | Out-Null

# 3. Watchdog (64C gate, poll 10s).
$hot = $false
for ($t = 10; $t -le $DurationSec; $t += 10) {
  Start-Sleep -Seconds 10
  $temp = [int]("0$(& $adb -s $DeviceSerial shell cat /sys/class/kgsl/kgsl-3d0/temp)".Trim())
  $busy = "$(& $adb -s $DeviceSerial shell cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage)".Trim()
  Write-Output ("t={0}s temp={1}C busy={2}" -f $t, ($temp/1000.0), $busy)
  if ($temp -ge 64000) { Write-Output "WATCHDOG: >=64C, force-stop"; & $adb -s $DeviceSerial shell am force-stop $pkg; $hot = $true; break }
}

# 4. Capture logcat (text, utf8) + screenshot (binary via /sdcard+pull), then stop.
$log = Join-Path $OutDir "$($label)_logcat.txt"
$png = Join-Path $OutDir "$($label)_frame.png"
if (-not $hot) {
  (& $adb -s $DeviceSerial logcat -d) | Out-File -Encoding utf8 $log
  & $adb -s $DeviceSerial shell screencap -p /sdcard/thor_gpu_capture.png
  & $adb -s $DeviceSerial pull /sdcard/thor_gpu_capture.png $png | Out-Null
  Write-Output "=== draw outcomes / RT IMAGE / DEPTH IMAGE (tail) ==="
  & $adb -s $DeviceSerial shell "logcat -d | grep -E 'GPU draw outcomes|dump RT IMAGE checksum|dump DEPTH IMAGE checksum|Loaded Turnip|No Vulkan physical' | tail -8"
  if ($TopProfile) {
    $topf = Join-Path $OutDir "$($label)_top.txt"
    Write-Output "=== per-thread CPU: top -H -b -n 2 -d 1 -m 22 (2nd iteration = real %CPU) ==="
    $topout = (& $adb -s $DeviceSerial shell "top -H -b -n 2 -d 1 -m 22")
    $topout | Out-File -Encoding utf8 $topf
    $topout | ForEach-Object { Write-Output $_ }
    Write-Output "top saved: $topf"
  }
}
& $adb -s $DeviceSerial shell am force-stop $pkg
& $adb -s $DeviceSerial shell svc power stayon false
$tempF = [int]("0$(& $adb -s $DeviceSerial shell cat /sys/class/kgsl/kgsl-3d0/temp)".Trim())
Write-Output ("final temp={0}C" -f ($tempF/1000.0))
Write-Output "logcat: $log    screenshot: $png"
Write-Output "inspect: python tools\thor\thor_inspect_capture.py $log"
