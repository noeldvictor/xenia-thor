<#
.SYNOPSIS
  Sweep the AYN Thor game library: launch each title, press Start/A to skip
  intro movies, let it settle, then capture a screenshot + guest-swap FPS +
  crash/quality classification into one results table.

.NOTES
  Drives adb directly (does NOT use the git-status-fragile thor wrappers).
  Quality is judged from the captured screenshots (near-black heuristic +
  on-screen guest-swap FPS badge) plus VdSwap progress in logcat.
  Behavior-neutral: launches with current default cvars unless -ExtraIntent
  is supplied (used later for the gpu_uma_direct_shared_memory A/B).
#>
param(
    [string]$Adb = "C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    [string]$DeviceSerial = "c3ca0370",
    [string]$Package = "jp.xenia.emulator.github.debug",
    [string]$Activity = "jp.xenia.emulator.EmulatorActivity",
    [string]$RomDir = "/storage/2664-21DE/Roms/xbox360",
    [string[]]$Targets = @(),
    [string]$OutDir = "",
    [string]$ExtraIntent = "",
    [string]$Label = "baseline",
    [int]$BootWaitSec = 28,
    [int]$SkipWindowSec = 55,
    [int]$SkipEverySec = 5,
    [int]$SettleSec = 6
)

$ErrorActionPreference = "Continue"
$component = "$Package/$Activity"

function Invoke-AdbShell([string]$Cmd) {
    & $Adb -s $DeviceSerial shell $Cmd
}

if (-not $OutDir) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutDir = "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\scratch\thor-debug\game-pass-$Label-$stamp"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Discover targets if none supplied: plain *.iso plus first *.iso inside *.m3u dirs.
if (-not $Targets -or $Targets.Count -eq 0) {
    $isoList = Invoke-AdbShell "ls -1 '$RomDir'/*.iso 2>/dev/null"
    $m3uIso  = Invoke-AdbShell "for d in '$RomDir'/*.m3u; do ls -1 `"`$d`"/*.iso 2>/dev/null | head -1; done"
    $Targets = @()
    $Targets += ($isoList | Where-Object { $_ -and $_.Trim() } | ForEach-Object { $_.Trim() })
    $Targets += ($m3uIso  | Where-Object { $_ -and $_.Trim() } | ForEach-Object { $_.Trim() })
}

