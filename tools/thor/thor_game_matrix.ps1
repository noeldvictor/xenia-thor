<#
.SYNOPSIS
  Regression harness for the priority Xbox 360 titles on AYN Thor. Runs each game
  through the VERIFIED capture gate, assigns an RPCS3-style status tier + metrics,
  and (optionally) compares against a saved baseline to report BETTER / WORSE /
  SAME per game. This is the "is it better or worse?" loop.

.DESIGN (grounded in emulator-testing best practice: Dolphin FifoCI, RPCS3 tiers)
  - Deterministic reach: launch + movie-skip dwell (later: input replay fixtures).
  - Trusted measurement: composes tools/thor/thor_verify_capture.ps1 — every
    metric comes from a screenshot READ this run + logcat (no fabrication).
  - Status tiers (ordinal, higher=better):
      0 Nothing   - no boot / immediate fatal, no present
      1 Loadable  - process alive but no guest present (no_present)
      2 Ingame-black - presenting (~live) but near_black high (e.g. 3D scene black,
                       HUD-only) = present_but_black
      3 Ingame    - rendering real content (near_black moderate)
      4 Playable  - rendering bright/full content (near_black low) at good fps
  - "Better/worse" = tier delta first, then near_black (lower=more scene), then fps.

.USAGE
  # establish baseline:
  thor_game_matrix.ps1 -SaveBaseline
  # after a change, compare:
  thor_game_matrix.ps1 -Baseline <path-to-baseline.json>

.NOTES
  Device-only. Captures are proof-gated. Reads each game's screenshot via the gate
  (which records png_sha256). The agent must still READ the screenshots to confirm
  scene content, but tier/metrics here are machine-computed and trustworthy.
