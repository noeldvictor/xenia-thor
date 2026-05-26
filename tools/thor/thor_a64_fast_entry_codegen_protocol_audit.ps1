[CmdletBinding()]
param(
    [string]$PayloadFlushPlanReportPath = "docs\research\20260526-104000-a64-fast-entry-payload-flush-plan.md",
    [string]$GuardProtocolReportPath = "docs\research\20260526-102500-a64-fast-entry-guard-protocol.md"
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

$payloadReportText = Read-OptionalText $PayloadFlushPlanReportPath
$guardReportText = Read-OptionalText $GuardProtocolReportPath

$codegenBlockerPatterns = @(
    'kA64GuestCallFastEntryCodegenBlockerBehaviorDisabled',
    'kA64GuestCallFastEntryCodegenBlockerGuardBlocked',
    'kA64GuestCallFastEntryCodegenBlockerPayloadFlushPlanBlocked',
    'kA64GuestCallFastEntryCodegenBlockerMissingGuardEmission',
    'kA64GuestCallFastEntryCodegenBlockerMissingPayloadPopulationEmission',
    'kA64GuestCallFastEntryCodegenBlockerMissingDirtyFlushEmission',
    'kA64GuestCallFastEntryCodegenBlockerMissingLateBoundFallbackEmission',
    'kA64GuestCallFastEntryCodegenBlockerMissingStackpointResumeEmission',
    'kA64GuestCallFastEntryCodegenBlockerMissingDebugExceptionVisibility'
)

$codegenInputPatterns = @(
    'guard_inputs',
    'payload_flush_inputs',
    'guard_emission_available',
    'payload_population_emission_available',
    'dirty_flush_emission_available',
    'late_bound_fallback_emission_available',
    'stackpoint_resume_emission_available',
    'debug_exception_visibility_available'
)

$codegenBlockerCount = Count-PresentPatterns $functionHeader $codegenBlockerPatterns
$codegenInputCount = Count-PresentPatterns $functionHeader $codegenInputPatterns

$protocolModelPresent =
    (Test-Pattern $functionHeader 'struct A64GuestCallFastEntryCodegenProtocolInputs') -and
    (Test-Pattern $functionHeader 'struct A64GuestCallFastEntryCodegenProtocolDecision') -and
    (Test-Pattern $functionHeader 'EvaluateA64GuestCallFastEntryCodegenProtocol') -and
    (Test-Pattern $functionSource 'A64GuestCallFastEntryCodegenProtocolDecision\s+EvaluateA64GuestCallFastEntryCodegenProtocol') -and
    (Test-Pattern $functionSource 'EvaluateA64GuestCallFastEntryGuard\(contract, inputs\.guard_inputs') -and
    (Test-Pattern $functionSource 'BuildA64GuestCallFastEntryPayloadFlushPlan\(\s*contract, inputs\.payload_flush_inputs') -and
    (Test-Pattern $functionSource 'A64GuestCallFastEntryContractEnablesBehavior\(contract\)') -and
    (Test-Pattern $functionSource '!inputs\.guard_emission_available') -and
    (Test-Pattern $functionSource '!inputs\.payload_population_emission_available') -and
    (Test-Pattern $functionSource '!inputs\.dirty_flush_emission_available') -and
    (Test-Pattern $functionSource '!inputs\.late_bound_fallback_emission_available') -and
    (Test-Pattern $functionSource '!inputs\.stackpoint_resume_emission_available') -and
    (Test-Pattern $functionSource '!inputs\.debug_exception_visibility_available')

$decisionModelPresent =
    (Test-Pattern $functionHeader 'guard_blockers') -and
    (Test-Pattern $functionHeader 'payload_blockers') -and
    (Test-Pattern $functionHeader 'dirty_flush_blockers') -and
    (Test-Pattern $functionHeader 'ready_for_behavior_codegen\(\) const')

$guardProtocolStillPresent =
    (Test-Pattern $functionHeader 'EvaluateA64GuestCallFastEntryGuard') -and
    (Test-Pattern $functionHeader 'A64GuestCallFastEntryGuardInputs') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryBlockerPayloadPopulationMissing') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryBlockerDirtyFlushMissing')

$payloadFlushPlanStillPresent =
    (Test-Pattern $functionHeader 'BuildA64GuestCallFastEntryPayloadFlushPlan') -and
    (Test-Pattern $functionHeader 'A64GuestCallFastEntryPayloadFlushPlanInputs') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryPlanBlockerMissingSourcePayload') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryPlanBlockerMissingDirtyFlushCoverage')

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

