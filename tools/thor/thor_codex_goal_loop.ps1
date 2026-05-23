[CmdletBinding()]
param(
    [ValidateSet("Status", "Enable", "EnableBlueDragonTitle", "EnableBlueDragonOpeningSpeed", "EnableBlueDragonFullSpeed", "Disable", "Reset", "MarkProof", "ClearProof")]
    [string]$Mode = "Status",

    [string]$GoalName = "xenia-thor-goal",

    [string]$GoalPrompt = "",

    [string]$ProofPath = "scratch/thor-debug/latest-goal-proof.json",

    [int]$MaxAttempts = 12
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$scratchRoot = Join-Path $repoRoot "scratch\thor-debug"
$configPath = Join-Path $scratchRoot "codex-goal-loop.json"
$statePath = Join-Path $scratchRoot "codex-goal-loop-state.json"
$legacyFlagPath = Join-Path $scratchRoot "continue-until-blue-dragon-title.flag"

function Ensure-ScratchRoot {
    if (!(Test-Path -LiteralPath $scratchRoot)) {
        New-Item -ItemType Directory -Path $scratchRoot | Out-Null
    }
}

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return (Join-Path $repoRoot $Path)
}

function Write-JsonFile {
    param(
        [string]$Path,
        [hashtable]$Value
    )

    $parent = Split-Path -Parent $Path
    if (!(Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Read-JsonFile {
    param([string]$Path)

    if (!(Test-Path -LiteralPath $Path)) {
        return $null
    }
    return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
}

function Enable-GoalLoop {
    param(
        [string]$Name,
        [string]$Prompt,
        [string]$Proof,
        [int]$Attempts
    )

    Ensure-ScratchRoot
    Write-JsonFile -Path $configPath -Value @{
        enabled = $true
        goal_name = $Name
        prompt = $Prompt
        proof_path = $Proof
        max_attempts = [Math]::Max(1, $Attempts)
        updated_at = (Get-Date).ToString("o")
    }
    if (Test-Path -LiteralPath $statePath) {
        Remove-Item -LiteralPath $statePath -Force
    }
    Write-Output "Enabled Codex goal loop: $Name"
    Write-Output "Proof marker: $(Resolve-RepoPath $Proof)"
    Write-Output "Max Stop-hook attempts: $([Math]::Max(1, $Attempts))"
}

switch ($Mode) {
    "EnableBlueDragonTitle" {
        $prompt = "Continue the Blue Dragon title milestone. Use repo-local xenia-thor skills, build/deploy if needed, launch Blue Dragon from the Thor SD-card path, capture screenshot/logcat, inspect fatal/GPU/ARM64 fallback lines, update the dated worklog and research if needed, and stop only after writing scratch/thor-debug/latest-title-proof.json or recording a concrete blocker."
        Enable-GoalLoop -Name "blue-dragon-title" -Prompt $prompt -Proof "scratch/thor-debug/latest-title-proof.json" -Attempts $MaxAttempts
    }

    "EnableBlueDragonOpeningSpeed" {
        $prompt = "Continue the Blue Dragon AYN Thor speed milestone. Use repo-local xenia-thor war-room, route-capture, experiment-gate, and A64 speed-hotpath skills. Start from the latest clean route baseline, run offline HIR/profiler analysis before code edits, make one-variable changes, build/deploy/capture on Thor when needed, update dated worklog/research, commit and push validated progress on master, and stop only after writing scratch/thor-debug/latest-blue-dragon-speed-proof.json with route plus FPS evidence or recording a concrete blocker."
        Enable-GoalLoop -Name "blue-dragon-opening-speed" -Prompt $prompt -Proof "scratch/thor-debug/latest-blue-dragon-speed-proof.json" -Attempts $MaxAttempts
    }

    "EnableBlueDragonFullSpeed" {
        $prompt = (
            "Continue researching and implementing toward full-speed Blue Dragon on AYN Thor. " +
            "Use .agents/skills/xenia-blue-dragon-full-speed-autopilot/SKILL.md first, then .agents/skills/xenia-working-fast-autonomy/SKILL.md as the concrete worker loop, then repo-local xenia-thor war-room, route-capture, experiment-gate, A64 speed-hotpath, AArch64 Snapdragon JIT, Xenon PPC, Ghidra OODA, and Vulkan/Adreno skills as evidence requires. " +
            "Treat full-speed proof as: Blue Dragon Disc 1 launched from the known Thor SD-card path, visible route at title/opening/gameplay, no searched fatal markers, APK hash and commit recorded, quiet speed capture with profiler data, and sustained >=30 FPS target evidence for at least a representative 180-second route segment; 60 FPS is a stretch target, not the blocker for declaring Xbox-360-accurate full speed if the title is 30 FPS. " +
            "Each continuation must inspect latest worklog/research/captures, avoid repeated rejected experiments, run offline HIR/profiler/Ghidra analysis before risky code edits, make one-variable changes, and must not end after orientation only: leave proof, a committed patch, a capture/report, or a concrete blocker. " +
            "Build/deploy/capture on Thor when needed, update dated worklog/research, commit and push validated progress on master, and stop only after writing scratch/thor-debug/latest-blue-dragon-full-speed-proof.json with route/FPS/APK/cvar/capture evidence or recording a concrete blocker and next experiment. " +
            "Current priority: r1 live-in replacement-count passed but quiet A/B did not prove speed, so keep arm64_context_promotion_gpr_livein_r1 default-off and do not repeat the exact A/B unchanged. " +
            "The 82282490 block body-time profiler now exists. Keep arm64_speed_profile_block_body_time default-off; use it only for target selection with an exact function/block filter. Capture scratch/thor-debug/20260522-170927-* reached the visible opening route with no searched fatal markers and showed 822825E0 dominates body-time (body_total=34726883, peak=14525259, peak_tpe=61), then 822825C8 (body_total=3216407, peak=1041116, peak_tpe=500), then 822824F0 (body_total=1280491, peak=554835, peak_tpe=1). " +
            "Do not start the 822824F0 stvewx peephole from stale entry-count evidence. The call-path audit docs/research/20260522-173542-82282490-call-path-audit.md shows the charged wall is inclusive call work: 822825E0 -> 0x82282490 charged_body_total=34726883, and 822825C8 -> 0x8227FEE8 charged_body_total=3216407. The targeted 8227FEE8 dump attempt scratch/thor-debug/20260522-172738-* idled before the route and emitted no callee dump. The default-off call-edge profiler now exists: docs/research/20260522-181040-a64-call-edge-profiler.md, cvar arm64_speed_profile_call_edge_filter, and Thor flag -Arm64SpeedProfileCallEdgeFilter. The first capture found and fixed an x9 callee-address clobber. The call-edge audit-only follow-up docs/research/20260522-190502-call-edge-audit-only-and-edge-proof.md adds arm64_speed_profile_call_edge_audit_only and corrects the route read: audit-only plus delayed 82282490 body-time reached opening and logged blocks=164 direct_call_edges=60 instrumentation=0, while audit-only without the body-time filter black-idled before 82282490. Real call-edge profiling plus delayed body-time reached opening and produced dynamic rows: 822825E0 -> 82282490 dominates body_ticks_total=21299726 peak=7315115, followed by 822825C8 -> 8227FEE8 body_ticks_total=4515376 peak=1933191. Keep both call-edge cvars default-off, but future edge captures for this route should include -Arm64SpeedProfileBodyTimeFilter 82282490 -Arm64SpeedProfileBodyTimeAfterMs 120000. Focused 8227FEE8 note docs/research/20260522-193049-8227fee8-focused-capture.md measured route-clean 8227FEE8 body_ticks_total=3650429 peak=1408271 peak_tpe=738 code_size=49804 in scratch/thor-debug/20260522-191427-*, but the stable speed lane suppressed HIR. Filtered dumps now log at warning level and Thor metadata records disassemble_function_filter; first patched filtered run scratch/thor-debug/20260522-192429-* black-idled before route and emitted no HIR/body rows. The patched no-filter control docs/research/20260522-230909-patched-route-control-black-idle.md / scratch/thor-debug/20260522-230518-* also black-idled before route: no filter, delayed body-time 82282490,8227FEE8, entry_delta=0 from 23:06:05, no target body rows, black screenshot, no fatal markers, idle snapshot blocked by last_global_owner_sys_tid=21741. Do not run filtered 8227FEE8 capture next; add route-stability or idle attribution around the flatline first."
        )
        Enable-GoalLoop -Name "blue-dragon-full-speed" -Prompt $prompt -Proof "scratch/thor-debug/latest-blue-dragon-full-speed-proof.json" -Attempts $MaxAttempts
    }

    "Enable" {
        if ([string]::IsNullOrWhiteSpace($GoalPrompt)) {
            $GoalPrompt = "Continue the configured xenia-thor goal. Check repo state and latest Thor evidence, choose one narrow next action, update dated worklogs/research, commit and push validated progress on master, and stop only after writing the configured proof marker or recording a concrete blocker."
        }
        Enable-GoalLoop -Name $GoalName -Prompt $GoalPrompt -Proof $ProofPath -Attempts $MaxAttempts
    }

    "Disable" {
        Ensure-ScratchRoot
        $config = Read-JsonFile -Path $configPath
        $name = $GoalName
        if ($null -ne $config -and ![string]::IsNullOrWhiteSpace($config.goal_name)) {
            $name = [string]$config.goal_name
        }
        Write-JsonFile -Path $configPath -Value @{
            enabled = $false
            goal_name = $name
            updated_at = (Get-Date).ToString("o")
        }
        if (Test-Path -LiteralPath $legacyFlagPath) {
            Remove-Item -LiteralPath $legacyFlagPath -Force
        }
        Write-Output "Disabled Codex goal loop."
    }

    "Reset" {
        if (Test-Path -LiteralPath $statePath) {
            Remove-Item -LiteralPath $statePath -Force
            Write-Output "Removed $statePath"
        } else {
            Write-Output "No goal-loop state file found."
        }
    }

    "MarkProof" {
        Ensure-ScratchRoot
        $config = Read-JsonFile -Path $configPath
        $proof = $ProofPath
        $name = $GoalName
        if ($null -ne $config) {
            if (![string]::IsNullOrWhiteSpace($config.proof_path)) {
                $proof = [string]$config.proof_path
            }
            if (![string]::IsNullOrWhiteSpace($config.goal_name)) {
                $name = [string]$config.goal_name
            }
        }
        $resolvedProof = Resolve-RepoPath $proof
        Write-JsonFile -Path $resolvedProof -Value @{
            goal_name = $name
            status = "proof-marker"
            created_at = (Get-Date).ToString("o")
            note = "Manual proof marker. Replace or augment with capture path, APK hash, and route/FPS evidence when available."
        }
        Write-Output "Wrote proof marker: $resolvedProof"
    }

    "ClearProof" {
        $config = Read-JsonFile -Path $configPath
        $proof = $ProofPath
        if ($null -ne $config -and ![string]::IsNullOrWhiteSpace($config.proof_path)) {
            $proof = [string]$config.proof_path
        }
        $resolvedProof = Resolve-RepoPath $proof
        if (Test-Path -LiteralPath $resolvedProof) {
            Remove-Item -LiteralPath $resolvedProof -Force
            Write-Output "Removed proof marker: $resolvedProof"
        } else {
            Write-Output "No proof marker found at: $resolvedProof"
        }
    }

    "Status" {
        Ensure-ScratchRoot
        Write-Output "Repo: $repoRoot"
        Write-Output "Hook config: $configPath"
        $config = Read-JsonFile -Path $configPath
        if ($null -eq $config) {
            Write-Output "Goal loop config: absent"
        } else {
            $config | ConvertTo-Json -Depth 8
            if (![string]::IsNullOrWhiteSpace($config.proof_path)) {
                $resolvedProof = Resolve-RepoPath ([string]$config.proof_path)
                Write-Output "Resolved proof marker: $resolvedProof"
                Write-Output "Proof exists: $(Test-Path -LiteralPath $resolvedProof)"
            }
        }
        if (Test-Path -LiteralPath $statePath) {
            Write-Output "State:"
            Get-Content -LiteralPath $statePath
        } else {
            Write-Output "State: absent"
        }
        Write-Output "Legacy title flag exists: $(Test-Path -LiteralPath $legacyFlagPath)"
    }
}
