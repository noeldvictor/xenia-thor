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
            "Current priority: keep arm64_context_promotion_gpr_livein_r1, broad GPR caches, call-edge cvars, and stale vector/block peepholes default-off unless fresh capture evidence justifies them. " +
            "Latest evidence: docs/research/20260522-234847-8227fee8-filtered-hir-sandwich.md. Filtered capture scratch/thor-debug/20260522-233545-* on APK SHA E92DAC2CB4E7080C196DB9656305F372DC20C189E7697A2FCCD47D1E12DA3FA3 reached visible opening, emitted warning-level 8227FEE8 OptHIR, and had no searched fatal markers. Body-time still shows 82282490 dominant (body_ticks_total=26728115, ticks_per_entry=120); 8227FEE8 is secondary (body_ticks_total=4125095, ticks_per_entry=264, code_size=49804). " +
            "8227FEE8 HIR is mostly state traffic and barriers: store_context=1426, load_context=896, context_barrier=332, branches=284, calls=78, permute=48, byte_swaps=30, with no dot/extract/splat/stvewx surface. Static vector candidates are 82280A68 and 82280E1C with lvlx/lvrx/stvlx/stvrx/vor and perm=24 each, but they lack body-time proof. Post-control scratch/thor-debug/20260522-234038-* black-idled with clean fatal search and owner_hint=miss for last_global_owner_sys_tid=7347, so route nondeterminism remains. " +
            "Next slice should run delayed 8227FEE8 block body-time with -Arm64SpeedProfileBodyTimeFilter 8227FEE8 -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileBlockFilter 8227FEE8 -Arm64SpeedProfileBlockBodyTime true, preferably without a fresh disassembly dump unless needed. If route-clean, choose the next default-off function/block-gated codegen experiment from real block body-time. If it black-idles, inspect owner_hint and improve host/non-XThread owner attribution before changing guest generated-code behavior."
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
