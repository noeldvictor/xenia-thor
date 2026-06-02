<#
.SYNOPSIS
  Guarded perfetto GPU-timeline capture for xenia on the AYN Thor (Adreno 740).
  Captures power/gpu_frequency + power/gpu_work_period ftrace over a short window so
  we can see whether a slow frame is the GPU genuinely grinding (no gaps) vs stalled
  (bubbles between work periods), plus the GPU frequency timeline.

  Same thermal discipline as thor_evidence.ps1: refuses to launch hot/busy, monitors
  temp every 5s through the boot wait, and force-stops the instant temp crosses
  ThrashTempC. Always force-stops the emulator at the end.

.NOTES
  perfetto gpu_work_period gives per-process GPU-active time + freq, NOT the per-stage
  (binning/fragment/tile) split — that still needs Snapdragon Profiler / AGI.
#>
param(
    [string]$Adb = "C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    [string]$Device = "c3ca0370",
    [string]$Package = "jp.xenia.emulator.github.debug",
    [string]$Label = "perfetto",
    [int]$BootWaitSec = 120,
    [int]$PerfettoMs = 5000,
    [string]$Iso = "/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso",
    [string]$Seq = "start@20000:300;a@26000:300;start@32000:300;a@38000:300;start@45000:300;a@52000:300;start@60000:300;a@70000:300;start@82000:300;a@92000:300;start@102000:300;a@112000:300",
    [int]$MaxStartTempC = 60,
    [int]$MaxStartBusyPct = 40,
    [int]$ThrashTempC = 68
)
$ErrorActionPreference = "Continue"
$comp = "$Package/jp.xenia.emulator.EmulatorActivity"
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$evDir = Join-Path $repo "docs\evidence"
New-Item -ItemType Directory -Force -Path $evDir | Out-Null
$stamp = (& $Adb -s $Device shell "date +%Y%m%d-%H%M%S").Trim()
$base = Join-Path $evDir ("{0}-{1}" -f $stamp, ($Label -replace '[^A-Za-z0-9._-]','_'))
$txt = "$base.txt"; $png = "$base.png"; $pb = "$base.pb"

function AdbSh($cmd) { & $Adb -s $Device shell $cmd 2>&1 }
function W($s) { $s | Tee-Object -FilePath $txt -Append | Out-Null; Write-Output $s }
function GpuTempC { $t = (AdbSh "cat /sys/class/kgsl/kgsl-3d0/temp") -replace '[^0-9]',''; if ($t) { [int]([double]$t / 1000.0) } else { -1 } }
function GpuBusyPct { $b = (AdbSh "cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage") -replace '[^0-9]',''; if ($b) { [int]$b } else { -1 } }

"=== xenia-thor perfetto GPU capture ===" | Out-File $txt -Encoding utf8
W "label: $Label"; W "device_time: $stamp"; W "boot_wait: ${BootWaitSec}s  perfetto: ${PerfettoMs}ms  thrash_guard: ${ThrashTempC}C"

# preflight
$preTemp = GpuTempC; $preBusy = GpuBusyPct
W "preflight: gpu_temp=${preTemp}C gpu_busy=${preBusy}%  (limits: temp<${MaxStartTempC}C busy<${MaxStartBusyPct}%)"
if (($preTemp -ge $MaxStartTempC) -or ($preBusy -ge $MaxStartBusyPct)) {
    AdbSh "am force-stop $Package" | Out-Null
    W "ABORT: device too hot/busy to launch (temp=${preTemp}C busy=${preBusy}%)."
    Write-Output "ABORTED (preflight) temp=${preTemp}C busy=${preBusy}%"; exit 2
}

W "action: LAUNCH (fresh)"
AdbSh "am force-stop $Package" | Out-Null
AdbSh "logcat -c" | Out-Null
$intent = "am start -W -n $comp --es gpu vulkan --es cpu arm64 --es apu android --es hid nop --es hid_nop_button_sequence '$Seq' --ez arm64_enable_mini_jit true --ez android_hide_osd true --ez mount_cache true --ez vulkan_trace_draw_outcomes_per_frame true --es target '$Iso'"
W "launch_intent: $intent"
AdbSh $intent | Out-Null