function Get-NearBlackShare([string]$PngPath) {
    # Average-luminance grid sample, ignoring the top-left OSD corner.
    try {
        Add-Type -AssemblyName System.Drawing -ErrorAction Stop
        $bmp = [System.Drawing.Bitmap]::FromFile($PngPath)
        try {
            $w = $bmp.Width; $h = $bmp.Height
            if ($w -le 0 -or $h -le 0) { return -1 }
            $cols = 32; $rows = 18; $dark = 0; $count = 0
            for ($iy = 0; $iy -lt $rows; $iy++) {
                for ($ix = 0; $ix -lt $cols; $ix++) {
                    $px = [int](($ix + 0.5) * $w / $cols)
                    $py = [int](($iy + 0.5) * $h / $rows)
                    # skip top-left quadrant where the FPS OSD lives
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

$results = New-Object System.Collections.Generic.List[object]
$idx = 0
foreach ($target in $Targets) {
    $idx++
    $name = [System.IO.Path]::GetFileNameWithoutExtension($target)
    $safe = ($name -replace '[^A-Za-z0-9._-]', '_')
    if ($safe.Length -gt 48) { $safe = $safe.Substring(0, 48) }
    Write-Output "===== [$idx/$($Targets.Count)] $name ====="

    & $Adb -s $DeviceSerial shell am force-stop $Package | Out-Null
    & $Adb -s $DeviceSerial logcat -c | Out-Null

    $tEsc = $target -replace "'", "'\\''"
    $intent = "am start -W -n $component --es gpu vulkan --es cpu arm64 --es apu android --es hid android --ez arm64_enable_mini_jit true --ez android_hide_osd true --ez mount_cache true"
    if ($ExtraIntent) { $intent += " $ExtraIntent" }
    $intent += " --es target '$tEsc'"
    Invoke-AdbShell $intent | Out-Null

    Start-Sleep -Seconds $BootWaitSec
    # Skip intro movies / advance menus: tap Start (108) and A (96) repeatedly.
    $elapsed = 0
    while ($elapsed -lt $SkipWindowSec) {
        & $Adb -s $DeviceSerial shell input keyevent 108 | Out-Null
        & $Adb -s $DeviceSerial shell input keyevent 96  | Out-Null
        Start-Sleep -Seconds $SkipEverySec
        $elapsed += $SkipEverySec
    }
    Start-Sleep -Seconds $SettleSec

    $png = Join-Path $OutDir "$safe.png"
    Invoke-AdbShell "screencap -p /sdcard/_pass.png" | Out-Null
    & $Adb -s $DeviceSerial pull /sdcard/_pass.png $png 2>$null | Out-Null

    $log = Join-Path $OutDir "$safe.logcat.txt"
    & $Adb -s $DeviceSerial logcat -d -t 120000 | Out-File -LiteralPath $log -Encoding utf8

    $logText = Get-Content -LiteralPath $log -Raw
    $vdswap = ([regex]::Matches($logText, "VdSwap\(")).Count
    $crashMarkers = ([regex]::Matches($logText, "RtlRaiseException|==== CRASH DUMP ====|FATAL EXCEPTION|tombstone|backtrace:|SIGSEGV|SIGABRT|AndroidRuntime")).Count
    $titleId = ""
    $m = [regex]::Match($logText, "Title ?ID[:=]?\s*([0-9A-Fa-f]{8})")
    if ($m.Success) { $titleId = $m.Groups[1].Value }
    $lastVd = ""
    $fpsRecent = 0.0
    $vdLines = [regex]::Matches($logText, "(\d\d:\d\d:\d\d\.\d+).*VdSwap\(")
    if ($vdLines.Count -gt 0) {
        $lastVd = $vdLines[$vdLines.Count - 1].Groups[1].Value
        # Approximate steady-state FPS: count guest swaps in the last 5s window.
        function To-Sec([string]$t) {
            $p = $t.Split(':'); return ([double]$p[0]) * 3600 + ([double]$p[1]) * 60 + [double]$p[2]
        }
        $lastSec = To-Sec $lastVd
        $windowSec = 5.0
        $inWindow = 0
        foreach ($vl in $vdLines) {
            $s = To-Sec $vl.Groups[1].Value
            if (($lastSec - $s) -ge 0 -and ($lastSec - $s) -le $windowSec) { $inWindow++ }
        }
        if ($inWindow -gt 1) { $fpsRecent = [math]::Round($inWindow / $windowSec, 1) }
    }
    $nearBlack = Get-NearBlackShare $png

    $klass = "unknown"
    if ($crashMarkers -gt 0) { $klass = "crash" }
    elseif ($vdswap -gt 0 -and $nearBlack -ge 0 -and $nearBlack -lt 0.85) { $klass = "rendering" }
    elseif ($vdswap -gt 0 -and $nearBlack -ge 0.85) { $klass = "present_but_black" }
    elseif ($vdswap -gt 0) { $klass = "present_unknown_visual" }
    else { $klass = "no_present_or_no_boot" }

    $row = [pscustomobject]@{
        idx        = $idx
        name       = $name
        title_id   = $titleId
        vdswap     = $vdswap
        fps_recent = $fpsRecent
        last_vdswap= $lastVd
        crash_mk   = $crashMarkers
        near_black = $nearBlack
        klass      = $klass
        png        = (Split-Path $png -Leaf)
    }
    $results.Add($row)
    Write-Output ("  -> {0} | fps~{1} vdswap={2} crash={3} near_black={4} title={5}" -f $klass, $fpsRecent, $vdswap, $crashMarkers, $nearBlack, $titleId)

    # Persist incrementally so partial progress survives interruption.
    $results | Format-Table -AutoSize | Out-File -LiteralPath (Join-Path $OutDir "results.txt") -Encoding utf8
    $results | ConvertTo-Json -Depth 4 | Out-File -LiteralPath (Join-Path $OutDir "results.json") -Encoding utf8
}

& $Adb -s $DeviceSerial shell am force-stop $Package | Out-Null
Write-Output ""
Write-Output "OutDir: $OutDir"
$results | Format-Table -AutoSize | Out-String | Write-Output
