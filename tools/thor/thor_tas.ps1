<#
.SYNOPSIS
  TAS-style record/replay for AYN Thor (xenia-thor). Records raw controller input
  to a movie file and replays it preserving timing, so a gameplay scene reached
  ONCE (by the user) can be re-reached deterministically for profiling / fix
  validation. Ends every replay with the verification gate (screenshot read).

.BACKGROUND (researched: libTAS, BizHawk)
  TAS = input movies (input only, no video) + frame advance + savestates +
  determinism. We implement the input-movie piece for the real controller.
  Device gamepad = /dev/input/event9 "Odin Controller" (verified via getevent -p:
  BTN_A=0x130 B=0x131 X=0x132 Y=0x133 Back=0x9e Start=0x13a, sticks ABS 0/1).

.MODES
  -Mode Record  -Movie <name> [-Seconds N]
      Streams `getevent -t -l /dev/input/event9` to <name>.movie while the USER
      plays (default 60s, or until -Seconds). The user drives to the target scene;
      we capture their raw input with timestamps.
  -Mode Replay  -Movie <name> [-SlowMo <scalar>]
      Replays the recorded events via on-device `sendevent` preserving inter-event
      delays. Optional -SlowMo sets time_scalar (widens timing windows). Ends by
      invoking thor_verify_capture.ps1 and printing the verdict + screenshot path
      to READ.
  -Mode Info    -Movie <name>   (summarize a movie: event count, duration, buttons)

.HONEST CAVEATS (validate before trusting)
  - sendevent fidelity to Xenia's HID path is UNVERIFIED on first use: confirm a
    replay actually reaches the guest (read the post-replay screenshot). If raw
    replay does not drive the guest, fall back to the guest-side hid_nop sequence.
  - Xenia is multithreaded -> replay is NOT bit-deterministic; ALWAYS verify the
    end state by reading the screenshot, do not assume identical frames.
  - getevent -t timestamps are kernel time; we replay relative gaps.
#>
param(
    [string]$Adb = "C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    [string]$DeviceSerial = "c3ca0370",
    [string]$EventNode = "/dev/input/event9",
    [ValidateSet("Record", "Replay", "Info")]
    [string]$Mode = "Info",
    [Parameter(Mandatory = $true)][string]$Movie,
    [int]$Seconds = 60,
    [double]$SlowMo = 0,
    [string]$Package = "jp.xenia.emulator.github.debug"
)

$ErrorActionPreference = "Continue"
$movieDir = "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\tools\thor\tas-movies"
New-Item -ItemType Directory -Force -Path $movieDir | Out-Null
$moviePath = Join-Path $movieDir "$Movie.movie"