# thermal-monitored boot wait
W "waiting ${BootWaitSec}s for heavy scene (thermal-monitored, abort at ${ThrashTempC}C)..."
$bootWaited = 0
while ($bootWaited -lt $BootWaitSec) {
    Start-Sleep -Seconds 5; $bootWaited += 5
    $tNow = GpuTempC; $bNow = GpuBusyPct
    if (($bootWaited % 20) -eq 0) { W "  t=${bootWaited}s temp=${tNow}C busy=${bNow}%" }
    if ($tNow -ge $ThrashTempC) {
        AdbSh "am force-stop $Package" | Out-Null
        W "THRASH GUARD: temp=${tNow}C >= ${ThrashTempC}C at ${bootWaited}s - force-stopped. ABORT."
        Write-Output "ABORTED (thermal, boot) temp=${tNow}C"; exit 2
    }
}

# confirm heavy scene + thermal headroom before the perfetto window
$tNow = GpuTempC; $bNow = GpuBusyPct
W "pre-capture: temp=${tNow}C busy=${bNow}%"
if ($tNow -ge $ThrashTempC) {
    AdbSh "am force-stop $Package" | Out-Null
    W "THRASH GUARD: temp=${tNow}C before capture - force-stopped. ABORT."
    Write-Output "ABORTED (thermal, pre-capture) temp=${tNow}C"; exit 2
}

# screenshot first so we can verify the scene even if capture is short
AdbSh "screencap -p /sdcard/_pf.png" | Out-Null
& $Adb -s $Device pull /sdcard/_pf.png $png 2>&1 | Out-Null
AdbSh "rm -f /sdcard/_pf.png" | Out-Null

# perfetto capture (short, from a safe temp)
$cfg = @"
buffers { size_kb: 131072 }
data_sources { config { name: "linux.ftrace" ftrace_config {
  ftrace_events: "kgsl/adreno_cmdbatch_queued"
  ftrace_events: "kgsl/adreno_cmdbatch_submitted"
  ftrace_events: "kgsl/adreno_cmdbatch_retired"
  ftrace_events: "kgsl/kgsl_gpubusy"
  ftrace_events: "kgsl/gpu_frequency"
  ftrace_events: "kgsl/kgsl_pwrlevel"
  ftrace_events: "kgsl/kgsl_waittimestamp_entry"
  ftrace_events: "kgsl/kgsl_waittimestamp_exit"
  ftrace_events: "kgsl/adreno_drawctxt_wait_start"
  ftrace_events: "kgsl/adreno_drawctxt_wait_done"
  ftrace_events: "kgsl/adreno_gpu_fault"
  ftrace_events: "kgsl/kgsl_mmu_pagefault" } } }
duration_ms: $PerfettoMs
"@
W "perfetto: capturing ${PerfettoMs}ms of gpu_work_period + gpu_frequency..."
# Push the config as an ASCII file (no BOM) instead of piping it through PowerShell's
# stdin, which encodes as UTF-16/BOM and makes perfetto's pbtxt parser choke
# ("Unexpected char").
$cfgLocal = "$base.cfg.txt"
$cfg | Out-File -FilePath $cfgLocal -Encoding ascii
& $Adb -s $Device push $cfgLocal /data/local/tmp/xenia_gpu.cfg 2>&1 | Out-Null
AdbSh "perfetto -c /data/local/tmp/xenia_gpu.cfg --txt -o /data/misc/perfetto-traces/xenia_gpu.pb" 2>&1 | ForEach-Object { W "  perfetto> $_" }
& $Adb -s $Device pull /data/misc/perfetto-traces/xenia_gpu.pb $pb 2>&1 | Out-Null
AdbSh "rm -f /data/misc/perfetto-traces/xenia_gpu.pb /data/local/tmp/xenia_gpu.cfg" | Out-Null

# the draw-outcomes line for this frame, for context
$log = (AdbSh "logcat -d") -join "`n"
$lastDraw = ($log -split "`n" | Select-String "GPU draw outcomes/frame" | Select-Object -Last 1).Line

# ALWAYS force-stop when done
AdbSh "am force-stop $Package" | Out-Null
$postTemp = GpuTempC; $postBusy = GpuBusyPct

W ""
W "--- RESULT ---"
W "trace_pb: $pb"
W "screenshot: $png"
W "last_draw_outcomes_line: $lastDraw"
W "post-capture (after force-stop): temp=${postTemp}C busy=${postBusy}%"
W "EVIDENCE_FILE: $txt"
Write-Output ""
Write-Output "DONE. Trace: $pb  Screenshot: $png  (emulator force-stopped; temp=${postTemp}C)"