$protocolUnusedByGeneratedCode =
    -not (Test-Pattern $emitter 'EvaluateA64GuestCallFastEntryCodegenProtocol|A64GuestCallFastEntryCodegenProtocolInputs|ready_for_behavior_codegen') -and
    -not (Test-Pattern $backend 'EvaluateA64GuestCallFastEntryCodegenProtocol|A64GuestCallFastEntryCodegenProtocolInputs|ready_for_behavior_codegen')

$guardUnusedByGeneratedCode =
    -not (Test-Pattern $emitter 'EvaluateA64GuestCallFastEntryGuard|A64GuestCallFastEntryGuardInputs') -and
    -not (Test-Pattern $backend 'EvaluateA64GuestCallFastEntryGuard|A64GuestCallFastEntryGuardInputs')

$payloadPlanUnusedByGeneratedCode =
    -not (Test-Pattern $emitter 'BuildA64GuestCallFastEntryPayloadFlushPlan|A64GuestCallFastEntryPayloadFlushPlanInputs') -and
    -not (Test-Pattern $backend 'BuildA64GuestCallFastEntryPayloadFlushPlan|A64GuestCallFastEntryPayloadFlushPlanInputs')

$generatedPayloadBehaviorPresent =
    (Test-Pattern $emitter 'PopulateGuestCallFastEntryPayload|EmitGuestCallFastEntryPayload|guest_call_fast_entry_payload.*store|payload_population') -or
    (Test-Pattern $backend 'PopulateGuestCallFastEntryPayload|EmitGuestCallFastEntryPayload|payload_population')

$generatedDirtyFlushBehaviorPresent =
    (Test-Pattern $emitter 'FlushGuestCallFastEntryPayload|EmitGuestCallFastEntryDirtyFlush|GuestCallFastEntryDirtyFlush|payload_dirty_flush') -or
    (Test-Pattern $backend 'FlushGuestCallFastEntryPayload|EmitGuestCallFastEntryDirtyFlush|GuestCallFastEntryDirtyFlush|payload_dirty_flush')

$fastEntryStubCodePresent =
    (Test-Pattern $emitter 'EmitGuestCallFastEntry|alternate_codegen=1|fast_entry_stub|late_bound_entry') -or
    (Test-Pattern $backend 'EmitGuestCallFastEntry|late_bound_entry|alternate_codegen=1')

$fastEntryCallsiteBehaviorPresent =
    (Test-Pattern $emitter 'guest_call_fast_entry_code\(\)|guest_call_fast_entry_contract\(\)|alternate_codegen=1') -or
    (Test-Pattern $backend 'alternate_codegen=1')

$payloadReportAgrees =
    $payloadReportText -match 'payload_flush_plan_source_ready_behavior_blocked'

$guardReportAgrees =
    $guardReportText -match 'guard_protocol_source_ready_behavior_blocked'

$blockerLine = Find-Line $functionHeader 'enum A64GuestCallFastEntryCodegenProtocolBlocker'
$inputsLine = Find-Line $functionHeader 'struct A64GuestCallFastEntryCodegenProtocolInputs'
$decisionLine = Find-Line $functionHeader 'struct A64GuestCallFastEntryCodegenProtocolDecision'
$evaluatorLine = Find-Line $functionSource 'EvaluateA64GuestCallFastEntryCodegenProtocol'
$directCallLine = Find-Line $emitter 'reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)'

