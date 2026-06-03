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
  [string]$OutDir = "scratch\thor-debug"
)
$ErrorActionPreference = "Stop"
$adb = "C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe"
$pkg = "jp.xenia.emulator.github.debug"
$iso = '/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'
$seq = 'start@20000:300;a@26000:300;start@32000:300;a@38000:300;start@45000:300;a@52000:300;start@60000:300;a@70000:300;start@82000:300;a@92000:300;start@102000:300;a@112000:300'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# 0. Never-thrash gate.
& $adb -s $DeviceSerial reconnect | Out-Null; Start-Sleep -Seconds 1
$pid0 = "$(& $adb -s $DeviceSerial shell pidof $pkg)".Trim()
if ($pid0) { Write-Output "ABORT: emulator already running (pid $pid0); force-stop first."; exit 1 }
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
$dumpArgs = "--ez vulkan_trace_dump_rt_image true --ez vulkan_trace_dump_depth_image true --ez vulkan_trace_edram_checksum true --ei vulkan_trace_edram_checksum_budget 9000 --ez vulkan_trace_draw_outcomes_per_frame true"

# 2. Wake + clear log + launch.
& $adb -s $DeviceSerial shell input keyevent KEYCODE_WAKEUP
& $adb -s $DeviceSerial shell svc power stayon true
& $adb -s $DeviceSerial logcat -G 64M
& $adb -s $DeviceSerial logcat -c
$cmd = "am start -W -n $pkg/jp.xenia.emulator.EmulatorActivity $drvArgs --es cpu arm64 --es apu android --es hid nop --es hid_nop_button_sequence '$seq' --ez arm64_enable_mini_jit true --ez android_hide_osd true --ez mount_cache true $dumpArgs --es target '$iso'"
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
$log = Join-Path $OutDir "$($Driver)_logcat.txt"
$png = Join-Path $OutDir "$($Driver)_frame.png"
if (-not $hot) {
  (& $adb -s $DeviceSerial logcat -d) | Out-File -Encoding utf8 $log
  & $adb -s $DeviceSerial shell screencap -p /sdcard/thor_gpu_capture.png
  & $adb -s $DeviceSerial pull /sdcard/thor_gpu_capture.png $png | Out-Null
  Write-Output "=== draw outcomes / RT IMAGE / DEPTH IMAGE (tail) ==="
  & $adb -s $DeviceSerial shell "logcat -d | grep -E 'GPU draw outcomes|dump RT IMAGE checksum|dump DEPTH IMAGE checksum|Loaded Turnip|No Vulkan physical' | tail -8"
}
& $adb -s $DeviceSerial shell am force-stop $pkg
& $adb -s $DeviceSerial shell svc power stayon false
$tempF = [int]("0$(& $adb -s $DeviceSerial shell cat /sys/class/kgsl/kgsl-3d0/temp)".Trim())
Write-Output ("final temp={0}C" -f ($tempF/1000.0))
Write-Output "logcat: $log    screenshot: $png"
Write-Output "inspect: python tools\thor\thor_inspect_capture.py $log"
