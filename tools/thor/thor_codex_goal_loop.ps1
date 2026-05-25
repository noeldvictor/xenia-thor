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
            "Current priority, superseding the older chronology below: docs/research/20260525-160247-arm64-guest-state-register-cache-audit-capture.md records a route-clean Thor capture for the default-off arm64_guest_state_register_cache_audit. Use -Arm64GuestStateRegisterCacheAuditFunction 0x82282490 for guest-PC filters; the old bare 82282490 spelling reached the route but emitted no audit rows. Corrected capture scratch/thor-debug/20260525-155824-* on commit 3b02ff758 and APK SHA 4ED7B6FE876F5ADCB7D11F70BDC42AC4D7A1BF916D680E4B22BEA861069440D6 reached visible opening sky/dragon-wing with a clean fatal-marker search and behavior-neutral rows: candidate_loads=778, candidate_stores=285, clean_hits_possible=357, dirty_hits_possible=247, normal_fallback=1063, estimated_spill_pressure=2, payload_materializations_allowed=0, behavior_changed=0. Per-slot totals favor a narrow r1 lane: r1 loads/stores/clean_hits/dirty_hits/fallback=433/11/322/4/444, r11=345/274/35/243/619. Next useful slice is a default-off, function-filtered r1 clean-load replacement prototype for 0x82282490 only, with no store elision, no r11 dirty caching, no payload materialization, normal fallback preserved, and route-clean Thor proof before any quiet speed A/B. Final evidence remains CPU/JIT-heavy: 82282490=31080764, 82281D28=7471709, 82287788=3379246, Main Thread 100%, GPU Commands 7.6%; do not pivot to broad Vulkan work. " +
            "Latest Blue Dragon edge-variant design audit: docs/research/20260524-165127-a64-edge-variant-design-audit.md. New tool tools/thor/thor_a64_edge_variant_design_audit.ps1 confirms the normal A64 entry is a singleton: host entry, compiled direct calls, unresolved resolution, the guest-address indirection table, and A64Function::machine_code() all point to one normal entry per guest function. For exact hot edge 82282490:82282598 -> 82287788, an edge-specific carrier cannot safely preload a spare register, replace 82287788's normal machine-code pointer, or change the global indirection slot. Do not patch generated behavior or run a quiet speed A/B from this state-carrier evidence. Next useful slice should add a default-off compile/runtime counter-only edge-variant probe that reports eligible edge compiles/calls, payload materializations, normal-entry fallbacks, child-call kills, and variant misses, or prove exact fpscr CFG writeback/flush rules before any behavior change. " +
            "Parallel Vulkan design lane: docs/research/20260524-165737-mature-vulkan-port-speed-design.md, docs/research/20260524-171203-vulkan-counter-surface-audit.md, docs/research/20260524-173639-vulkan-perf-counter-skeleton.md, and docs/research/20260524-175206-vulkan-counter-route-sandwich.md. External emulator evidence from Dolphin, RPCS3, PCSX2, Khronos, Android, and mobile GPU guidance says mature Vulkan ports win by measuring GPU costs directly: pipeline cache misses, shader/pipeline compile stalls, barriers, render passes, readbacks, texture uploads/copies, descriptor churn, present waits, and frame pacing. The default-off cvars vulkan_trace_perf_counters and vulkan_trace_perf_counters_log_interval are wired through Android extras, Thor metadata, IssueSwap, graphics pipeline create/cache-hit, command-processor submit, presenter acquire/submit/present, barriers, and render-pass begins. The route-clean 180-second sandwich used commit 73c0d8059 and APK SHA C1F35AF00D9CED398333E591326A4E69C7178CB20B27CCCB30C3D417BD6A91A6: control scratch/thor-debug/20260524-174125-* and counter-on scratch/thor-debug/20260524-174443-* both reached visible opening sky/dragon-wing with clean fatal-marker searches. Counter-on ended with issue_swaps=840, pipeline_creates=56, queue_submits=840, barrier_submits=115307, render_pass_begins=50458, presents=841, present_failures=0, and no queue submit failures. Final thread samples still show Main Thread near one full core and GPU Commands low, so do not pivot Blue Dragon away from A64, do not start RenderDoc-only FPS claims, Swappy/frame pacing, or broad Vulkan rewrites. Next useful main slice should return to A64 state-carrier/edge-variant work unless a later capture shows GPU Commands, present/submit waits, or pipeline stalls overtaking the CPU/JIT wall. " +
            "Previous Blue Dragon state-carrier ABI audit: docs/research/20260524-163338-a64-state-carrier-abi-audit.md. New tool tools/thor/thor_a64_state_carrier_abi_audit.ps1 confirms the current A64 direct-call ABI is guest_return_only_in_x0, the existing f[1] carrier is only a callee-local 82287788 stack slot, and a broader 82282490 -> 82287788 payload is blocked without either an explicit call-ABI extension or an edge-specific compiled entry/variant with normal-entry fallback. Do not run another quiet speed A/B or patch generated behavior from the current state-carrier evidence. " +
            "Latest Blue Dragon state-carrier follow-up plan: docs/research/20260524-145241-state-carrier-followup-plan.md. New tool tools/thor/thor_hir_state_carrier_followup_plan.ps1 parses the latest route-clean state-carrier counter row and joins it with prior static audits. It keeps f[1] as a broader parent/callee design lane only: 3477646 reads and zero fallbacks, but 50.10% helper-preserved / 49.90% child-spanning traffic plus prior stack-slot and pair-entry evidence make another narrow f[1] behavior patch the wrong next move. It keeps fpscr audit-only: 3640919 dirty writes, 1182090 required writebacks, and 2924556 call kills require exact CFG writeback rules before any shortcut. Do not patch generated behavior yet. Next useful slice should design a broader 82282490 -> 82287788 parent/callee carrier with compile-time/runtime counters, or prove exact CFG writeback rules before any quiet speed A/B. " +
            "Latest Blue Dragon state-carrier audit capture: docs/research/20260524-143436-state-carrier-audit-interval-fix.md. tools/thor/thor_xenia_debug.ps1 now auto-sets Arm64SpeedProfileIntervalMs=15000 when a Blue Dragon speed capture requests row-producing speed-profiler instrumentation and the caller did not pass an explicit interval. Route-clean capture scratch/thor-debug/20260524-143436-* reached the visible opening sky/dragon-wing route on APK SHA 5A80DD15AF4471FABE16E7370D6CA5EB25FC8582993F0208BE98DB33EFCFA2FD with a clean fatal-marker search, log_level=1, arm64_speed_profile_interval_ms=15000, and the intended counter/body rows. Final counters were f1_read=3477646, f1_helper_read=1742466, f1_child_read=1735180, f1_child_call=576589, f1_fallback=0, fpscr_read=3640919, fpscr_dirty_write=3640919, fpscr_required_writeback=1182090, fpscr_call_kill=2924556, and fpscr_fallback=0. Final body-time kept 82282490 dominant at 61133316, with 82281D28=13198164 and 82287788=7477399; Main Thread remained about one full core and GPU Commands was low. Treat this as audit evidence, not a quiet FPS verdict. Do not repeat the no-interval/log-level-only captures and do not patch behavior directly from this row. Next useful slice is an offline parent/callee 82282490 -> 82287788 state-carrier design, especially a broader f[1] carrier with explicit helper, child-call, barrier, exit, exception, and fallback rules, or a CFG-aware fpscr dirty-carrier audit before any speed A/B. Previous state-carrier capture docs/research/20260524-141502-state-carrier-audit-route-capture.md is route-clean but data-blocked; follow-up scratch/thor-debug/20260524-142827-* proved log_level=1 alone was not enough without arm64_speed_profile_interval_ms. " +
            "Previous 8228252C state-carrier design audit: docs/research/20260524-133027-8228252c-state-carrier-design-audit.md. New tool tools/thor/thor_hir_state_carrier_design_audit.ps1 joins parent span HIR, callee HIR, and dynamic edge rows to print exact seed, kill, and writeback requirements for 82282490:8228252C-822825C4 and 82282598 -> 82287788. Current output classifies f[1] as a medium-risk read_only_entry_seed with loads=10, stores=0, and dynamic_load_upper=16912720; fpscr as a very-high-risk dirty_cfg_carrier with loads=26, stores=26, and writebacks at 82287ED4, 82287EDC, 82287EE4, and 82288220; r[3] as a high-risk mutable cache candidate; and lr as blocked call-link state. Do not patch behavior next or run a speed A/B from this directly. " +
            "Previous 8228252C state-carrier design constraints: docs/research/20260524-131709-8228252c-state-carrier-design-constraints.md. 8228252C-822825C4 remains the top 82282490 local/exclusive target with approx_exclusive=2876500, but the live direct-call state roundtrip at 82282598 -> 82287788 blocks another narrow behavior patch. Parent 82282490 stores r[3], f[1], fpscr, and lr before or at the call, and callee 82287788 loads all four. f[1] is read-only but only has one seed-load incremental ROI over the previous stack-slot carrier; fpscr needs CFG-aware dirty writebacks at call-visible PCs 82287ED4, 82287EDC, 82287EE4, and 82288220; r[3] is mutable; lr is call-link state. Do not patch a local PC fold, standalone f[1] thunk, or fpscr shortcut next. " +
            "Previous 82282490 branch-loop aggregate audit: docs/research/20260524-121503-82282490-branch-loop-aggregate-audit.md. Re-running tools/thor/thor_hir_dynamic_block_slice_report.ps1 and tools/thor/thor_hir_branch_state_audit.ps1 across entry 82282490-822824B8, loop head 822824B8-822824E8, and loop tail 822825F4-82282600 closes the standalone branch/GPR lane. The branch loop 822824B8 plus 822825F4 totals 2133128 local-exclusive body ticks and four CR predicate stores, but only three fallthrough-only GPR reloads, all in 822824B8; nearby entry 82282490-822824B8 adds only 249802 ticks and no reloads. Do not patch CR store/barrier fusion or a narrow branch-local GPR carrier next. " +
            "Previous 822824B8 branch-state audit: docs/research/20260524-120704-822824b8-branch-state-audit.md. New tool tools/thor/thor_hir_branch_state_audit.ps1 reports that 82282490:822824B8-822824E8 has body_ticks_total=1099164, class_stores=cr:9,gpr:7, class_loads=gpr:4, three branch predicate values also stored to CR context before context_barrier/branch, and only three fallthrough-only GPR reload opportunities: r[11], r[11], and r[31]. The loop tail 822825F4-82282600 has the same CR predicate-store shape and no reload opportunity. Do not patch 822824B8-822824E8 next: broad CR store/compare/barrier fusion is a known Blue Dragon negative lane, and the GPR upper bound is too small for another standalone carrier probe. " +
            "Latest 8228252C lane closure and pivot: docs/research/20260524-115538-8228252c-lane-closure-next-target.md. The broader 82282490:8228252C-822825C4 span remains high-value local work with approx_exclusive=2876500, store_context=27, load_context=14, mul_add=3, and stvewx=3, but isolated sub-lanes are closed for immediate patching: candidate-dead pre-call stores have no HIR DCE payoff, f[1] pair-entry ROI is only the seed context load, fpscr needs CFG-aware dirty writebacks, all-three stvewx did not prove speed, and the three-PC MUL_ADD_V128 fastpath was mixed. Do not patch 8228252C-822825C4 behavior next unless building a broader CFG-aware or interprocedural state-carrier design; the later branch-loop aggregate audit closed the 822824B8 local detour too. " +
            "Latest 82287788 fpscr dirty-cache audit: docs/research/20260524-114614-82287788-fpscr-dirty-cache-audit.md. New tool tools/thor/thor_hir_fpscr_dirty_cache_audit.ps1 reports that 82287788 has fpscr loads=26, stores=26, paired updates=26, and dynamic upper loads=43973072/stores=43973072 on the hot 82282490 -> 82287788 edge, but strict_same_window=0, cfg_phi_or_stack_carrier_required=24, and external_visibility_flush_required=2 with call writeback PCs 82287ED4, 82287EDC, 82287EE4, and 82288220. Parent 82282490 is worse: loads=12, stores=12, and all transitions require external call visibility. Do not implement an fpscr dirty-cache behavior patch next; return to broader 8228252C-822825C4 state/vector/FPR work or first design a generic CFG-aware dirty-state carrier audit with exact call/exit writebacks. " +
            "Latest 82287788 pair-entry ROI: docs/research/20260524-113615-82287788-pair-entry-roi.md. tools/thor/thor_hir_function_pair_variant_audit.ps1 now supports -CompareExistingStackCarrier. The f[1] pair-entry candidate is clean, but compared with the existing stack-slot carrier it only removes the seed context load per 82287788 call: existing_seed_context_loads=1, existing_reuse_loads=9, pair_incremental_context_loads=1, pair_incremental_static_upper=1691272. Do not implement a standalone f[1] pair-entry behavior patch next; the ABI/prolog/thunk complexity is too high after the broader f[1] carrier A/B missed speed proof. Next lane should be broader 8228252C-822825C4 state-cache/callee-variant design or an fpscr dirty-cache audit with exact writeback points. " +
            "Latest 82287788 function-pair variant audit: docs/research/20260524-112944-82287788-function-pair-variant-audit.md. New tool tools/thor/thor_hir_function_pair_variant_audit.ps1 reports that for 82282490:82282598 -> 82287788, f[1] has parent seed store at 82282594, callee loads=10, stores=0, replaceable_loads=10, unsafe_loads=0, and static_replace_upper=16912720 when child call PCs 82287854 and 82287ED4 are marked preserved. The fpscr control is blocked by callee writes (loads=26, stores=26). Do not patch behavior yet: A64Emitter::Call currently passes guest return in x0, so the next useful lane is a default-off pair-specific thunk/entry-seed design or compile-time caller-seeded callee-slot proof for f[1], preserving normal 82287788 entry semantics and parent context visibility. " +
            "Latest 8228252C state forwarding plan: docs/research/20260524-111940-8228252c-state-forwarding-plan.md. tools/thor/thor_hir_interproc_state_roundtrip_audit.ps1 now prints a Forwarding Plan for live direct-call state. For 82282490 call PC 82282598 -> 82287788, the edge is hot: calls_total=1691272 and body_ticks_total=5653971. f[1] is the only medium-risk read-only candidate (loads=10, stores=0, static_load_upper=16912720), but the previous stack-slot carrier A/B missed speed proof, so the next lane should be a function-pair/callee-variant design audit for f[1], keeping parent context visibility unless proven movable. Do not patch fpscr from this evidence even though static_load_upper=43973072 and static_store_upper=43973072; it is mutable and needs exact dirty writeback at calls, barriers, exits, exceptions, and all readers. r[3] is mutable/high-risk and lr is call-link state; do not forward either. " +
            "Latest f[1] carrier fastpath A/B: docs/research/20260524-110545-blue-dragon-f1-carrier-fastpath-ab.md. The default-off arm64_blue_dragon_f1_carrier_fastpath probe is implemented for audited 82287788 f[1] offset 296 reloads. It seeds a function-local stack slot at 82287798 and reuses it only at helper/821CE028-preserved PCs. NativeCore and FullDeploy passed. Audit capture scratch/thor-debug/20260524-105424-* reached visible opening sky/dragon-wing with clean fatal-marker search on APK SHA A2A59F7777F77B52526ABF5E401194A4A7490D71B4F7A878ED8099B0CA7557A9 and counters seed=223805/688280, reuse=443749/1365365. Quiet same-APK A/B control scratch/thor-debug/20260524-105813-* versus fastpath-on scratch/thor-debug/20260524-110156-* was route-clean but did not prove speed: control reached a later opening frame, 82287788 code size changed 35568 -> 35572, and 82282490 stayed the real wall. Keep arm64_blue_dragon_f1_carrier_fastpath and arm64_blue_dragon_f1_carrier_audit default-off and out of presets. Do not repeat this exact A/B unchanged. Next continuation should move to broader 8228252C-822825C4 parent/callee state/vector/FPR reduction with an offline audit proving how to reduce live 82282490 -> 82287788 state round-trips without hiding guest-visible r[3], f[1], fpscr, or lr state. " +
            "Previous 821CE028 f[1] clobber audit: docs/research/20260524-102113-821ce028-f1-clobber-audit.md. The child call from 82287788 no longer needs to be treated as an unknown f[1] clobber for this narrow carrier lane. Running tools/thor/thor_hir_fpr_carrier_safety_audit.ps1 on 821CE028 offset 296 reports target_loads=0, target_stores=0, and decision=no_target_context_access_observed. The only calls are call_indirect.6 return paths at 821CE1CC and 821CE1D4; the hotpath report shows only f[0] context traffic, not f[1] or fpscr. Treat direct calls to 0x821CE028 as f[1]-preserving for the parent 82287788 carrier lane, but do not patch 821CE028 or call it a speed target. " +
            "Previous FPR carrier safety audit: docs/research/20260524-101421-82287788-f1-carrier-safety-audit.md. tools/thor/thor_hir_fpr_carrier_safety_audit.ps1 classifies target f[1] loads in 82287788 by helper/unknown-call safety. For offset 296 it reports target_loads=10, target_stores=0, helper_whitelist=2, and unknown_call_blocked=8. Source review of TryEmitGprLrHelperCall shows __savegprlr_28 and __restgprlr_28 touch GPR/LR state but not FPR, VMX, or fpscr, so the first two f[1] loads are plausible helper-whitelist candidates. The remaining eight loads cross real child calls to 0x821CE028 at 82287854 and 82287ED4, so do not carry f[1] across them without a separate clobber/use proof. Do not implement a speed patch from this audit alone. Keep fpscr out of this lane. " +
            "Previous callee-local promotion audit: docs/research/20260524-100409-82287788-callee-local-promotion-audit.md. tools/thor/thor_hir_callee_local_promotion_audit.ps1 splits filtered HIR into strict promotion windows broken by labels, context barriers, calls, branches, and returns. For callee 82287788, f[1] is read-only with loads=10 and stores=0, but strict_redundant_loads=0; its 9 reload opportunities cross boundaries and 2 cross calls. fpscr is mutable with loads=26/stores=26 and no strict local wins. Do not build a blind callee-local cache. Next continuation should design or audit a default-off CFG/function-pair f[1] carrier probe for 82282490 -> 82287788 with explicit call-clobber, helper, exit, exception, barrier, and alias flush rules. Treat fpscr as high-risk unless source review proves exact dirty update forwarding. " +
            "Latest interprocedural state-roundtrip audit: docs/research/20260524-095137-82282598-82287788-state-roundtrip-audit.md. tools/thor/thor_hir_interproc_state_roundtrip_audit.ps1 joins parent/callee filtered HIR with dynamic call-edge rows. For 82282490 call PC 82282598 -> 82287788, the dynamic edge is hot: calls_total=1691272, body_ticks_total=5653971, ticks_per_call=4. Parent 82282490 stores r[3], f[1], fpscr, and lr before or at the call, and callee 82287788 loads all four tracked fields, including f[1] loads=10 and fpscr loads=26/stores=26. Do not skip these live stores. Next continuation should design or audit a default-off Blue-Dragon/function-pair carrier or callee-local promotion probe for 82282490 -> 82287788, with explicit helper/exit/exception/alias flush rules and route proof before quiet A/B. " +
            "Latest HIR DCE audit: docs/research/20260524-094213-hir-dead-state-store-dce-audit.md. tools/thor/thor_hir_dead_state_store_dce_audit.ps1 virtually removes target store_context rows before DCE and counts recursive dead assignment fallout. For exact BlueDragonCallBoundaryDead stores in 82282490:8228252C-822825C4 it reports target_store_context=13 and dead_assignments=0, so do not implement a HIR-level version of the same 13-store skip. The unsafe all-span upper bound exposes 22 dead assignments, mostly the live f[1] / fpscr path before 82282598 -> 82287788, but focused 82287788 HIR proves the callee loads f[1] and fpscr. Next continuation should audit interprocedural live argument/state round-trips for 82282598 -> 82287788, such as a guarded function-pair carrier, callee-local promotion, or direct-call argument lowering, without hiding guest-visible PPC state. " +
            "Previous call-boundary store-suppression probe: docs/research/20260524-093001-blue-dragon-call-boundary-store-suppression-probe.md. Default-off arm64_blue_dragon_call_boundary_state_suppress_dead_stores now suppresses only the 13 route-proven candidate-dead Blue Dragon 82282490 stores before 82282598 -> 82287788, never live-in r[3], f[1], fpscr, or lr. Route-safety capture scratch/thor-debug/20260524-091813-* reached visible opening sky/dragon-wing with clean fatal-marker search and proved suppressed=dead=9007635. Quiet same-APK A/B scratch/thor-debug/20260524-092153-* control versus scratch/thor-debug/20260524-092523-* suppression-on was route-clean and shrank 82282490 only 86828 -> 86768; final counters and thread samples did not prove route-wide speed. Keep this cvar default-off and out of presets. Do not repeat the exact same store-suppression A/B next. " +
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
