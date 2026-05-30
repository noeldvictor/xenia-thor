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
        Status  = "REGRESSION FOUND+WORKAROUND (2026-05-30, A/B device-verified): with gpu_uma_direct_shared_memory ON (default) the present pipeline HANGS during the movie chain (VdSwap freezes, unresponsive). With it OFF, Burnout runs ~63fps straight through movies (EA->EAHD->CRRW->BG1) to a live menu. See docs/research/20260530-burnout-ingame-hard-hang.md."
        Cvars   = @{ gpu_uma_direct_shared_memory = $false }  # VALIDATED: ON wedges present on Burnout; OFF runs ~60fps past movies
        Reach   = "launch with gpu_uma_direct_shared_memory=false; movie chain auto-plays to menu @~60fps; then navigate."
    }
    "bluedragon" = @{
        Title   = "Blue Dragon"
        TitleId = "4D5307DF"
        Iso     = "/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso"
        Status  = "Reaches ACTUAL IN-GAME (2026-05-30 verified, UMA off): in-field party HUD (char 'Shu' HP40/MP30). BUT 3D world does NOT reach frontbuffer (flat light-blue, only HUD composites) and ~2.0fps. Guest IS rendering (log: 320x8192 + 160x4096 MSAA color/depth RTs created) -> our MSAA-RT resolve->frontbuffer path is the suspect, shared with Burnout in-race black-3D. See docs/research/20260530-bluedragon-ingame-black3d-2fps.md."
        Cvars   = @{ gpu_uma_direct_shared_memory = $false }
        Reach   = "multi-disc .m3u (Disc 1); auto-reaches in-game field HUD; 3D-black + 2fps blocker."
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
