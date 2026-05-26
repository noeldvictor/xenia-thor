[CmdletBinding()]
param(
    [string]$FastEntryTargetRowReportPath = "docs\research\20260526-053600-a64-guest-call-fast-entry-target-row-capture.md",
    [string]$StubSkeletonReportPath = "docs\research\20260526-101000-a64-fast-entry-stub-skeleton.md"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Read-RepoFile {
    param([string]$RelativePath)

    $path = Join-Path $repoRoot $RelativePath
    return [ordered]@{
        Path = $RelativePath
        FullPath = $path
        Text = Get-Content -LiteralPath $path -Raw
        Lines = Get-Content -LiteralPath $path
    }
}

function Read-OptionalText {
    param([string]$RelativePath)

    $path = Join-Path $repoRoot $RelativePath
    if (!(Test-Path -LiteralPath $path)) {
        return ""
    }
    return Get-Content -LiteralPath $path -Raw
}

function Find-Line {
    param(
        [object]$File,
        [string]$Pattern
    )

    for ($i = 0; $i -lt $File.Lines.Count; $i++) {
        if ($File.Lines[$i] -match $Pattern) {
            return ($i + 1)
        }
    }
    return 0
}

function Test-Pattern {
    param(
        [object]$File,
        [string]$Pattern
    )

    return [regex]::IsMatch($File.Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
}

function Format-Source {
    param(
        [object]$File,
        [int]$Line
    )

    if ($Line -le 0) {
        return "$($File.Path):?"
    }
    return "$($File.Path):$Line"
}

function Emit-Check {
    param(
        [string]$Name,
        [bool]$Ok,
        [object]$File,
        [int]$Line,
        [string]$Reason
    )

    Write-Output ("check {0}={1} source={2} reason={3}" -f $Name, $Ok.ToString().ToLowerInvariant(), (Format-Source $File $Line), $Reason)
}

function Find-ReportInt {
    param(
        [string]$Text,
        [string]$Name
    )

    if (!$Text) {
        return -1
    }
    $pattern = [regex]::Escape($Name) + "[`:= ]+(\d+)"
    $match = [regex]::Match($Text, $pattern)
    if (!$match.Success) {
        return -1
    }
    return [int]$match.Groups[1].Value
}

function Count-PresentPatterns {
    param(
        [object]$File,
        [string[]]$Patterns
    )

    $count = 0
    foreach ($pattern in $Patterns) {
        if (Test-Pattern $File $pattern) {
            $count++
        }
    }
    return $count
}

$functionHeader = Read-RepoFile "src\xenia\cpu\backend\a64\a64_function.h"
$functionSource = Read-RepoFile "src\xenia\cpu\backend\a64\a64_function.cc"
$emitter = Read-RepoFile "src\xenia\cpu\backend\a64\a64_emitter.cc"
$backend = Read-RepoFile "src\xenia\cpu\backend\a64\a64_backend.cc"
$stackLayout = Read-RepoFile "src\xenia\cpu\backend\a64\a64_stack_layout.h"

$targetRowReportText = Read-OptionalText $FastEntryTargetRowReportPath
$stubSkeletonReportText = Read-OptionalText $StubSkeletonReportPath

$guardBlockerPatterns = @(
    'kA64GuestCallFastEntryBlockerBehaviorDisabled',
    'kA64GuestCallFastEntryBlockerContractIncomplete',
    'kA64GuestCallFastEntryBlockerMissingFastEntryCode',
    'kA64GuestCallFastEntryBlockerNotDirectGuestCall',
    'kA64GuestCallFastEntryBlockerTargetUnresolved',
    'kA64GuestCallFastEntryBlockerPayloadPopulationMissing',
    'kA64GuestCallFastEntryBlockerDirtyFlushMissing',
    'kA64GuestCallFastEntryBlockerDirtyFlushIncomplete',
    'kA64GuestCallFastEntryBlockerLateBoundFallbackMissing',
    'kA64GuestCallFastEntryBlockerStackpointResumeMissing',
    'kA64GuestCallFastEntryBlockerDebugExceptionVisibility'
)

$guardInputPatterns = @(
    'direct_guest_call',
    'target_resolved',
    'payload_population_available',
    'dirty_flush_codegen_available',
    'late_bound_fallback_available',
    'stackpoint_resume_supported',
    'crosses_debug_or_exception_boundary',
    'available_dirty_flush_mask'
)

$guardBlockerCount = Count-PresentPatterns $functionHeader $guardBlockerPatterns
$guardInputCount = Count-PresentPatterns $functionHeader $guardInputPatterns

$guardDecisionPresent =
    (Test-Pattern $functionHeader 'struct A64GuestCallFastEntryGuardDecision') -and
    (Test-Pattern $functionHeader 'uint32_t blockers') -and
    (Test-Pattern $functionHeader 'required_dirty_flush_mask') -and
    (Test-Pattern $functionHeader 'can_use_fast_entry\(\) const')

$guardEvaluatorPresent =
    (Test-Pattern $functionHeader 'EvaluateA64GuestCallFastEntryGuard') -and
    (Test-Pattern $functionSource 'A64GuestCallFastEntryGuardDecision EvaluateA64GuestCallFastEntryGuard') -and
    (Test-Pattern $functionSource 'A64GuestCallFastEntryContractEnablesBehavior\(contract\)') -and
    (Test-Pattern $functionSource 'A64GuestCallFastEntryContractCoversStubSkeleton\(contract\)') -and
    (Test-Pattern $functionSource '!fast_entry_code') -and
    (Test-Pattern $functionSource '!inputs\.direct_guest_call') -and
    (Test-Pattern $functionSource '!inputs\.target_resolved') -and
    (Test-Pattern $functionSource '!inputs\.payload_population_available') -and
    (Test-Pattern $functionSource '!inputs\.dirty_flush_codegen_available') -and
    (Test-Pattern $functionSource 'inputs\.available_dirty_flush_mask') -and
    (Test-Pattern $functionSource '!inputs\.late_bound_fallback_available') -and
    (Test-Pattern $functionSource '!inputs\.stackpoint_resume_supported') -and
    (Test-Pattern $functionSource 'inputs\.crosses_debug_or_exception_boundary')

$stubSkeletonPresent =
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryFlagStubSkeleton') -and
    (Test-Pattern $functionSource 'SetupGuestCallFastEntryStubSkeleton') -and
    (Test-Pattern $backend 'arm64_guest_call_fast_entry_stub_skeleton,\s*false')

$normalEntryStillOnlyReturn =
    (Test-Pattern $stackLayout 'x0 holds the guest PPC return') -and
    (Test-Pattern $backend 'Pass guest return address in x0')

$directCallsStillNormal =
    (Test-Pattern $emitter 'reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)') -and
    (Test-Pattern $emitter 'StackLayout::GUEST_CALL_RET_ADDR') -and
    (Test-Pattern $emitter 'blr\(x9\)')

$lateBoundStillNormal =
    (Test-Pattern $emitter 'ResolveFunction') -and
    (Test-Pattern $emitter 'Load host code address from indirection table')

$guardUnusedByCodegen =
    -not (Test-Pattern $emitter 'EvaluateA64GuestCallFastEntryGuard|A64GuestCallFastEntryGuardInputs') -and
    -not (Test-Pattern $backend 'EvaluateA64GuestCallFastEntryGuard|A64GuestCallFastEntryGuardInputs')

$directCallsiteGuardAbsent =
    -not (Test-Pattern $emitter 'guest_call_fast_entry_code\(\)|guest_call_fast_entry_contract\(\)|EvaluateA64GuestCallFastEntryGuard')

$payloadPopulationCodePresent =
    (Test-Pattern $emitter 'PopulateGuestCallFastEntryPayload|EmitGuestCallFastEntryPayload|payload_population') -or
    (Test-Pattern $backend 'PopulateGuestCallFastEntryPayload|EmitGuestCallFastEntryPayload|payload_population')

$dirtyFlushCodePresent =
    (Test-Pattern $emitter 'FlushGuestCallFastEntryPayload|EmitGuestCallFastEntryDirtyFlush|GuestCallFastEntryDirtyFlush|payload_dirty_flush') -or
    (Test-Pattern $backend 'FlushGuestCallFastEntryPayload|EmitGuestCallFastEntryDirtyFlush|GuestCallFastEntryDirtyFlush|payload_dirty_flush')

$fastEntryStubCodePresent =
    (Test-Pattern $emitter 'EmitGuestCallFastEntry|alternate_codegen=1|fast_entry_stub|late_bound_entry') -or
    (Test-Pattern $backend 'EmitGuestCallFastEntry|late_bound_entry|alternate_codegen=1')

$stubReportAgrees =
    $stubSkeletonReportText -match 'source skeleton' -and
    $stubSkeletonReportText -match 'payload population' -and
    $stubSkeletonReportText -match 'dirty flush codegen' -and
    $stubSkeletonReportText -match 'stub.*codegen' -and
    $stubSkeletonReportText -match 'callsite'

$runtimeUnresolvedTargets = Find-ReportInt $targetRowReportText "unresolved_direct_targets"
$runtimeNormalFallback = Find-ReportInt $targetRowReportText "normal_entry_fallback"
$runtimeStackpointSensitive = Find-ReportInt $targetRowReportText "stackpoint_sensitive"
$runtimeDirtyFlushPoints = Find-ReportInt $targetRowReportText "dirty_flush_points"
$runtimeFlushContextBarrier = Find-ReportInt $targetRowReportText "flush_context_barrier"
$runtimeParentPreCallFlushPoints = Find-ReportInt $targetRowReportText "parent_pre_call_flush_points"
$runtimeKnownFirstUse = Find-ReportInt $targetRowReportText "callee_first_use_known"

$guardLine = Find-Line $functionHeader 'enum A64GuestCallFastEntryGuardBlocker'
$inputsLine = Find-Line $functionHeader 'struct A64GuestCallFastEntryGuardInputs'
$decisionLine = Find-Line $functionHeader 'struct A64GuestCallFastEntryGuardDecision'
$evaluatorLine = Find-Line $functionSource 'A64GuestCallFastEntryGuardDecision EvaluateA64GuestCallFastEntryGuard'
$stubLine = Find-Line $backend 'arm64_guest_call_fast_entry_stub_skeleton'
$directCallLine = Find-Line $emitter 'reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)'

Write-Output "audit=a64_fast_entry_guard_protocol"
Emit-Check "guard_blockers_cover_required_classes" ($guardBlockerCount -eq 11) $functionHeader $guardLine "Guard blockers must name behavior-disabled, contract, code, direct-call, unresolved, payload, dirty-flush, fallback, stackpoint, and debug/exception gates."
Emit-Check "guard_inputs_cover_runtime_contracts" ($guardInputCount -eq 8) $functionHeader $inputsLine "Guard inputs must represent direct/resolved calls, payload and dirty-flush availability, fallback, stackpoint, debug/exception, and available flush coverage."
Emit-Check "guard_decision_model_present" $guardDecisionPresent $functionHeader $decisionLine "Guard evaluation needs a blocker bitmask plus required dirty-flush mask before behavior work."
Emit-Check "guard_evaluator_blocks_missing_protocols" $guardEvaluatorPresent $functionSource $evaluatorLine "The source evaluator must conservatively block when any required protocol input is missing."
Emit-Check "stub_skeleton_present" $stubSkeletonPresent $backend $stubLine "The behavior-neutral skeleton remains present and default-off."
Emit-Check "normal_entry_abi_still_return_x0_only" $normalEntryStillOnlyReturn $stackLayout (Find-Line $stackLayout 'Convention: at guest function entry, x0 holds') "Normal guest entry still receives only the guest return address in x0."
Emit-Check "direct_calls_still_use_normal_entry" $directCallsStillNormal $emitter $directCallLine "Direct calls still branch to fn->machine_code() and pass only the guest return address."
Emit-Check "late_bound_paths_still_use_normal_entry" $lateBoundStillNormal $emitter (Find-Line $emitter 'Load host code address from indirection table') "Resolve and indirection paths still use the normal entry."
Emit-Check "guard_model_unused_by_codegen" $guardUnusedByCodegen $emitter $directCallLine "Generated code does not call the guard evaluator yet."
Emit-Check "direct_callsite_guard_absent" $directCallsiteGuardAbsent $emitter $directCallLine "Direct callsites still do not test fast-entry code or contract metadata."
Emit-Check "payload_population_codegen_absent" (-not $payloadPopulationCodePresent) $emitter 0 "No generated code populates r3-r10/lr payloads yet."
Emit-Check "dirty_flush_codegen_absent" (-not $dirtyFlushCodePresent) $emitter 0 "No generated code flushes fast-entry payloads to PPCContext yet."
Emit-Check "fast_entry_stub_codegen_absent" (-not $fastEntryStubCodePresent) $emitter 0 "No generated fast-entry stub or alternate callsite code exists yet."
Emit-Check "stub_report_matches_guard_state" $stubReportAgrees $functionHeader $decisionLine "The previous skeleton note agrees that behavior is still blocked."

Write-Output ("guard_protocol blockers={0}/11 inputs={1}/8 behavior_changed=0" -f $guardBlockerCount, $guardInputCount)
Write-Output ("previous_runtime_blockers unresolved_direct_targets={0} normal_entry_fallback={1} stackpoint_sensitive={2} dirty_flush_points={3} flush_context_barrier={4} parent_pre_call_flush_points={5} callee_first_use_known={6}" -f `
    $runtimeUnresolvedTargets,
    $runtimeNormalFallback,
    $runtimeStackpointSensitive,
    $runtimeDirtyFlushPoints,
    $runtimeFlushContextBarrier,
    $runtimeParentPreCallFlushPoints,
    $runtimeKnownFirstUse)
Write-Output "required_before_behavior=generated_payload_population;generated_dirty_flush_for_all_required_boundaries;late_bound_normal_fallback;stackpoint_resume_flush_or_invalidate;direct_call_guard_codegen;route_clean_proof_then_quiet_ab"

if ($guardBlockerCount -eq 11 -and $guardInputCount -eq 8 -and
    $guardDecisionPresent -and $guardEvaluatorPresent -and
    $stubSkeletonPresent -and $normalEntryStillOnlyReturn -and
    $directCallsStillNormal -and $lateBoundStillNormal -and
    $guardUnusedByCodegen -and $directCallsiteGuardAbsent -and
    -not $payloadPopulationCodePresent -and
    -not $dirtyFlushCodePresent -and
    -not $fastEntryStubCodePresent) {
    Write-Output "decision=guard_protocol_source_ready_behavior_blocked"
    Write-Output "safe_next_patch=payload_flush_and_codegen_protocol_gates_present_continue_with_lane_switch_or_behavior_neutral_emitter_planning_audit"
    Write-Output "do_not_patch=callsite_branch;machine_code_pointer;global_indirection;payload_materialization;speed_ab"
} else {
    Write-Output "decision=needs_manual_review_before_any_fast_entry_behavior"
    Write-Output "safe_next_patch=fix_failed_guard_protocol_checks_first"
}
