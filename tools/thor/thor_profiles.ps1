<#
.SYNOPSIS
  Per-game profiles for xenia-thor (the RPCS3-style per-title config we lacked).
  Each profile = exact ISO path + recommended cvars (graphics/hacks/perf) +
  verified status + reach hints. Get-GameProfile returns one; ConvertTo-ProfileIntent
  builds the --ez/--ei/--es launch-intent extras from a profile's cvars so launches
  apply per-game settings automatically.

.WHY (answers "why don't we have RPCS3's options/hacks?")
  Xenia exposes ~70+ GPU cvars but they are (a) mostly dev/debug/trace flags, not
  curated user hacks, and (b) NOT organized per-game or surfaced in a GUI - they
  only reach the emulator via the Android launch-intent allow-list (171 forwarded).
  This file adds the missing PER-GAME organization layer. cvar VALUES here are
  conservative/empty until validated per title (do not invent tuned values).

.NOTE
  cvars listed are applied at launch. Only include a cvar value once it is
  device-VALIDATED to help that title (else leave the cvars map empty = defaults).
  status/scene fields reflect only what has been screenshot-verified.
#>

$script:ThorGameProfiles = @{
    "burnout" = @{
        Title   = "Burnout Revenge"
        TitleId = "454107DC"
        Iso     = "/storage/2664-21DE/Roms/xbox360/Burnout Revenge (USA).iso"
        Status  = "Boots, skips movies, reaches SAVE/LOAD player-file menu + garage/vehicle screens @~59fps (verified read). 3D scenes render in some scenes @~59fps; framerate collapses to ~7-10fps in others (scene-dependent, under investigation). Full in-race playability not yet confirmed."
        Cvars   = @{}   # no per-game cvar validated yet; defaults
        Reach   = "movie-skip stepper -> SAVE/LOAD player-file -> select slot/Create; gameplay gated behind player-file."
    }
    "bluedragon" = @{
        Title   = "Blue Dragon"
        TitleId = "4D5307DF"
        Iso     = "/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso"
        Status  = "Boots to 'press START' title @~31fps (verified read). In a prior run reached an in-game HUD/field (party + HP/MP) but slow (~2fps). 3D DOES reach the frontbuffer for this title (contrast vs Burnout/LO black-3D)."
        Cvars   = @{}
        Reach   = "press START at title; multi-disc .m3u (Disc 1)."
    }
    "lostodyssey" = @{
        Title   = "Lost Odyssey"
        TitleId = "4D5307FA"
        Iso     = "/storage/2664-21DE/Roms/xbox360/Lost Odyssey.m3u/Lost Odyssey (USA, Europe) (En,Ja,Fr,De,Es,It) (Disc 1).iso"
        Status  = "Boots; observed black 'Loading' screen @~164fps and present_but_black states. Deeper: a guest worker thread the game joins becomes a zombie and the join never wakes (XThread::Exit signals header.signal_state but join waits on the HOST thread handle). Kernel-level, USER-GATED."
        Cvars   = @{}
        Reach   = "multi-disc .m3u (Disc 1); known to reach black/loading; thread-join stall under investigation."
    }
    "banjo" = @{
        Title   = "Banjo-Kazooie: Nuts & Bolts"
        TitleId = "4D5307ED"
        Iso     = "/storage/2664-21DE/Roms/xbox360/Banjo-Kazooie - Nuts & Bolts (USA) (En,Ja,Fr,De,Es,It,Nl,Sv,No,Zh,Ko,Pl,Ru,Cs).iso"
        Status  = "Boots; hits a 'Disc Read Error' dialog (file-IO class: NtQueryFullAttributesFile GAME:\loctext... missing) in prior runs; sometimes no_present. Not reaching gameplay."
        Cvars   = @{}
        Reach   = "file-IO/dirty-disc blocker before gameplay."
    }
}

function Get-GameProfile {
    param([Parameter(Mandatory=$true)][string]$Name)
    $k = $Name.ToLower()
    if (-not $script:ThorGameProfiles.ContainsKey($k)) {
        Write-Error "Unknown profile '$Name'. Known: $($script:ThorGameProfiles.Keys -join ', ')"
        return $null
    }
    return [pscustomobject]$script:ThorGameProfiles[$k]
}

function Get-GameProfileNames { return $script:ThorGameProfiles.Keys }

# Build launch-intent extras from a profile's Cvars map. bool->--ez, int->--ei,
# others->--es. Returns a string to append to the am start command.
function ConvertTo-ProfileIntent {
    param([Parameter(Mandatory=$true)]$Profile)
    $parts = @()
    foreach ($kv in $Profile.Cvars.GetEnumerator()) {
        $v = $kv.Value
        if ($v -is [bool]) {
            $parts += "--ez $($kv.Key) $($v.ToString().ToLower())"
        } elseif ($v -is [int] -or $v -is [long]) {
            $parts += "--ei $($kv.Key) $v"
        } else {
            $parts += "--es $($kv.Key) '$v'"
        }
    }
    return ($parts -join " ")
}