Write-Output "audit=a64_fast_entry_codegen_protocol"
Emit-Check "codegen_protocol_blockers_cover_required_classes" ($codegenBlockerCount -eq 9) $functionHeader $blockerLine "Codegen blockers must name behavior-disabled, guard, payload/flush plan, guard emission, payload population, dirty flush, late-bound fallback, stackpoint resume, and debug/exception visibility."
Emit-Check "codegen_protocol_inputs_cover_required_sources" ($codegenInputCount -eq 8) $functionHeader $inputsLine "Protocol inputs must compose guard inputs, payload/flush inputs, and all generated-code capabilities required before behavior."
Emit-Check "codegen_protocol_decision_model_present" $decisionModelPresent $functionHeader $decisionLine "The decision must preserve guard, payload, and dirty-flush blockers separately from the aggregate behavior block."
Emit-Check "codegen_protocol_evaluator_present" $protocolModelPresent $functionSource $evaluatorLine "The source evaluator must compose the guard and payload/flush planners and conservatively block missing generated-code capabilities."
Emit-Check "guard_protocol_still_present" $guardProtocolStillPresent $functionHeader (Find-Line $functionHeader 'EvaluateA64GuestCallFastEntryGuard') "The direct-call guard model remains the first protocol input."
Emit-Check "payload_flush_plan_still_present" $payloadFlushPlanStillPresent $functionHeader (Find-Line $functionHeader 'BuildA64GuestCallFastEntryPayloadFlushPlan') "The payload/dirty-flush planner remains the second protocol input."
Emit-Check "normal_entry_abi_still_return_x0_only" $normalEntryStillOnlyReturn $stackLayout (Find-Line $stackLayout 'x0 holds') "Normal guest entry remains the existing x0 return-address ABI."
Emit-Check "direct_calls_still_use_normal_entry" $directCallsStillNormal $emitter $directCallLine "Direct calls still branch to fn->machine_code() and pass only the guest return address."
Emit-Check "late_bound_paths_still_use_normal_entry" $lateBoundStillNormal $emitter (Find-Line $emitter 'Load host code address from indirection table') "Resolve and indirection paths still use the normal entry."
Emit-Check "codegen_protocol_unused_by_generated_code" $protocolUnusedByGeneratedCode $emitter $directCallLine "Generated code does not consult the new source-only protocol yet."
Emit-Check "guard_model_unused_by_generated_code" $guardUnusedByGeneratedCode $emitter $directCallLine "Generated code still does not call the direct-call guard evaluator."
Emit-Check "payload_flush_plan_unused_by_generated_code" $payloadPlanUnusedByGeneratedCode $emitter $directCallLine "Generated code still does not call the payload/dirty-flush planner."
Emit-Check "generated_payload_behavior_absent" (-not $generatedPayloadBehaviorPresent) $emitter 0 "No generated path populates fast-entry payload state."
Emit-Check "generated_dirty_flush_behavior_absent" (-not $generatedDirtyFlushBehaviorPresent) $emitter 0 "No generated path flushes fast-entry payload state to PPCContext."
Emit-Check "fast_entry_stub_codegen_absent" (-not $fastEntryStubCodePresent) $emitter 0 "No generated fast-entry stub or alternate callsite code exists."
Emit-Check "fast_entry_callsite_behavior_absent" (-not $fastEntryCallsiteBehaviorPresent) $emitter $directCallLine "Callsites still do not guard or branch to alternate fast-entry code."
Emit-Check "payload_report_matches_current_blocked_state" $payloadReportAgrees $functionHeader $decisionLine "The previous payload/flush note agrees generated behavior remains blocked."
Emit-Check "guard_report_matches_current_blocked_state" $guardReportAgrees $functionHeader $decisionLine "The previous guard note agrees generated behavior remains blocked."

Write-Output ("codegen_protocol blockers={0}/9 inputs={1}/8 behavior_changed=0" -f $codegenBlockerCount, $codegenInputCount)
Write-Output "required_before_behavior=guard_emission;payload_population_emission;dirty_flush_emission;late_bound_fallback_emission;stackpoint_resume_emission;debug_exception_visibility;route_clean_stub_proof;quiet_ab_after_route_safety"

if ($codegenBlockerCount -eq 9 -and $codegenInputCount -eq 8 -and
    $decisionModelPresent -and $protocolModelPresent -and
    $guardProtocolStillPresent -and $payloadFlushPlanStillPresent -and
    $normalEntryStillOnlyReturn -and $directCallsStillNormal -and
    $lateBoundStillNormal -and $protocolUnusedByGeneratedCode -and
    $guardUnusedByGeneratedCode -and $payloadPlanUnusedByGeneratedCode -and
    -not $generatedPayloadBehaviorPresent -and
    -not $generatedDirtyFlushBehaviorPresent -and
    -not $fastEntryStubCodePresent -and
    -not $fastEntryCallsiteBehaviorPresent) {
    Write-Output "decision=codegen_protocol_source_ready_behavior_blocked"
    Write-Output "safe_next_patch=lane_switch_or_behavior_neutral_emitter_planning_audit"
    Write-Output "do_not_patch=direct_callsites;machine_code_pointer;global_indirection;payload_materialization;dirty_flush_codegen;fast_entry_stub_codegen;speed_ab"
} else {
    Write-Output "decision=needs_manual_review_before_any_fast_entry_codegen_behavior"
    Write-Output "safe_next_patch=fix_failed_codegen_protocol_checks_first"
}