#>
param(
    [string]$Adb = "C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe",
    [string]$DeviceSerial = "c3ca0370",
    [string]$Package = "jp.xenia.emulator.github.debug",
    [string]$Activity = "jp.xenia.emulator.EmulatorActivity",
    [int]$BootDwellSec = 35,
    [string]$Baseline = "",
    [switch]$SaveBaseline,
    [string]$ExtraIntent = "",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Continue"
$verifyTool = "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\tools\thor\thor_verify_capture.ps1"

if (-not $OutDir) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutDir = "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\scratch\thor-debug\matrix-$stamp"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Priority titles (user-set): Blue Dragon, Lost Odyssey, Banjo, Burnout.
$games = @(
    @{ key="burnout";      iso="/storage/2664-21DE/Roms/xbox360/Burnout Revenge (USA).iso" },
    @{ key="banjo";        iso="/storage/2664-21DE/Roms/xbox360/Banjo-Kazooie - Nuts & Bolts (USA) (En,Ja,Fr,De,Es,It,Nl,Sv,No,Zh,Ko,Pl,Ru,Cs).iso" },
    @{ key="bluedragon";   iso="/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso" },
    @{ key="lostodyssey";  iso="/storage/2664-21DE/Roms/xbox360/Lost Odyssey.m3u/Lost Odyssey (USA, Europe) (En,Ja,Fr,De,Es,It) (Disc 1).iso" }
)

function Get-Tier($classification, $nearBlack, $fps, $fatal) {
    if ($classification -eq "crash" -or $fatal -gt 0) { return 0 }       # Nothing
    if ($classification -eq "no_present" -or $classification -eq "no_present_recent") { return 1 } # Loadable
    if ($classification -eq "present_but_black") { return 2 }            # Ingame-black
    if ($classification -eq "rendering") {
        if ($nearBlack -ge 0 -and $nearBlack -lt 0.35 -and $fps -ge 25) { return 4 } # Playable
        return 3                                                          # Ingame
    }
    return 1
}
$tierName = @{ 0="Nothing"; 1="Loadable"; 2="Ingame-black"; 3="Ingame"; 4="Playable" }

$rows = New-Object System.Collections.Generic.List[object]
foreach ($g in $games) {
    Write-Output "===== $($g.key) ====="
    & $Adb -s $DeviceSerial shell am force-stop $Package | Out-Null
    & $Adb -s $DeviceSerial logcat -c | Out-Null
    $tEsc = $g.iso -replace "'", "'\\''"
    $intent = "am start -W -n $Package/$Activity --es gpu vulkan --es cpu arm64 --es apu android --es hid android --ez arm64_enable_mini_jit true --ez android_hide_osd true --ez mount_cache true"
    if ($ExtraIntent) { $intent += " $ExtraIntent" }
    $intent += " --es target '$tEsc'"
    & $Adb -s $DeviceSerial shell $intent | Out-Null

    # Deterministic dwell + light movie-skip mashing (Start+A) during boot.
    $elapsed = 0
    while ($elapsed -lt $BootDwellSec) {
        & $Adb -s $DeviceSerial shell input keyevent 108 | Out-Null
        & $Adb -s $DeviceSerial shell input keyevent 96  | Out-Null
        Start-Sleep -Seconds 5
        $elapsed += 5
    }

    # Trusted measurement via the verify gate.
    $gameOut = Join-Path $OutDir $g.key
    & $verifyTool -OutDir $gameOut -Label $g.key | Out-Null
    $vfile = Join-Path $gameOut "verdict.json"
    if (Test-Path $vfile) {
        $v = Get-Content -LiteralPath $vfile -Raw | ConvertFrom-Json
        $tier = Get-Tier $v.classification $v.near_black $v.fps_recent $v.fatal_count
        $row = [pscustomobject]@{
            game           = $g.key
            tier           = $tier
            tier_name      = $tierName[$tier]
            classification = $v.classification
            near_black     = $v.near_black
            fps_recent     = $v.fps_recent
            vdswap_recent  = $v.vdswap_recent
            fatal_count    = $v.fatal_count
            in_emulator    = $v.in_emulator
            png_sha256     = $v.proof.png_sha256
            png_path       = $v.proof.png_path
        }
        $rows.Add($row)
        Write-Output ("  {0} tier={1}({2}) class={3} near_black={4} fps~{5}" -f $g.key,$tier,$tierName[$tier],$v.classification,$v.near_black,$v.fps_recent)
    } else {
        Write-Output "  $($g.key): NO VERDICT (gate did not emit) - skipped"
    }
}

& $Adb -s $DeviceSerial shell am force-stop $Package | Out-Null

# Persist matrix.
$matrixJson = Join-Path $OutDir "matrix.json"
$rows | ConvertTo-Json -Depth 5 | Out-File -LiteralPath $matrixJson -Encoding utf8
$rows | Format-Table game,tier_name,classification,near_black,fps_recent,fatal_count | Out-File -LiteralPath (Join-Path $OutDir "matrix.txt") -Encoding utf8

Write-Output ""
Write-Output "=== MATRIX ($OutDir) ==="
$rows | Format-Table game,tier,tier_name,classification,near_black,fps_recent,fatal_count -AutoSize | Out-String | Write-Output

if ($SaveBaseline) {
    $baselinePath = "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\docs\research\game-matrix-baseline.json"
    $rows | ConvertTo-Json -Depth 5 | Out-File -LiteralPath $baselinePath -Encoding utf8
    Write-Output "Saved baseline -> $baselinePath"
}

if ($Baseline -and (Test-Path $Baseline)) {
    Write-Output ""
    Write-Output "=== COMPARISON vs baseline ($Baseline) ==="
    $base = Get-Content -LiteralPath $Baseline -Raw | ConvertFrom-Json
    $baseMap = @{}
    foreach ($b in $base) { $baseMap[$b.game] = $b }
    foreach ($r in $rows) {
        $b = $baseMap[$r.game]
        if (-not $b) { Write-Output ("  {0}: NEW (no baseline)" -f $r.game); continue }
        $verdict = "SAME"
        if ($r.tier -gt $b.tier) { $verdict = "BETTER (tier $($b.tier)->$($r.tier))" }
        elseif ($r.tier -lt $b.tier) { $verdict = "WORSE (tier $($b.tier)->$($r.tier))" }
        else {
            # same tier: use near_black (lower=more scene) then fps
            $nbDelta = [math]::Round($r.near_black - $b.near_black, 3)
            $fpsDelta = [math]::Round($r.fps_recent - $b.fps_recent, 1)
            if ($nbDelta -le -0.05) { $verdict = "BETTER (near_black $($b.near_black)->$($r.near_black), more scene)" }
            elseif ($nbDelta -ge 0.05) { $verdict = "WORSE (near_black $($b.near_black)->$($r.near_black), less scene)" }
            elseif ($fpsDelta -ge 5) { $verdict = "BETTER (fps $($b.fps_recent)->$($r.fps_recent))" }
            elseif ($fpsDelta -le -5) { $verdict = "WORSE (fps $($b.fps_recent)->$($r.fps_recent))" }
        }
        Write-Output ("  {0}: {1}" -f $r.game, $verdict)
    }
}
Write-Output ""
Write-Output "REMINDER: tiers/metrics are machine-computed from gate verdicts (screenshot read + logcat). To confirm SCENE CONTENT (e.g. 3D vs HUD-only), READ each game's png_path."
