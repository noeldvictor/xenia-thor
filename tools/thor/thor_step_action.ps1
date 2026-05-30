<#
.SYNOPSIS
  Pause-step a guest game one action at a time, the way the user described:
  PAUSE -> screenshot (review a STABLE frame) -> UNPAUSE -> send ONE action ->
  re-PAUSE -> screenshot. Each cycle leaves the game paused on a stable frame the
  agent/user can READ before deciding the next action. This is the reliable,
  reviewable alternative to blind timed input (which caused misreads/fabrications).

.PAUSE MECHANISM
  Uses time_scalar via the live SET_CVAR broadcast:
    pause  = time_scalar = PauseScalar (default 0.02 = near-freeze slow-mo;
             device-tested: slows hard, not a perfect freeze, but frames are
             stable enough to read between actions)
    run    = time_scalar = 1.0 briefly while the action is delivered, then re-pause
  (In-game titles with a Start pause menu can use thor_pause.ps1 -Mode
  InGamePauseCapture instead; this tool uses time_scalar so it works in menus too.)

.ACTION
  -Action is one input token: a,b,x,y,start,back,up,down,left,right (Android
  keyevent to the active controller). One action per invocation keeps it
  reviewable; call repeatedly to build a sequence, reading the still each time.

.OUTPUT
  Saves before.png (paused, pre-action) and after.png (paused, post-action) so the
  effect of the single action is visible on two stable frames. Prints both paths
  to READ. Records the action to an optional -MovieLog for replay.
#>
param(
    [string]$Adb = "C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    [string]$DeviceSerial = "c3ca0370",
    [string]$Package = "jp.xenia.emulator.github.debug",
    [string]$Action = "a",
    [double]$PauseScalar = 0.02,
    [int]$ActionRunMs = 450,
    [string]$OutDir = "",
    [string]$MovieLog = ""
)

$ErrorActionPreference = "Continue"
$keymap = @{ a=96; b=97; x=99; y=100; start=108; back=4; up=19; down=20; left=21; right=22; dd=20; du=19; dl=21; dr=22 }
$key = $keymap[$Action.ToLower()]
if (-not $key) { Write-Output "Unknown action '$Action'"; exit 1 }

if (-not $OutDir) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutDir = "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\scratch\thor-debug\step-$Action-$stamp"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Set-Scalar([double]$v) {
    & $Adb -s $DeviceSerial shell am broadcast -a "$Package.SET_CVAR" -p $Package `
        --es cvar_name time_scalar --es cvar_value $v | Out-Null
}
function Cap([string]$name) {
    $p = Join-Path $OutDir $name
    & $Adb -s $DeviceSerial shell screencap -p /sdcard/_step.png | Out-Null
    & $Adb -s $DeviceSerial pull /sdcard/_step.png $p 2>$null | Out-Null
    return $p
}

# 1. PAUSE (slow to near-freeze) and capture the stable BEFORE frame.
Set-Scalar $PauseScalar
Start-Sleep -Milliseconds 500
$before = Cap "before.png"
$bsha = if (Test-Path $before) { (Get-FileHash -LiteralPath $before -Algorithm SHA256).Hash.Substring(0,12) } else { "MISSING" }

# 2. UNPAUSE briefly, send ONE action, then re-PAUSE.
Set-Scalar 1.0
Start-Sleep -Milliseconds 120
& $Adb -s $DeviceSerial shell input keyevent $key | Out-Null
Start-Sleep -Milliseconds $ActionRunMs
Set-Scalar $PauseScalar
Start-Sleep -Milliseconds 500

# 3. Capture the stable AFTER frame.
$after = Cap "after.png"
$asha = if (Test-Path $after) { (Get-FileHash -LiteralPath $after -Algorithm SHA256).Hash.Substring(0,12) } else { "MISSING" }

if ($MovieLog) {
    "$Action" | Out-File -LiteralPath $MovieLog -Append -Encoding utf8
}

Write-Output "OutDir: $OutDir"
Write-Output ("action: {0} (keyevent {1})" -f $Action, $key)
Write-Output ("before: {0}  sha={1}" -f $before, $bsha)
Write-Output ("after : {0}  sha={1}" -f $after, $asha)
Write-Output ("changed_by_action: {0}" -f ($bsha -ne $asha))
Write-Output "Guest left PAUSED (time_scalar=$PauseScalar). READ after.png to review; run again for the next action, or thor_cvar.ps1 -Name time_scalar -Value 1.0 to resume."
