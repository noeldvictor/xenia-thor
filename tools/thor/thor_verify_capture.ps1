<#
.SYNOPSIS
  Verification-gated capture for AYN Thor. Emits a machine-checked "verdict record"
  for a title's current on-screen state — and REFUSES to emit one unless it
  actually read the screenshot bytes + the logcat this run.

.WHY
  This session repeatedly FABRICATED game-state claims (screenshots never read,
  fps/scene invented). This tool makes that structurally impossible: the verdict
  is computed ONLY from files proven to exist + parsed this run, and includes a
  screenshot SHA256 so any note quoting it can be cross-checked. No proof -> no
  verdict (exit 2).

.WHAT IT DOES
  1. Pulls the current device screen + a bounded logcat tail into a fresh packet
     (does NOT launch/relaunch — capture-first; pair with a separate launch step).
  2. Verifies the PNG exists and is non-trivial; computes its SHA256 + a
     luminance-based near_black share (OSD corner ignored).
  3. Parses logcat for guest VdSwap count, recent FPS (VdSwap timestamps), and
     fatal markers (CRASH DUMP / __assert2 / abort).
  4. Emits verdict.json + verdict.txt with: title_id, vdswap, fps_recent,
     near_black, classification, fatal_count, png_sha256, logcat_lines, and an
     explicit evidence block (the exact paths + sizes read).
  5. classification is one of: rendering | present_but_black | no_present |
     crash | UNVERIFIED. If the PNG can't be read, classification=UNVERIFIED and
     exit code 2 — callers/notes MUST NOT claim a rendered state in that case.

.NOTES
  Device-only, no source reads, no kernel risk. Run AFTER a launch (e.g. via
  thor_game_speed_quality_pass.ps1 or a manual am start) to grade the current
  screen with attached proof.
#>
param(
    [string]$Adb = "C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    [string]$DeviceSerial = "c3ca0370",
    [string]$OutDir = "",
    [int]$LogcatTailLines = 60000,
    [string]$Label = "verify"
)

$ErrorActionPreference = "Continue"

if (-not $OutDir) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutDir = "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\scratch\thor-debug\verify-$Label-$stamp"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$png = Join-Path $OutDir "screen.png"
$log = Join-Path $OutDir "logcat.txt"

# --- 0. FOREGROUND CHECK: is the emulator actually the resumed activity? ---
$fg = (& $Adb -s $DeviceSerial shell "dumpsys activity activities | grep topResumedActivity" 2>$null) -join "`n"
$inEmulator = $fg -match "jp\.xenia\.emulator"
$fgActivity = ""
$mfg = [regex]::Match($fg, "topResumedActivity=ActivityRecord\{[^ ]+ [^ ]+ ([^ }]+)")
if ($mfg.Success) { $fgActivity = $mfg.Groups[1].Value }

# --- 1. capture screen + logcat (device-only) ---
& $Adb -s $DeviceSerial shell screencap -p /sdcard/_verify.png | Out-Null
& $Adb -s $DeviceSerial pull /sdcard/_verify.png $png 2>$null | Out-Null
# Use epoch timestamps so we can enforce RECENCY (ignore stale tail).
& $Adb -s $DeviceSerial logcat -d -v epoch -t $LogcatTailLines | Out-File -LiteralPath $log -Encoding utf8
$captureEpoch = [double](& $Adb -s $DeviceSerial shell "date +%s")

# --- 2. PROOF GATE: the screenshot must really exist and be non-trivial ---
$pngOk = $false
$pngSha = ""
$pngBytes = 0
if (Test-Path -LiteralPath $png) {
    $fi = Get-Item -LiteralPath $png
    $pngBytes = $fi.Length
    if ($pngBytes -gt 8192) {
        $pngSha = (Get-FileHash -LiteralPath $png -Algorithm SHA256).Hash
        $pngOk = $true
    }
}

# --- near_black share from the actual pixels (OSD corner ignored) ---
function Get-NearBlackShare([string]$Path) {
    try {
        Add-Type -AssemblyName System.Drawing -ErrorAction Stop
        $bmp = [System.Drawing.Bitmap]::FromFile($Path)
        try {
            $w = $bmp.Width; $h = $bmp.Height
            if ($w -le 0 -or $h -le 0) { return -1 }
            $cols = 32; $rows = 18; $dark = 0; $count = 0
            for ($iy = 0; $iy -lt $rows; $iy++) {
                for ($ix = 0; $ix -lt $cols; $ix++) {
                    $px = [int](($ix + 0.5) * $w / $cols)
                    $py = [int](($iy + 0.5) * $h / $rows)
                    if ($px -lt ($w * 0.30) -and $py -lt ($h * 0.18)) { continue }
                    $c = $bmp.GetPixel($px, $py)
                    $lum = 0.299 * $c.R + 0.587 * $c.G + 0.114 * $c.B
                    if ($lum -lt 16) { $dark++ }
                    $count++
                }
            }
            if ($count -eq 0) { return -1 }
            return [math]::Round($dark / $count, 4)
        } finally { $bmp.Dispose() }
    } catch { return -1 }
}

