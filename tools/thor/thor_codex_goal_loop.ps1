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
            "Latest evidence: docs/research/20260524-052524-82281d28-call-edge-split.md. Capture scratch/thor-debug/20260524-052031-* reached the visible opening sky/wing route with clean fatal-marker search on APK SHA A4CB9C0E8F4CBB63EB09F3DA38640BEA5410DFE5916A155627C18F3B90C39BD8. With the HIR/block dump removed, body rows were 82282490=30214411, 82281D28=10212419, 82287788=3195362, 82282388=3020644, 82282410=2902097, 821CE028=674464, and 826BF770=563064. Dynamic edges show 8228233C -> 82281D28 at body_ticks_total=7350722 and calls_total=69445, about 72.0% of final 82281D28 body time. Reported direct edges account for about 89.1%, leaving only about 1115427 ticks of approximate parent-exclusive work. Do not patch local 82281D28 generated code yet or rerun the exact HIR/block capture unchanged. Next step should collapse/reason about the recursive SCC and return to body-backed 82282490 local-span work, or improve the mapper enough to recover 8228233C static span context before any local 82281D28 patch. Previous evidence: docs/research/20260524-050931-82281d28-focused-capture.md. The focused HIR/block run made 82281D28 look first, but it was trace-heavy and could not map the hot 8228233C block because the warning-level HIR dump started at 82282398. Previous evidence: docs/research/20260524-045602-821ce028-focused-capture.md. 821CE028 is real but too small to patch immediately; CR store/barrier and broad VMX-dot lanes remain rejected. Previous evidence: docs/research/20260524-044223-822877bc-span-reduction-audit.md. For 82287788 span 822877BC-82287B38, block body was 1173620 ticks, but child edge 82287788 -> 821CE028 accounted for 1147798 ticks over 340310 calls, leaving only 25822 approximate parent-exclusive ticks (2.2%), so do not patch local 822877BC-82287B38 generated code first. Older evidence: docs/research/20260524-042555-82287788-focused-callee-split.md. The lower-noise call-edge split kept 82282490 as the main wall at body_ticks_total=31584286 and put 82287788 secondary at body_ticks_total=3659269. Older evidence: docs/research/20260524-040404-blue-dragon-stvewx-lane-fastpath.md. The default-off A64 EXTRACT_I32 fastpath for only 82282580 -> lane 0 and 82282584 -> lane 1 is route-clean and audited but did not prove speed; keep arm64_blue_dragon_stvewx_stack_lane_fastpath and audit default-off and do not repeat that exact A/B unchanged. Previous lane provenance remains docs/research/20260524-034103-8228252c-stvewx-lane-provenance.md. Previous recursive-edge evidence remains docs/research/20260524-031017-82282490-exclusive-callee-attribution.md and previous entry/exit evidence remains docs/research/20260524-025544-a64-entry-exit-profiler.md: generated prolog/epilog/stackpoint overhead is tiny, so keep a64_enable_host_guest_stack_synchronization default true and treat stack-sync-off as code-size evidence only. " +
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
