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
        [object]$Value
    )

    $parent = Split-Path -Parent $Path
    if (!(Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    $Value | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Path -Encoding UTF8
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
    Write-JsonFile -Path $configPath -Value ([ordered]@{
        enabled = $true
        goal_name = $Name
        prompt = $Prompt
        proof_path = $Proof
        max_attempts = [Math]::Max(1, $Attempts)
        updated_at = (Get-Date).ToString("o")
    })
    if (Test-Path -LiteralPath $statePath) {
        Remove-Item -LiteralPath $statePath -Force
    }
    Write-Output "Enabled Codex goal loop: $Name"
    Write-Output "Proof marker: $(Resolve-RepoPath $Proof)"
    Write-Output "Max Stop-hook attempts: $([Math]::Max(1, $Attempts))"
}

function New-BlueDragonFullSpeedPrompt {
    $parts = @(
        "Continue the xenia-thor Blue Dragon full-speed sprint with the reset plan, not the old chronology.",
        "First read AGENTS.md, .agents/skills/xenia-blue-dragon-full-speed-autopilot/SKILL.md, .agents/skills/xenia-working-fast-autonomy/SKILL.md, .agents/skills/xenia-a64-speed-hotpath/SKILL.md, .agents/skills/xenia-codex-goal-loop/SKILL.md, .agents/skills/xenia-continual-harness-refiner/SKILL.md, docs/worklogs/20260526.md or newer, and scratch/thor-debug/codex-goal-loop.json.",
        "If scratch/thor-debug/latest-blue-dragon-full-speed-proof.json exists, summarize the proof and recommend pausing this loop.",
        "Before starting, check for another build, deploy, Blue Dragon capture, real git operation, or active Codex slice; ignore short-lived read-only status probes after confirming they are not commit/push/merge/rebase work.",
        "Current truth: no sustained 30 FPS proof exists. Latest Blue Dragon captures are still Main Thread / A64 generated-code bound, not broad Vulkan bound; the recurring wall is 82282490, 82281D28, and 82287788.",
        "Current decision notes: docs/research/20260526-075500-a64-offset-address-fastpath-quiet-ab.md, docs/research/20260526-083000-a64-no-wrap-memory-eligibility-audit.md, docs/research/20260526-092500-a64-nonclosed-gpr-cache-capture.md, docs/research/20260526-115700-a64-scalar-context-load-store-lowering.md, docs/research/20260526-121500-a64-static-superblock-feasibility.md, and docs/research/20260526-123000-continual-rerank-after-a64-closures.md. Use them as the active plan if present.",
        "Closed lanes: do not repeat f1/edge-payload behavior, stvewx, three-PC MUL_ADD_V128, fpscr, pre/post-promotion r1/r11, local-only 822824F0, broad Vulkan work, PACK/UNPACK or broad PERMUTE/LOAD_VECTOR_SHL/SHR VMX128 behavior, unchanged HIR mapper captures, ordinal-fallback HIR behavior, local 82281D28 store elision, one-off 826BF770 caller inlining, unchanged fast-entry/guest-stack/load-store/offset-helper captures, scalar context load/store behavior for the mapped wall blocks, static-superblock/alternate-entry behavior for the mapped wall blocks, current fastmem/addressing behavior without a new no-wrap proof surface, nonclosed GPR cache behavior without broader CFG/dataflow, any fast-entry patch that replaces A64Function::machine_code or the global indirection slot, and any fast-entry callsite behavior before the dirty-flush/payload protocol and stub shape are explicit.",
        "Do not make the next default slice another narrow first-CONTEXT_BARRIER audit unless it yields a reusable backend rule or closes a broad barrier class.",
        "Preferred next lane: the mature backend rerank reports decision=body_dominant_a64_micro_lanes_closed_need_fresh_route_proof_or_non_a64_lane and safe_next_patch=best_current_quiet_route_proof_refresh_or_android_ux. Next default runtime slice is a best-current quiet route proof refresh: FullDeploy current master, launch the known Blue Dragon route with -Arm64OffsetMemoryAddressFastpath true, audit/disassembly/block tracing off, delayed body-time comparators 82282490,82281D28,82287788, clean fatal-marker search, and explicit FPS/proof parsing. Treat this as fresh evidence, not a behavior patch or A64 micro-audit. Android controller/settings UX is the alternate lane if the user prioritizes playability. Reopen A64/VMX/fastmem/helper/GPU behavior only if a new clean capture shows a different body-dominant wall outside the closed 82282490/82281D28/82287788 cluster. Do not patch behavior, run another micro-audit, run a speed A/B from source-only evidence, add another fast-entry audit, use host-pointer immediate fastmem without no-wrap proof, reopen closed VMX128/nonclosed-cache behavior, enable arm64_cr_compare_branch_across_context_barrier, enable arm64_cr_store_elide_for_fused_branch, fuse single-PC barriers, repeat scalar context load/store behavior, or patch multi-block/static-superblock behavior from the closed source audits. Use -Arm64OffsetMemoryAddressFastpath true explicitly in future Blue Dragon speed captures, but keep the cvar default-off globally. Do not use -AllowOrdinalFallback for behavior decisions.",
        "Each slice must leave proof, a committed patch, a route capture/report, a dated research note, or a concrete blocker. No orientation-only slice.",
        "After two repeated/inconclusive slices, after three counter-only no-speed slices, or after any stale prompt/skill finding, run tools/thor/thor_continual_harness_review.ps1 -Mode Window and use the continual-harness refiner for prompt/skill/memory/tool CRUD before another risky Thor run.",
        "Full-speed proof means Disc 1 launched from the known Thor SD-card path with visible title/opening/gameplay evidence, quiet 180-second route speed capture, commit/APK/cvars/capture paths, clean fatal-marker search, and sustained >=30 FPS; track 60 FPS as stretch.",
        "Update dated worklogs/research, re-arm the loop with this concise prompt, then commit and push validated progress on master."
    )
    return ($parts -join " ")
}

switch ($Mode) {
    "EnableBlueDragonTitle" {
        $prompt = "Continue the Blue Dragon title milestone. Read repo-local xenia-thor skills, build/deploy only if needed, launch Blue Dragon from the Thor SD-card path, capture screenshot/logcat, inspect fatal/GPU/ARM64 fallback lines, update dated worklog/research if needed, and stop only after writing scratch/thor-debug/latest-title-proof.json or recording a concrete blocker."
        Enable-GoalLoop -Name "blue-dragon-title" -Prompt $prompt -Proof "scratch/thor-debug/latest-title-proof.json" -Attempts $MaxAttempts
    }

    "EnableBlueDragonOpeningSpeed" {
        $prompt = "Continue the Blue Dragon AYN Thor opening-speed milestone. Read AGENTS.md and repo-local xenia-thor skills, start from the latest clean route baseline, run offline analysis before risky edits, make one-variable changes, build/deploy/capture on Thor when runtime behavior changes, update dated worklog/research, commit and push validated progress on master, and stop only after writing scratch/thor-debug/latest-blue-dragon-speed-proof.json with route plus FPS evidence or recording a concrete blocker."
        Enable-GoalLoop -Name "blue-dragon-opening-speed" -Prompt $prompt -Proof "scratch/thor-debug/latest-blue-dragon-speed-proof.json" -Attempts $MaxAttempts
    }

    "EnableBlueDragonFullSpeed" {
        Enable-GoalLoop -Name "blue-dragon-full-speed" -Prompt (New-BlueDragonFullSpeedPrompt) -Proof "scratch/thor-debug/latest-blue-dragon-full-speed-proof.json" -Attempts $MaxAttempts
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
        Write-JsonFile -Path $configPath -Value ([ordered]@{
            enabled = $false
            goal_name = $name
            updated_at = (Get-Date).ToString("o")
        })
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
        Write-JsonFile -Path $resolvedProof -Value ([ordered]@{
            goal_name = $name
            status = "proof-marker"
            created_at = (Get-Date).ToString("o")
            note = "Manual proof marker. Replace or augment with capture path, APK hash, cvars, route evidence, and FPS evidence when available."
        })
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
        $config = Read-JsonFile -Path $configPath
        $state = Read-JsonFile -Path $statePath
        if ($null -eq $config) {
            Write-Output "No Codex goal-loop config found at $configPath"
            break
        }

        Write-Output "Config: $configPath"
        Write-Output "Enabled: $($config.enabled)"
        Write-Output "Goal: $($config.goal_name)"
        Write-Output "Proof: $(Resolve-RepoPath ([string]$config.proof_path))"
        Write-Output "Max attempts: $($config.max_attempts)"
        if ($null -ne $state) {
            Write-Output "Attempts used: $($state.attempts)"
            Write-Output "Last update: $($state.updated_at)"
        } else {
            Write-Output "Attempts used: 0"
        }
        if (![string]::IsNullOrWhiteSpace($config.prompt)) {
            Write-Output "Prompt preview:"
            Write-Output ([string]$config.prompt)
        }
    }
}
