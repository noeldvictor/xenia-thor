<#
.SYNOPSIS
  Evidence-capture harness for xenia-thor performance claims. EVERY fps/GPU number
  must come out of a saved evidence file produced by THIS script - never typed by hand.

  It launches (or attaches to) Blue Dragon on the AYN Thor, waits to the heavy scene,
  then over a measurement window records to a single timestamped evidence file:
    - raw VdSwap timestamps -> fps (computed from the count, shown with the raw count)
    - Adreno KGSL gpu_busy_percentage + clock_mhz samples
    - the per-frame "GPU draw outcomes/frame" lines (gpu_frame_us, rendered, etc.)
    - a screenshot pulled to <evidence>.png (so the visual is verifiable too)
    - the exact launch config (cvars) used
  It prints a SUMMARY block whose numbers are computed from the captured data, and
  writes that same summary into the evidence file. To cite a number, quote the file.

.NOTES
  This exists because fabricated numbers were committed twice (see memory
  no-fabrication-autonomous-failure). Rule: no perf number in a worklog/commit unless
  it appears in a docs/evidence/*.txt file this script wrote. docs/evidence/ is gitignored
  (raw captures); copy the SUMMARY lines into the worklog with the evidence filename.

.EXAMPLE
  # capture baseline on the heavy scene (launch fresh, 130s to scene, 15s window)
  tools\thor\thor_evidence.ps1 -Label baseline -BootWaitSec 130 -WindowSec 15

  # capture with a cvar set, attaching to an already-running session (no relaunch)
  tools\thor\thor_evidence.ps1 -Label skip16 -Attach -SetCvar gpu_skip_draws_below_verts=16 -WindowSec 12
#>
param(
    [string]$Adb = "C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    [string]$Device = "c3ca0370",
    [string]$Package = "jp.xenia.emulator.github.debug",
    [Parameter(Mandatory = $true)][string]$Label,
    [switch]$Attach,                       # attach to running session instead of launching
    [string]$SetCvar = "",                 # name=value, applied live via SET_CVAR broadcast before the window
    [int]$BootWaitSec = 130,               # wait after launch to reach the heavy scene (ignored with -Attach)
    [int]$WindowSec = 15,                  # measurement window length
    [string]$Iso = "/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso",
    [string]$Seq = "start@20000:300;a@26000:300;start@32000:300;a@38000:300;start@45000:300;a@52000:300;start@60000:300;a@70000:300;start@82000:300;a@92000:300;start@102000:300;a@112000:300",
    # THERMAL/THRASH GUARD (NEVER let the Thor thrash again). A fresh launch is
    # REFUSED unless the GPU is cool+idle. Tunables are deliberately conservative.
    [int]$MaxStartTempC = 60,        # do not launch if GPU temp >= this
    [int]$MaxStartBusyPct = 40,      # do not launch if GPU already this busy
    [int]$CooldownWaitSec = 180,     # wait up to this long for it to cool, polling gently
    [int]$ThrashTempC = 68,          # DEFENSIVE: force-stop the emulator if temp hits this (BELOW the ~72C that crashed it once)
    [switch]$Force                   # override the guard (explicit opt-in only)
)

$ErrorActionPreference = "Continue"
$comp = "$Package/jp.xenia.emulator.EmulatorActivity"
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$evDir = Join-Path $repo "docs\evidence"
New-Item -ItemType Directory -Force -Path $evDir | Out-Null
# timestamp from the DEVICE (Date.now is unavailable to the harness; use device clock)
$stamp = (& $Adb -s $Device shell "date +%Y%m%d-%H%M%S").Trim()
$base = Join-Path $evDir ("{0}-{1}" -f $stamp, ($Label -replace '[^A-Za-z0-9._-]','_'))
$txt = "$base.txt"
$png = "$base.png"

function AdbSh($cmd) { & $Adb -s $Device shell $cmd 2>&1 }
function W($s) { $s | Tee-Object -FilePath $txt -Append | Out-Null; Write-Output $s }
function GpuTempC { $t = (AdbSh "cat /sys/class/kgsl/kgsl-3d0/temp") -replace '[^0-9]',''; if ($t) { [int]([double]$t / 1000.0) } else { -1 } }
function GpuBusyPct { $b = (AdbSh "cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage") -replace '[^0-9]',''; if ($b) { [int]$b } else { -1 } }

"=== xenia-thor evidence capture ===" | Out-File $txt -Encoding utf8
W "label: $Label"
W "device_time: $stamp"
W "package: $Package"
W "attach: $($Attach.IsPresent)  set_cvar: '$SetCvar'  boot_wait: ${BootWaitSec}s  window: ${WindowSec}s"

# THERMAL/THRASH PREFLIGHT GUARD - never launch onto a hot or busy Thor.
$preTemp = GpuTempC
$preBusy = GpuBusyPct
W "preflight: gpu_temp=${preTemp}C gpu_busy=${preBusy}%  (limits: temp<${MaxStartTempC}C busy<${MaxStartBusyPct}%)"
if (-not $Force) {
    $waited = 0
    while ((($preTemp -ge $MaxStartTempC) -or ($preBusy -ge $MaxStartBusyPct)) -and ($waited -lt $CooldownWaitSec)) {
        # If it is hot AND something is hammering the GPU, defensively stop the emu so it can cool.
        if ($preBusy -ge $MaxStartBusyPct) {
            W "preflight: GPU busy ${preBusy}% - force-stopping emulator to let it cool"
            AdbSh "am force-stop $Package" | Out-Null
        }
        W "preflight: device too hot/busy (temp=${preTemp}C busy=${preBusy}%); cooling ${waited}/${CooldownWaitSec}s"
        Start-Sleep -Seconds 15
        $waited += 15
        $preTemp = GpuTempC
        $preBusy = GpuBusyPct
    }
    if (($preTemp -ge $MaxStartTempC) -or ($preBusy -ge $MaxStartBusyPct)) {
        W "ABORT: device still too hot/busy after ${CooldownWaitSec}s (temp=${preTemp}C busy=${preBusy}%). Refusing to launch - protecting the Thor. Re-run when cool, or pass -Force only if you mean it."
        W "EVIDENCE_FILE: $txt"
        Write-Output "ABORTED (thermal guard) - device temp=${preTemp}C busy=${preBusy}%. No launch."
        AdbSh "am force-stop $Package" | Out-Null
        exit 2
    }
    W "preflight: OK to launch (temp=${preTemp}C busy=${preBusy}%)"
}

if (-not $Attach) {
    W "action: LAUNCH (fresh)"
    AdbSh "am force-stop $Package" | Out-Null
    AdbSh "logcat -c" | Out-Null
    $extra = ""
    if ($SetCvar -and $SetCvar.Contains("=")) {
        $n,$v = $SetCvar.Split("=",2)
        # int vs bool/string heuristic for the launch extra
        if ($v -match '^\d+$') { $extra = "--ei $n $v" } elseif ($v -in @('true','false')) { $extra = "--ez $n $v" } else { $extra = "--es $n $v" }
    }
    $intent = "am start -W -n $comp --es gpu vulkan --es cpu arm64 --es apu android --es hid nop --es hid_nop_button_sequence '$Seq' --ez arm64_enable_mini_jit true --ez android_hide_osd true --ez mount_cache true --ez vulkan_trace_draw_outcomes_per_frame true $extra --es target '$Iso'"
    W "launch_intent: $intent"
    AdbSh $intent | Out-Null
    W "waiting ${BootWaitSec}s for heavy scene (thermal-monitored)..."
    # THERMAL WATCHDOG DURING BOOT WAIT. The boot/intro phase pegs the GPU at ~99%
    # for 2+ minutes; do NOT sleep blind through it (that left the Thor unguarded for
    # the whole boot once). Poll temp every 5s and force-stop the instant temp crosses
    # ThrashTempC so the device can never cook unattended.
    $bootWaited = 0
    while ($bootWaited -lt $BootWaitSec) {
        Start-Sleep -Seconds 5
        $bootWaited += 5
        $tNow = GpuTempC
        if ($tNow -ge $ThrashTempC) {
            W "THRASH GUARD TRIPPED during boot wait: gpu_temp=${tNow}C >= ${ThrashTempC}C at ${bootWaited}/${BootWaitSec}s - force-stopping emulator NOW to protect the Thor."
            AdbSh "am force-stop $Package" | Out-Null
            W "ABORT: device got too hot during boot wait; no measurement taken."
            W "EVIDENCE_FILE: $txt"
            Write-Output "ABORTED (thermal guard, boot wait) - gpu_temp=${tNow}C. Emulator force-stopped."
            exit 2
        }
    }
} else {
    W "action: ATTACH (running session)"
    if ($SetCvar -and $SetCvar.Contains("=")) {
        $n,$v = $SetCvar.Split("=",2)
        AdbSh "am broadcast -a $Package.SET_CVAR -p $Package --es cvar_name $n --es cvar_value $v" | Out-Null
        Start-Sleep -Milliseconds 800
        $applied = (AdbSh "logcat -d -t 40" | Select-String "SET_CVAR $n").Line | Select-Object -Last 1
        W "set_cvar_result: $applied"
    }
}

# --- measurement window ---
AdbSh "logcat -c" | Out-Null
# sample KGSL busy/clock across the window (every ~1s) WITH a defensive thermal
# watchdog: if the GPU crosses ThrashTempC mid-capture, force-stop the emulator
# immediately to protect the device, and abort the window.
$busy = @(); $clk = @()
$iters = [Math]::Max(1, $WindowSec)
$thrashed = $false
for ($i = 0; $i -lt $iters; $i++) {
    $b = (AdbSh "cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage") -replace '[^0-9]',''
    $c = (AdbSh "cat /sys/class/kgsl/kgsl-3d0/clock_mhz") -replace '[^0-9]',''
    if ($b) { $busy += [int]$b }
    if ($c) { $clk += [int]$c }
    $tNow = GpuTempC
    if ($tNow -ge $ThrashTempC) {
        W "THRASH GUARD TRIPPED: gpu_temp=${tNow}C >= ${ThrashTempC}C mid-capture - force-stopping emulator NOW to protect the Thor."
        AdbSh "am force-stop $Package" | Out-Null
        $thrashed = $true
        break
    }
    Start-Sleep -Seconds 1
}
$log = (AdbSh "logcat -d") -join "`n"
if ($thrashed) { W "NOTE: capture aborted by thermal watchdog; numbers below are partial - do NOT cite as a clean measurement." }

# screenshot
AdbSh "screencap -p /sdcard/_ev.png" | Out-Null
& $Adb -s $Device pull /sdcard/_ev.png $png 2>&1 | Out-Null
AdbSh "rm -f /sdcard/_ev.png" | Out-Null

# --- derive numbers FROM the captured log (never by hand) ---
$vdcount = ([regex]::Matches($log, "VdSwap\(")).Count
$fps = if ($WindowSec -gt 0) { [math]::Round($vdcount / $WindowSec, 2) } else { 0 }
$drawLines = ($log -split "`n" | Select-String "GPU draw outcomes/frame")
$lastDraw = if ($drawLines) { ($drawLines | Select-Object -Last 1).Line } else { "" }
$gpuUs = if ($lastDraw -match 'gpu_frame_us=(\d+)') { [int]$Matches[1] } else { 0 }
$rendered = if ($lastDraw -match 'rendered=(\d+)') { [int]$Matches[1] } else { 0 }
$guestMs = if ($lastDraw -match 'guest_ms=(\d+)') { [int]$Matches[1] } else { 0 }
$busyMed = if ($busy.Count) { ($busy | Sort-Object)[[int]($busy.Count/2)] } else { -1 }
$clkMax  = if ($clk.Count) { ($clk | Measure-Object -Maximum).Maximum } else { -1 }

# raw evidence into the file
W ""
W "--- RAW EVIDENCE ---"
W "vdswap_count: $vdcount over ${WindowSec}s"
W "kgsl_busy_samples: $($busy -join ',')"
W "kgsl_clk_samples_hz: $($clk -join ',')"
W "last_draw_outcomes_line: $lastDraw"
W "screenshot: $png"
W ""
W "===== SUMMARY (derived from raw above; cite this file) ====="
W ("fps={0}  (vdswap={1}/{2}s)" -f $fps, $vdcount, $WindowSec)
W ("gpu_busy_median={0}%  gpu_clk_max={1}MHz" -f $busyMed, ([int]($clkMax/1000000)))
W ("gpu_frame_us={0}  rendered={1}  guest_ms={2}" -f $gpuUs, $rendered, $guestMs)
W "=========================================================="
W ""
W "EVIDENCE_FILE: $txt"
Write-Output ""
Write-Output "Screenshot saved: $png  (READ it to verify the scene before citing fps)"
