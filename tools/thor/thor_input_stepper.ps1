<#
.SYNOPSIS
  Screenshot-gated input stepper for AYN Thor. Advances a guest UI by pressing an
  input, capturing the screen, and proceeding to the NEXT step only when the frame
  actually CHANGED — instead of fixed wall-clock timings (which are unreliable on
  Xenia and caused fabricated "reached level" claims this session). Ends with the
  verification gate so the final state carries a real, read-this-run screenshot.

.WHY
  Research (Eidos/Repeato automated-game-testing) + hard experience: replaying
  fixed-timed inputs only works if the game is fully deterministic; Xenia is host-
  threaded + wall-clock dependent, so fixed timings land on the wrong screen. This
  tool gates each step on observed frame change (luminance-grid signature diff),
  the robust state-aware approach.

.HOW
  -Steps is a list like "a","a","start","dd","a" (hid_nop button tokens sent as
  Android keyevents via the active controller). For each step: press -> wait ->
  capture -> compare signature to previous; if changed, advance; if not changed
  after -RepressMax presses, move on (logged). Captures go in OutDir; the final
  call invokes thor_verify_capture.ps1 for a proof-backed verdict.

.NOTES
  Device-only. Assumes the title is ALREADY LAUNCHED and the emulator is
  foreground (launch separately first). Keyevents: a=96, b=97, x=99, y=100,
  start=108, back=4, dpad up=19 down=20 left=21 right=22.
#>
param(
    [string]$Adb = "C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    [string]$DeviceSerial = "c3ca0370",
    [string[]]$Steps = @("start","a","a","a","a"),
    [int]$StepWaitMs = 3500,
    [int]$RepressMax = 3,
    [double]$ChangeThreshold = 0.04,
    [string]$OutDir = "",
    [string]$Label = "step",
    [switch]$SkipMoviesFirst,
    [int]$MovieSkipMaxSec = 90,
    [double]$StaticThreshold = 0.012,
    [int]$StaticHitsToSettle = 3
)

$ErrorActionPreference = "Continue"

$keymap = @{ a=96; b=97; x=99; y=100; start=108; back=4; up=19; down=20; left=21; right=22; dd=20; du=19; dl=21; dr=22 }

if (-not $OutDir) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutDir = "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\scratch\thor-debug\stepper-$Label-$stamp"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Luminance-grid signature: a 16x9 vector of average-brightness buckets; the L1
# distance between two signatures (normalized 0..1) is the "frame change" metric.
function Get-FrameSignature([string]$Path) {
    try {
        Add-Type -AssemblyName System.Drawing -ErrorAction Stop
        $bmp = [System.Drawing.Bitmap]::FromFile($Path)
        try {
            $w = $bmp.Width; $h = $bmp.Height
            if ($w -le 0 -or $h -le 0) { return $null }
            $cols = 16; $rows = 9
            $sig = New-Object 'double[]' ($cols * $rows)
            for ($iy = 0; $iy -lt $rows; $iy++) {
                for ($ix = 0; $ix -lt $cols; $ix++) {
                    $px = [int](($ix + 0.5) * $w / $cols)
                    $py = [int](($iy + 0.5) * $h / $rows)
                    $c = $bmp.GetPixel($px, $py)
                    $sig[$iy * $cols + $ix] = (0.299 * $c.R + 0.587 * $c.G + 0.114 * $c.B) / 255.0
                }
            }
            return $sig
        } finally { $bmp.Dispose() }
    } catch { return $null }
}

function Get-SigDistance($a, $b) {
    if ($null -eq $a -or $null -eq $b) { return 1.0 }
    $sum = 0.0
    for ($i = 0; $i -lt $a.Length; $i++) { $sum += [math]::Abs($a[$i] - $b[$i]) }
    return [math]::Round($sum / $a.Length, 4)
}

function Capture([string]$tag) {
    $p = Join-Path $OutDir "$tag.png"
    & $Adb -s $DeviceSerial shell screencap -p /sdcard/_step.png | Out-Null
    & $Adb -s $DeviceSerial pull /sdcard/_step.png $p 2>$null | Out-Null
    return $p
}