switch ($Mode) {
    "Record" {
        Write-Output "RECORD: capturing $EventNode for ${Seconds}s -> $moviePath"
        Write-Output "  >>> PLAY NOW on the Thor controller (drive to the target scene) <<<"
        # getevent -t -l: timestamped, labeled. Run on device for N seconds via timeout.
        # We capture to a device file then pull (more reliable than streaming stdout).
        $devFile = "/sdcard/_tas_$Movie.txt"
        & $Adb -s $DeviceSerial shell "rm -f $devFile" | Out-Null
        & $Adb -s $DeviceSerial shell "timeout $Seconds getevent -t -l $EventNode > $devFile"
        & $Adb -s $DeviceSerial pull $devFile $moviePath 2>$null | Out-Null
        if (Test-Path $moviePath) {
            $lines = (Get-Content -LiteralPath $moviePath | Measure-Object -Line).Lines
            Write-Output "Saved movie: $moviePath ($lines event lines)"
        } else {
            Write-Output "FAILED to pull movie (no events captured?)"
        }
    }
    "Info" {
        if (-not (Test-Path $moviePath)) { Write-Output "No movie at $moviePath"; break }
        $raw = Get-Content -LiteralPath $moviePath
        $evCount = ($raw | Where-Object { $_ -match "EV_KEY|EV_ABS|EV_SYN" }).Count
        $keys = $raw | Where-Object { $_ -match "EV_KEY" } | ForEach-Object { if ($_ -match "(BTN_\w+|KEY_\w+)") { $Matches[1] } } | Group-Object | Sort-Object Count -Desc
        Write-Output "Movie: $moviePath"
        Write-Output "  total lines: $($raw.Count), event lines: $evCount"
        Write-Output "  buttons seen:"
        $keys | Select-Object -First 12 | ForEach-Object { Write-Output ("    {0} x{1}" -f $_.Name, $_.Count) }
    }
    "Replay" {
        if (-not (Test-Path $moviePath)) { Write-Output "No movie at $moviePath"; break }
        Write-Output "REPLAY: $moviePath via sendevent (preserving timing)"
        if ($SlowMo -gt 0) {
            Write-Output "  setting time_scalar=$SlowMo for replay"
            & $Adb -s $DeviceSerial shell am broadcast -a "$Package.SET_CVAR" -p $Package --es cvar_name time_scalar --es cvar_value $SlowMo | Out-Null
        }
        # Parse getevent -t -l lines: "[   12345.678901] EV_KEY  BTN_A  DOWN"
        # Need numeric type/code/value for sendevent -> re-capture with raw getevent
        # mapping. getevent -l gives labels; sendevent needs numbers. We parse the
        # timestamp for delays and translate common labels to codes.
        $labelToType = @{ EV_SYN=0; EV_KEY=1; EV_ABS=3 }
        $labelToKey  = @{ BTN_A=0x130; BTN_B=0x131; BTN_X=0x132; BTN_Y=0x133; BTN_START=0x13a; BTN_SELECT=0x13b; BTN_MODE=0x13c; BTN_THUMBL=0x13d; BTN_THUMBR=0x13e; BTN_TL=0x136; BTN_TR=0x137 }
        $valMap = @{ DOWN=1; UP=0 }
        $raw = Get-Content -LiteralPath $moviePath
        $prevTs = $null
        $sent = 0
        foreach ($ln in $raw) {
            if ($ln -match "\[\s*([0-9]+\.[0-9]+)\]\s+(EV_\w+)\s+(\S+)\s+(\S+)") {
                $ts = [double]$Matches[1]; $etype = $Matches[2]; $code = $Matches[3]; $val = $Matches[4]
                if ($null -ne $prevTs) {
                    $gap = $ts - $prevTs
                    if ($gap -gt 0.004) { Start-Sleep -Milliseconds ([int]([math]::Min($gap*1000, 2000))) }
                }
                $prevTs = $ts
                $tnum = $labelToType[$etype]
                if ($null -eq $tnum) { continue }
                $cnum = if ($labelToKey.ContainsKey($code)) { $labelToKey[$code] } elseif ($code -match "^[0-9a-fA-Fx]+$") { [Convert]::ToInt32($code,16) } else { $null }
                if ($null -eq $cnum) { continue }
                $vnum = if ($valMap.ContainsKey($val)) { $valMap[$val] } elseif ($val -match "^[0-9a-fA-Fx]+$") { [Convert]::ToInt32($val,16) } else { 0 }
                & $Adb -s $DeviceSerial shell sendevent $EventNode $tnum $cnum $vnum | Out-Null
                $sent++
            }
        }
        Write-Output "  sent $sent events"
        if ($SlowMo -gt 0) {
            & $Adb -s $DeviceSerial shell am broadcast -a "$Package.SET_CVAR" -p $Package --es cvar_name time_scalar --es cvar_value 1.0 | Out-Null
            Write-Output "  restored time_scalar=1.0"
        }
        Start-Sleep -Seconds 3
        Write-Output ""
        Write-Output "Now grading final state with the verification gate (READ the screenshot it names):"
        & "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\tools\thor\thor_verify_capture.ps1" -Label "tasreplay-$Movie"
    }
}
