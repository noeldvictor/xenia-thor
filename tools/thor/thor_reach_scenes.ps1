<#
.SYNOPSIS
  Per-title deterministic reach-scene sequences for the AYN Thor test rig.

  Each entry maps a title to: its exact ISO path, a known-good deterministic
  hid_nop -InputSequence (BUTTONS@DELAY_MS:HOLD_MS from emulator start) that
  reproducibly advances it to a named scene, and the recommended timing window.
  Dot-source this and use Get-ReachScene to drive thor_game_speed_quality_pass.ps1
  without re-deriving navigation each run.

.EXAMPLE
  . tools\thor\thor_reach_scenes.ps1
  $s = Get-ReachScene gears3
  tools\thor\thor_game_speed_quality_pass.ps1 -Targets @($s.Iso) `
    -InputSequence $s.InputSequence -BootWaitSec $s.BootWaitSec `
    -SkipWindowSec $s.SkipWindowSec -SettleSec 8 -Label gears3

.NOTES
  Sequences are emulator-start-relative; if boot time shifts (cold vs warm),
  widen DELAY_MS. Scenes verified during the 2026-05-29 autonomous session.
#>

$script:ThorReachScenes = @{
    "gears3" = @{
        Title         = "Gears of War 3"
        Iso           = "/storage/2664-21DE/Roms/xbox360/Gears of War 3 (World) (En,Fr,Pt,Zh,Ko,Pl,Ru,Cs,Hu).iso"
        # Verified: reaches PRESS START title, then main menu, ~30 fps, no crash.
        InputSequence = "start@26000:400;a@30000:400;a@34000:400;a@40000:400"
        Scene         = "main menu (~30 fps, renders)"
        BootWaitSec   = 30
        SkipWindowSec = 30
    }
    "judgment" = @{
        Title         = "Gears of War: Judgment"
        Iso           = "/storage/2664-21DE/Roms/xbox360/Gears of War - Judgment (USA, Europe) (En,Fr,Es,Nl,No,Da,Fi,Zh,Ko,Pl,Cs,Hu).iso"
        # Verified: reaches PRESS START title screen, ~22 fps, no crash.
        InputSequence = "start@26000:400;start@31000:400;a@35000:400;start@40000:400"
        Scene         = "title screen (PRESS START, renders)"
        BootWaitSec   = 30
        SkipWindowSec = 30
    }
    "burnout" = @{
        Title         = "Burnout Revenge"
        Iso           = "/storage/2664-21DE/Roms/xbox360/Burnout Revenge (USA).iso"
        # Renders in-game (highway) at ~60 fps; eventually RtlRaiseException gameplay path.
        InputSequence = "start@24000:400;a@28000:400;start@32000:400;a@36000:400"
        Scene         = "in-game highway (~60 fps, renders)"
        BootWaitSec   = 28
        SkipWindowSec = 26
    }
    "lost_odyssey" = @{
        Title         = "Lost Odyssey"
        Iso           = "/storage/2664-21DE/Roms/xbox360/Lost Odyssey.m3u/Lost Odyssey (USA, Europe) (En,Ja,Fr,De,Es,It) (Disc 1).iso"
        # Boots + runs uncapped (~170 fps) but renders BLACK (parked guest-render lane).
        # Good UMA/upload-counter subject (heavy guest memory traffic).
        InputSequence = "start@20000:300;start@25000:300;a@28000:300;start@33000:300"
        Scene         = "boots-but-black (uncapped ~170 fps; upload-counter subject)"
        BootWaitSec   = 26
        SkipWindowSec = 30
    }
}

function Get-ReachScene {
    param([Parameter(Mandatory = $true)][string]$Name)
    $key = $Name.ToLower()
    if (-not $script:ThorReachScenes.ContainsKey($key)) {
        Write-Error "Unknown reach-scene '$Name'. Known: $($script:ThorReachScenes.Keys -join ', ')"
        return $null
    }
    return [pscustomobject]$script:ThorReachScenes[$key]
}

function Get-ReachSceneNames {
    return $script:ThorReachScenes.Keys
}