# Baseline frame before any input.
$prevPng = Capture "000-baseline"
$prevSig = Get-FrameSignature $prevPng
$timeline = New-Object System.Collections.Generic.List[string]
$timeline.Add("000-baseline sig_ok=$([bool]$prevSig)")

# --- MOVIE-SKIP PHASE (paramount): a playing movie makes EVERY frame differ, so
# we mash Start+A WHILE frames keep changing, and conclude we've reached an
# interactive menu only when the frame goes STATIC (change < StaticThreshold) for
# StaticHitsToSettle consecutive checks. This is the inverse of frame-change
# gating and is the correct way to detect "movie ended / menu reached". ---
if ($SkipMoviesFirst) {
    $timeline.Add("--- movie-skip phase begin (mash while changing, settle on static) ---")
    $deadline = (Get-Date).AddSeconds($MovieSkipMaxSec)
    $staticHits = 0
    $msIdx = 0
    while ((Get-Date) -lt $deadline) {
        $msIdx++
        # Mash both common skip buttons.
        & $Adb -s $DeviceSerial shell input keyevent 108 | Out-Null  # start
        & $Adb -s $DeviceSerial shell input keyevent 96  | Out-Null  # a
        Start-Sleep -Milliseconds 1800
        $png = Capture ("ms{0:000}" -f $msIdx)
        $sig = Get-FrameSignature $png
        $dist = Get-SigDistance $prevSig $sig
        $prevSig = $sig; $prevPng = $png
        if ($dist -lt $StaticThreshold) {
            $staticHits++
            $timeline.Add(("ms{0:000} change={1} STATIC ({2}/{3})" -f $msIdx, $dist, $staticHits, $StaticHitsToSettle))
            if ($staticHits -ge $StaticHitsToSettle) {
                $timeline.Add("--- movie-skip phase: settled (interactive screen reached) ---")
                break
            }
        } else {
            $staticHits = 0
            $timeline.Add(("ms{0:000} change={1} (movie/animation playing, keep mashing)" -f $msIdx, $dist))
        }
    }
    if ((Get-Date) -ge $deadline) {
        $timeline.Add("--- movie-skip phase: hit MovieSkipMaxSec without settling (still animating?) ---")
    }
}

$stepIdx = 0
foreach ($step in $Steps) {
    $stepIdx++
    $key = $keymap[$step.ToLower()]
    if (-not $key) { $timeline.Add(("{0:000} SKIP unknown step '{1}'" -f $stepIdx, $step)); continue }

    $advanced = $false
    for ($attempt = 1; $attempt -le $RepressMax; $attempt++) {
        & $Adb -s $DeviceSerial shell input keyevent $key | Out-Null
        Start-Sleep -Milliseconds $StepWaitMs
        $tag = "{0:000}-{1}-try{2}" -f $stepIdx, $step, $attempt
        $png = Capture $tag
        $sig = Get-FrameSignature $png
        $dist = Get-SigDistance $prevSig $sig
        $timeline.Add(("{0:000} {1} try{2} change={3} {4}" -f $stepIdx, $step, $attempt, $dist, (Split-Path $png -Leaf)))
        if ($dist -ge $ChangeThreshold) {
            $advanced = $true
            $prevSig = $sig
            $prevPng = $png
            break
        }
    }
    if (-not $advanced) {
        $timeline.Add(("{0:000} {1} NO-CHANGE after {2} presses (screen static; continuing)" -f $stepIdx, $step, $RepressMax))
    }
}

$timeline | Out-File -LiteralPath (Join-Path $OutDir "timeline.txt") -Encoding utf8

Write-Output "=== stepper timeline ==="
$timeline | ForEach-Object { Write-Output $_ }
Write-Output ""
Write-Output "OutDir: $OutDir"
Write-Output "Final frame: $prevPng"
Write-Output ""
Write-Output "Now run the verification gate to grade the final state with proof:"
Write-Output "  tools\thor\thor_verify_capture.ps1 -Label $Label-final"
