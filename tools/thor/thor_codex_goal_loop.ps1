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
            "Latest evidence: docs/research/20260524-052524-82281d28-call-edge-split.md. Capture scratch/thor-debug/20260524-052031-* reached the visible opening sky/wing route with clean fatal-marker search on APK SHA A4CB9C0E8F4CBB63EB09F3DA38640BEA5410DFE5916A155627C18F3B90C39BD8. With the HIR/block dump removed, body rows were 82282490=30214411, 82281D28=10212419, 82287788=3195362, 82282388=3020644, 82282410=2902097, 821CE028=674464, and 826BF770=563064. Dynamic edges show 8228233C -> 82281D28 at body_ticks_total=7350722 and calls_total=69445, about 72.0% of final 82281D28 body time. Reported direct edges account for about 89.1%, leaving only about 1115427 ticks of approximate parent-exclusive work. Do not patch local 82281D28 generated code yet or rerun the exact HIR/block capture unchanged. Tooling update: docs/research/20260524-053502-call-path-recursive-heuristic.md adds recursive/direct-edge/exclusive body-time shares to tools/thor/thor_hir_call_path_report.ps1; validation reports 82281D28 at 89.08% direct-edge body and 71.98% self-recursive body, and 82282490 at 84.08% direct-edge body and 71.44% self-recursive body. Latest local-span ranking: docs/research/20260524-054204-82282490-exclusive-candidate-ranking.md. tools/thor/thor_hir_dynamic_block_slice_report.ps1 now prints Local Exclusive Candidate Ranking; it keeps 8228252C-822825C4 first (approx_exclusive=2876500, exclusive_pct=33.72) and 822824F0-82282528 second (approx_exclusive=2021174, exclusive_pct=57.72). Do not repeat the exact narrow stvewx stack-lane fold A/B for 82282580/82282584; it was route-clean but not a proven speed win. Latest top-span comparison: docs/research/20260524-055108-82282490-top-span-comparison.md. The focused offline audit says not to patch generated-code behavior yet. 8228252C-822825C4 remains the larger absolute local target but its safe lanes are not ready; 822824F0-82282528 is smaller but cleaner and exposes an exact lvlx/lvrx to load_vector_shl+permute to v[0] shape. Next step should add or run focused provenance for that 822824F0 vector-load join before any default-off function/span-gated codegen experiment. Previous evidence: docs/research/20260524-050931-82281d28-focused-capture.md. The focused HIR/block run made 82281D28 look first, but it was trace-heavy and could not map the hot 8228233C block because the warning-level HIR dump started at 82282398. Previous evidence: docs/research/20260524-045602-821ce028-focused-capture.md. 821CE028 is real but too small to patch immediately; CR store/barrier and broad VMX-dot lanes remain rejected. Previous evidence: docs/research/20260524-044223-822877bc-span-reduction-audit.md. For 82287788 span 822877BC-82287B38, block body was 1173620 ticks, but child edge 82287788 -> 821CE028 accounted for 1147798 ticks over 340310 calls, leaving only 25822 approximate parent-exclusive ticks (2.2%), so do not patch local 822877BC-82287B38 generated code first. Older evidence: docs/research/20260524-042555-82287788-focused-callee-split.md. The lower-noise call-edge split kept 82282490 as the main wall at body_ticks_total=31584286 and put 82287788 secondary at body_ticks_total=3659269. Older evidence: docs/research/20260524-040404-blue-dragon-stvewx-lane-fastpath.md. The default-off A64 EXTRACT_I32 fastpath for only 82282580 -> lane 0 and 82282584 -> lane 1 is route-clean and audited but did not prove speed; keep arm64_blue_dragon_stvewx_stack_lane_fastpath and audit default-off and do not repeat that exact A/B unchanged. Previous lane provenance remains docs/research/20260524-034103-8228252c-stvewx-lane-provenance.md. Previous recursive-edge evidence remains docs/research/20260524-031017-82282490-exclusive-callee-attribution.md and previous entry/exit evidence remains docs/research/20260524-025544-a64-entry-exit-profiler.md: generated prolog/epilog/stackpoint overhead is tiny, so keep a64_enable_host_guest_stack_synchronization default true and treat stack-sync-off as code-size evidence only. " +
            "Latest vector-load provenance: docs/research/20260524-060827-822824f0-vector-load-provenance.md and tools/thor/thor_hir_vector_load_join_audit.ps1. The auditor confirms 822824F0-82282528 is body-backed local work (body_ticks_total=3501617, child edge 82274DB0=1480443, approximate exclusive 2021174), but the vector join is not self-contained: 82282520 lvlx stores v[13] from r30+0x14, 82282528 lvrx stores v[0] from r30+0x20 with a zero path, and the first vor v0,v13,v0 join is 8228254C in the next span. Do not patch local 822824F0-82282528 generated code yet. " +
            "Latest cross-span consumer audit: docs/research/20260524-062037-822824f0-cross-span-consumer-audit.md. The auditor now prints vector consumer rows and shows 822824F0-82282574 contains the full lvlx/lvrx -> vsldoi -> vor -> extract/splat -> vmaddfp chain, plus four stack-ish lvx128 loads and downstream stvewx lookahead. It is body-backed (body_ticks_total=3501617, child edge 82274DB0=1480443, approximate exclusive 2021174), but not a quick local-only peephole because the three MUL_ADD_V128 lowerings include VMX FPCR handling, scratch-stack saves, PPC NaN fixup, and denormal handling. Do not patch local-only 822824F0 vector codegen. " +
            "Latest source-reviewed span estimate: docs/research/20260524-063109-82282490-span-instruction-estimate.md. tools/thor/thor_hir_a64_codegen_audit.ps1 now prints a Source-Reviewed A64 Floor Estimate. This is a heuristic source floor, not exact generated instruction count. 8228252C-822825C4 remains the better next local target than 822824F0-82282574: 8228252C-822825C4 has approximate exclusive 2876500, estimated floor 192, and 14981.77 exclusive ticks per estimated instruction, while 822824F0-82282574 has approximate exclusive 2021174, estimated floor 164, and 12324.23 exclusive ticks per estimated instruction. " +
            "Latest 82282588 stvewx provenance: docs/research/20260524-064118-82282588-stvewx-provenance.md. tools/thor/thor_hir_stvewx_lane_audit.ps1 now supports -ProvenanceStartGuest and context-store forwarding. With target slice 8228252C-822825C4 and provenance start 82282514, all three dynamic stvewx extract lanes are proven under normal 16-byte PPC stack alignment: 82282580 -> lane 0, 82282584 -> lane 1, and 82282588 -> lane 2 via r6 = r1 + 0x50, then r6 + 0x8. Do not rerun the old two-site stvewx lane-fold A/B unchanged. Next step should either implement a new default-off Blue-Dragon/function/PC-gated all-three-site EXTRACT_I32 fastpath with audit counters and route-clean proof before quiet A/B, or switch to a MUL_ADD_V128 source/audit lane for 82282568/8228256C/82282570. " +
            "Latest all-three stvewx fastpath audit: docs/research/20260524-065602-blue-dragon-all-three-stvewx-fastpath.md. The existing default-off arm64_blue_dragon_stvewx_stack_lane_fastpath now folds 82282580, 82282584, and 82282588. NativeCore and FullDeploy passed. Capture scratch/thor-debug/20260524-065132-* reached the visible opening sky/dragon-wing route on APK SHA 8A3DA22B2208AA67DE13C07383490F3BEBA14E2DC1538AAB5CE08FD035626771 with a clean fatal-marker search and audit counters fastpath=722256/2008221 fallback=0/0. This is route-clean correctness evidence, not speed proof, because audit counters and block body-time were enabled. Keep the cvar default-off in presets. Next step should be a quiet same-APK A/B or control sandwich with audit off; if that is inconclusive again, move to MUL_ADD_V128 cost for 82282568/8228256C/82282570. " +
            "Latest all-three stvewx quiet A/B: docs/research/20260524-071045-blue-dragon-all-three-stvewx-quiet-ab.md. Control scratch/thor-debug/20260524-070253-* and fastpath-on scratch/thor-debug/20260524-070613-* used the same commit a924aba56 and APK SHA 8A3DA22B2208AA67DE13C07383490F3BEBA14E2DC1538AAB5CE08FD035626771 with audit/body-time/block/disassembly off. Both reached matching opening sky/dragon-wing frames with clean fatal-marker searches. Fastpath shrank 82282490 code size 86828 -> 86708, but final 82282490 total entries were identical at 183498 and final interval deltas were only 54908 control vs 51940 fastpath-on. Treat this as no proven speed win. Keep arm64_blue_dragon_stvewx_stack_lane_fastpath default-off and stop spending immediate sprint slices on narrow stvewx lane folds unless broader state/vector work reopens them. Next lane: MUL_ADD_V128 source/cost audit for 82282568/8228256C/82282570. " +
            "Latest MUL_ADD_V128 source/cost audit: docs/research/20260524-072228-mul-add-v128-source-cost-audit.md. tools/thor/thor_hir_a64_codegen_audit.ps1 now prints a MUL_ADD_V128 Source-Cost Audit section. Both 822824F0-82282574 and 8228252C-822825C4 hit vmaddfp PCs 82282568, 8228256C, and 82282570; 8228252C-822825C4 remains the larger local target with approx_exclusive=2876500 and mul_add_v128=3. Do not patch MUL_ADD_V128 behavior yet: A64 lowering includes VMX FPCR mode, optional denormal flushing, scratch source saves, fmla, PPC NaN fast-path/repair, optional output flushing, and a destination copy; x64 also intentionally avoids fused host FMA for this opcode due test differences. Next lane should be a default-off function/span/PC-gated runtime audit for 82282568/8228256C/82282570 that counts denormal flush need, NaN-fixup entry/per-lane repair, FPCR mode switches, and source/dest alias copies before any shortcut. " +
            "Latest MUL_ADD_V128 fastpath A/B: docs/research/20260524-082324-blue-dragon-mul-add-v128-fastpath-ab.md. The default-off arm64_blue_dragon_mul_add_v128_fastpath is implemented for only Blue Dragon 82282490 PCs 82282568, 8228256C, and 82282570, gated on Thor denormal-input flushing. NativeCore and FullDeploy passed. Route-safety capture scratch/thor-debug/20260524-081212-* reached visible opening sky/dragon-wing with clean fatal-marker search. Quiet same-APK A/B captures scratch/thor-debug/20260524-081601-* control and scratch/thor-debug/20260524-081929-* fastpath-on both reached matching opening frames with clean fatal-marker searches. The fastpath shrank 82282490 code size 86828 -> 85424, but final counters were mixed: control delta=65296 total=170142, fastpath-on delta=60844 total=177562, and thread samples did not prove route-wide speed. Keep the cvar default-off and out of presets. Next continuation should move to broader 8228252C-822825C4 local-span state/vector/FPR reduction, not another immediate narrow three-PC FMA skip unless broader evidence reopens it. Previous audit evidence remains docs/research/20260524-080156-blue-dragon-mul-add-v128-audit-capture.md: the three PCs were hot while software-flush, denormal, and NaN repair counters stayed zero. " +
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
