<#
.SYNOPSIS
  Pause / slow / resume the guest game on AYN Thor via the live time_scalar cvar,
  so a frame can be REVIEWED stably and menu items SELECTED without the game being
  a moving target. (User request 2026-05-30.)

.WHY
  A live, animating game makes every screenshot a moving target — the cause of
  repeated frame-misreads this session. Freezing/slowing the guest before capture
  gives a stable frame to read and a stable state to navigate, then resume.

.HOW
  Uses the SET_CVAR broadcast (same path as tools/thor/thor_cvar.ps1) to set
  time_scalar:
    -Mode Pause   -> time_scalar = PauseScalar (default 0.02, near-freeze; NOT 0,
                     which may stall/divide-by-zero)
    -Mode Resume  -> time_scalar = 1.0
    -Mode Capture -> Pause, screencap+pull a still frame, then (unless -StayPaused)
                     Resume; prints the saved PNG path for the caller to READ.

.CAVEAT (UNVALIDATED)
  time_scalar live-pause behavior must be DEVICE-VALIDATED: confirm a captured
  frame is actually frozen (two captures while paused are near-identical) before
  trusting pause. Do not claim pause works until verified on device. If time_scalar
  is not honored live, fall back to the in-game menu (Back/toggleInGameMenu) or
  relaunch-time time_scalar.

.NOTES
  Pairs with thor_verify_capture.ps1 (capture-while-paused) and
  thor_input_stepper.ps1 (pause between steps). Device-only; no kernel risk.
#>
param(
    [string]$Adb = "C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    [string]$DeviceSerial = "c3ca0370",
    [string]$Package = "jp.xenia.emulator.github.debug",
    [ValidateSet("Pause", "Resume", "Capture")]
    [string]$Mode = "Capture",
    [double]$PauseScalar = 0.02,
    [switch]$StayPaused,
    [string]$OutDir = "",
    [string]$Label = "pause"
)

$ErrorActionPreference = "Continue"
$action = "$Package.SET_CVAR"

function Set-TimeScalar([double]$v) {
    & $Adb -s $DeviceSerial shell am broadcast -a $action -p $Package `
        --es cvar_name time_scalar --es cvar_value $v | Out-Null
    # Echo the native confirmation if present.
    $line = & $Adb -s $DeviceSerial logcat -d -t 80 | Select-String "nativeSetConfigVar: time_scalar" | Select-Object -Last 1
    if ($line) { Write-Output ("  " + $line.Line) }
}

switch ($Mode) {
    "Pause" {
        Write-Output "Pausing guest: time_scalar=$PauseScalar"
        Set-TimeScalar $PauseScalar
    }
    "Resume" {
        Write-Output "Resuming guest: time_scalar=1.0"
        Set-TimeScalar 1.0
    }
    "Capture" {
        if (-not $OutDir) {
            $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
            $OutDir = "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\scratch\thor-debug\pause-$Label-$stamp"
        }
        New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

        Write-Output "Pause -> capture -> (resume) for stable frame review"
        Set-TimeScalar $PauseScalar
        Start-Sleep -Milliseconds 600   # let the slow-down take effect

        # Two captures to PROVE the frame is frozen (caller should see them ~equal).
        $p1 = Join-Path $OutDir "still-1.png"
        & $Adb -s $DeviceSerial shell screencap -p /sdcard/_p1.png | Out-Null
        & $Adb -s $DeviceSerial pull /sdcard/_p1.png $p1 2>$null | Out-Null
        Start-Sleep -Milliseconds 700
        $p2 = Join-Path $OutDir "still-2.png"
        & $Adb -s $DeviceSerial shell screencap -p /sdcard/_p2.png | Out-Null
        & $Adb -s $DeviceSerial pull /sdcard/_p2.png $p2 2>$null | Out-Null

        $h1 = if (Test-Path $p1) { (Get-FileHash -LiteralPath $p1 -Algorithm SHA256).Hash } else { "" }
        $h2 = if (Test-Path $p2) { (Get-FileHash -LiteralPath $p2 -Algorithm SHA256).Hash } else { "" }
        $frozen = ($h1 -ne "" -and $h1 -eq $h2)

        if (-not $StayPaused) {
            Set-TimeScalar 1.0
            Write-Output "Resumed (time_scalar=1.0)"
        } else {
            Write-Output "Left PAUSED (time_scalar=$PauseScalar); call -Mode Resume when done"
        }

        Write-Output "OutDir: $OutDir"
        Write-Output ("still-1: {0}" -f $p1)
        Write-Output ("still-2: {0}" -f $p2)
        Write-Output ("frozen_check: {0} (h1==h2 over ~0.7s while paused; identical hashes => guest frozen)" -f $frozen)
        Write-Output "NOTE: READ still-1.png to review the frame. If frozen_check=False the pause may not be honored live -> validate / fall back."
    }
}