$nearBlack = if ($pngOk) { Get-NearBlackShare $png } else { -1 }

# --- 3. parse logcat (guest progress + crash) ---
$logText = ""
if (Test-Path -LiteralPath $log) { $logText = Get-Content -LiteralPath $log -Raw }
$logLines = if ($logText) { ([regex]::Matches($logText, "`n")).Count } else { 0 }

$fatal = ([regex]::Matches($logText, "==== CRASH DUMP ====|__assert2|abort\+|Abort message")).Count
$titleId = ""
$m = [regex]::Match($logText, "Title ?ID[:=]?\s*([0-9A-Fa-f]{8})")
if ($m.Success) { $titleId = $m.Groups[1].Value }

# RECENCY: only count VdSwap rows whose epoch timestamp is within the last
# RecentWindowSec of capture. Stale tail swaps (prior session) must NOT count.
$recentWindowSec = 8.0
$vdLinesEpoch = [regex]::Matches($logText, "(?m)^\s*(\d+\.\d+).*VdSwap\(")
$vdswap = 0          # recent-only count
$vdswapTotal = ([regex]::Matches($logText, "VdSwap\(")).Count
$recentEpochs = New-Object System.Collections.Generic.List[double]
foreach ($vl in $vdLinesEpoch) {
    $e = [double]$vl.Groups[1].Value
    if (($captureEpoch - $e) -le $recentWindowSec -and ($captureEpoch - $e) -ge -2) {
        $vdswap++
        $recentEpochs.Add($e)
    }
}
$fpsRecent = 0.0
if ($recentEpochs.Count -gt 1) {
    $span = ($recentEpochs[$recentEpochs.Count - 1] - $recentEpochs[0])
    if ($span -gt 0.2) { $fpsRecent = [math]::Round($recentEpochs.Count / $span, 1) }
}

# --- 4. classification (only meaningful if pngOk AND emulator is foreground) ---
$classification = "UNVERIFIED"
$exit = 2
if (-not $pngOk) {
    $classification = "UNVERIFIED"; $exit = 2
} elseif (-not $inEmulator) {
    # Screen proven, but the emulator is NOT the foreground app -> not a game state.
    $classification = "NOT_IN_EMULATOR"; $exit = 3
} else {
    # vdswap here is RECENT-only (within recentWindowSec of capture).
    if ($fatal -gt 0 -and $vdswap -eq 0) { $classification = "crash"; $exit = 0 }
    elseif ($vdswap -gt 0 -and $nearBlack -ge 0 -and $nearBlack -lt 0.85) { $classification = "rendering"; $exit = 0 }
    elseif ($vdswap -gt 0 -and $nearBlack -ge 0.85) { $classification = "present_but_black"; $exit = 0 }
    elseif ($vdswap -gt 0) { $classification = "present_unknown_visual"; $exit = 0 }
    else { $classification = "no_present_recent"; $exit = 0 }
}

# --- 5. emit verdict (with evidence block) ---
$verdict = [ordered]@{
    label              = $Label
    classification     = $classification
    title_id           = $titleId
    in_emulator        = $inEmulator
    foreground_activity = $fgActivity
    vdswap_recent      = $vdswap
    vdswap_total_tail  = $vdswapTotal
    recent_window_sec  = $recentWindowSec
    fps_recent         = $fpsRecent
    near_black         = $nearBlack
    fatal_count        = $fatal
    proof = [ordered]@{
        png_read       = $pngOk
        png_path       = $png
        png_bytes      = $pngBytes
        png_sha256     = $pngSha
        logcat_path    = $log
        logcat_lines   = $logLines
        captured_at    = (Get-Date -Format o)
    }
    caveat = if (-not $pngOk) {
        "UNVERIFIED: screenshot was not read this run (png missing/too small). No rendered-state claim is permitted from this record."
    } else {
        "Classification computed from the screenshot + logcat read this run. Any note citing a rendered state MUST quote png_sha256."
    }
}

$verdict | ConvertTo-Json -Depth 5 | Out-File -LiteralPath (Join-Path $OutDir "verdict.json") -Encoding utf8
$verdict | ConvertTo-Json -Depth 5 | Out-File -LiteralPath (Join-Path $OutDir "verdict.txt") -Encoding utf8

Write-Output "OutDir: $OutDir"
Write-Output ("verdict: {0} | in_emulator={1} fg='{2}' vdswap_recent={3} (tail={4}) fps~{5} near_black={6} fatal={7} png_read={8} sha={9}" -f `
    $classification, $inEmulator, $fgActivity, $vdswap, $vdswapTotal, $fpsRecent, $nearBlack, $fatal, $pngOk, ($pngSha.Substring(0, [Math]::Min(12, $pngSha.Length))))
if (-not $pngOk) { Write-Output "REFUSED: no screenshot proof -> classification=UNVERIFIED (exit 2)" }
elseif (-not $inEmulator) { Write-Output "REFUSED: emulator not foreground -> classification=NOT_IN_EMULATOR (exit 3)" }
exit $exit
