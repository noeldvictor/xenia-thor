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
            "Use .agents/skills/xenia-blue-dragon-full-speed-autopilot/SKILL.md first, then .agents/skills/xenia-working-fast-autonomy/SKILL.md as the concrete worker loop, then repo-local xenia-thor war-room, route-capture, experiment-gate, continual-harness refiner, A64 speed-hotpath, AArch64 Snapdragon JIT, Xenon PPC, Ghidra OODA, and Vulkan/Adreno skills as evidence requires. " +
            "Treat full-speed proof as: Blue Dragon Disc 1 launched from the known Thor SD-card path, visible route at title/opening/gameplay, no searched fatal markers, APK hash and commit recorded, quiet speed capture with profiler data, and sustained >=30 FPS target evidence for at least a representative 180-second route segment; 60 FPS is a stretch target, not the blocker for declaring Xbox-360-accurate full speed if the title is 30 FPS. " +
            "Each continuation must inspect latest worklog/research/captures, avoid repeated rejected experiments, run offline HIR/profiler/Ghidra analysis before risky code edits, make one-variable changes, and must not end after orientation only: leave proof, a committed patch, a capture/report, or a concrete blocker. If the loop starts repeating stale lanes or the prompt/skills lag behind fresh evidence, run tools/thor/thor_continual_harness_review.ps1 -Mode Window and use .agents/skills/xenia-continual-harness-refiner/SKILL.md for conservative harness CRUD before another risky Thor run. " +
            "Build/deploy/capture on Thor when needed, update dated worklog/research, commit and push validated progress on master, and stop only after writing scratch/thor-debug/latest-blue-dragon-full-speed-proof.json with route/FPS/APK/cvar/capture evidence or recording a concrete blocker and next experiment. " +
            "Current priority: keep arm64_context_promotion_gpr_livein_r1, broad GPR caches, stale vector/block peepholes, and call-edge cvars default-off unless a fresh capture specifically needs them. " +
            "Latest evidence: docs/research/20260524-034103-8228252c-stvewx-lane-provenance.md. The new tools/thor/thor_hir_stvewx_lane_audit.ps1 proves two exact stvewx lanes in 8228252C-822825C4: 82282580 -> lane 0 from r1+0x50 and 82282584 -> lane 1 from r1+0x54 under normal 16-byte PPC stack-pointer alignment. 82282588 remains unknown because it uses r6+0x8. Next implement a default-off Blue-Dragon/function/PC-gated A64 EXTRACT_I32 fastpath for only 82282580 and 82282584, with audit/counters before any quiet speed A/B. Keep broad 822824F0 vector peepholes stale unless fresh body-backed evidence reopens them. Previous codegen audit remains docs/research/20260524-033116-8228252c-a64-codegen-audit.md: the exact slice has body total 8530471, child edge 8228252C -> 82287788 body 5653971, approximate exclusive local work 2876500, and generated-code shape dominated by store_context:27, load_context:14, extract:9, splat:6, stvewx:3, and vmaddfp:3. Previous block-slice evidence remains docs/research/20260524-032005-82282490-dynamic-block-slice-report.md. Previous recursive-edge evidence remains docs/research/20260524-031017-82282490-exclusive-callee-attribution.md: direct-call edges account for 84.08% of final 82282490 body ticks, and 822825E0 -> 82282490 is mostly callee-body work, not generic call/prolog overhead. Previous entry/exit evidence remains docs/research/20260524-025544-a64-entry-exit-profiler.md: generated prolog/epilog/stackpoint overhead is tiny, so keep a64_enable_host_guest_stack_synchronization default true and treat stack-sync-off as code-size evidence only. " +
            "Previous word-loop confirmation remains docs/research/20260524-014050-word-loop-preset-rebaseline.md and docs/research/20260524-013145-82485dd8-word-loop-quiet-ab.md: the Blue Dragon speed preset sends arm64_blue_dragon_word_copy_loop_fastpath=true, the backend cvar remains globally default-off, and controls should pass -Arm64BlueDragonWordCopyLoopFastpath false when needed. " +
            "Previous 82486178 VMX loop evidence remains docs/research/20260524-005156-82486178-vmx-copy-loop-fastpath.md: arm64_blue_dragon_vmx_copy_loop_fastpath is route-clean but did not prove speed, so keep it default-off and do not combine it with the word-loop toggle yet. " +
            "Previous 82490030 split remains docs/research/20260524-001152-82490030-call-edge-split.md: dynamic child totals put 82486178 first (body_ticks_total=2302621, calls_total=33218), followed by 82485DD8 (804023) and 82486018 (679426). " +
            "Previous stability evidence remains docs/research/20260523-235800-object-release-outside-global-lock.md: the ObjectTable ReleaseHandle/RemoveHandle fix defers object->Release() until after the global critical-region lock and restored route-clean body-time rows for 8227F1D8 and 82490030. Treat the Processor::OnThreadDestroyed black-idle as fixed unless fresh owner-source evidence reopens it. " +
            "Previous route-clean parent evidence remains docs/research/20260523-153235-8227f1d8-call-edge-split.md: 8227F1D8 -> 82490030 is the hot child edge, while 826BFC7C was not meaningful in that route. " +
            "Keep arm64_blue_dragon_vmx_copy_loop_fastpath, arm64_context_promotion_gpr_livein_r1, broad GPR caches, and call-edge profiling default-off outside targeted captures. Do not globally disable a64_enable_host_guest_stack_synchronization from this mixed evidence."
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
