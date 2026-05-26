[CmdletBinding()]
param(
    [string]$GuardProtocolReportPath = "docs\research\20260526-102500-a64-fast-entry-guard-protocol.md",
    [string]$DirtyFlushProtocolReportPath = "docs\research\20260526-094500-a64-fast-entry-dirty-flush-protocol.md"
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

$guardReportText = Read-OptionalText $GuardProtocolReportPath
$dirtyFlushReportText = Read-OptionalText $DirtyFlushProtocolReportPath

$planBlockerPatterns = @(
    'kA64GuestCallFastEntryPlanBlockerMissingSourcePayload',
    'kA64GuestCallFastEntryPlanBlockerMissingWritablePayloadSlots',
    'kA64GuestCallFastEntryPlanBlockerMissingDirtyFlushCoverage',
    'kA64GuestCallFastEntryPlanBlockerMissingContextWriteback',
    'kA64GuestCallFastEntryPlanBlockerMissingStackpointResumeFlush',
    'kA64GuestCallFastEntryPlanBlockerMissingDebugExceptionFlush'
)

$planInputPatterns = @(
    'available_source_payload_mask',
    'writable_payload_mask',
    'dirty_payload_mask',
    'available_dirty_flush_mask',
    'payload_slots_writable',
    'context_writeback_available',
    'stackpoint_resume_flush_available',
    'debug_exception_flush_available'
)

$planBlockerCount = Count-PresentPatterns $functionHeader $planBlockerPatterns
$planInputCount = Count-PresentPatterns $functionHeader $planInputPatterns

$planModelPresent =
    (Test-Pattern $functionHeader 'struct A64GuestCallFastEntryPayloadFlushPlanInputs') -and
    (Test-Pattern $functionHeader 'struct A64GuestCallFastEntryPayloadFlushPlan') -and
    (Test-Pattern $functionHeader 'BuildA64GuestCallFastEntryPayloadFlushPlan') -and
    (Test-Pattern $functionSource 'A64GuestCallFastEntryPayloadFlushPlan\s+BuildA64GuestCallFastEntryPayloadFlushPlan') -and
    (Test-Pattern $functionSource 'A64GuestCallFastEntryContractCoversStubSkeleton\(contract\)') -and
    (Test-Pattern $functionSource 'available_source_payload_mask') -and
    (Test-Pattern $functionSource 'writable_payload_mask') -and
    (Test-Pattern $functionSource 'dirty_payload_mask') -and
    (Test-Pattern $functionSource 'available_dirty_flush_mask') -and
    (Test-Pattern $functionSource 'context_writeback_available') -and
    (Test-Pattern $functionSource 'stackpoint_resume_flush_available') -and
    (Test-Pattern $functionSource 'debug_exception_flush_available')

$requiredPayloadMaskPresent =
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryRequiredPayloadMask') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryPayloadGpr3') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryPayloadGpr10') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryPayloadLr')

$requiredDirtyFlushMaskPresent =
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryRequiredDirtyFlushMask') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryFlushContextBarrier') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryFlushHostCall') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryFlushDebugTrap') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryFlushException') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryFlushUnresolvedTarget')

$guardProtocolStillPresent =
    (Test-Pattern $functionHeader 'EvaluateA64GuestCallFastEntryGuard') -and
    (Test-Pattern $functionSource 'A64GuestCallFastEntryGuardDecision EvaluateA64GuestCallFastEntryGuard') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryBlockerPayloadPopulationMissing') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryBlockerDirtyFlushMissing')

$normalEntryStillOnlyReturn =
    (Test-Pattern $stackLayout 'x0 holds the guest PPC return') -and
    (Test-Pattern $backend 'Pass guest return address in x0')

$directCallsStillNormal =
    (Test-Pattern $emitter 'reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)') -and
    (Test-Pattern $emitter 'StackLayout::GUEST_CALL_RET_ADDR') -and
    (Test-Pattern $emitter 'blr\(x9\)')

$planUnusedByGeneratedCode =
    -not (Test-Pattern $emitter 'BuildA64GuestCallFastEntryPayloadFlushPlan|A64GuestCallFastEntryPayloadFlushPlanInputs') -and
    -not (Test-Pattern $backend 'BuildA64GuestCallFastEntryPayloadFlushPlan|A64GuestCallFastEntryPayloadFlushPlanInputs')

$generatedPayloadBehaviorPresent =
    (Test-Pattern $emitter 'PopulateGuestCallFastEntryPayload|EmitGuestCallFastEntryPayload|guest_call_fast_entry_payload.*store') -or
    (Test-Pattern $backend 'PopulateGuestCallFastEntryPayload|EmitGuestCallFastEntryPayload')

$generatedDirtyFlushBehaviorPresent =
    (Test-Pattern $emitter 'FlushGuestCallFastEntryPayload|EmitGuestCallFastEntryDirtyFlush|GuestCallFastEntryDirtyFlush|payload_dirty_flush') -or
    (Test-Pattern $backend 'FlushGuestCallFastEntryPayload|EmitGuestCallFastEntryDirtyFlush|GuestCallFastEntryDirtyFlush|payload_dirty_flush')

$fastEntryCallsiteBehaviorPresent =
    (Test-Pattern $emitter 'guest_call_fast_entry_code\(\)|guest_call_fast_entry_contract\(\)|EvaluateA64GuestCallFastEntryGuard|alternate_codegen=1') -or
    (Test-Pattern $backend 'alternate_codegen=1')

$guardReportAgrees =
    $guardReportText -match 'guard_protocol_source_ready_behavior_blocked'

$dirtyFlushReportAgrees =
    $dirtyFlushReportText -match 'Payload metadata covers' -or
    $dirtyFlushReportText -match 'Generated behavior remains absent' -or
    $dirtyFlushReportText -match 'stub_skeleton_source_ready_but_behavior_blocked'

$blockerLine = Find-Line $functionHeader 'enum A64GuestCallFastEntryPayloadFlushPlanBlocker'
$inputsLine = Find-Line $functionHeader 'struct A64GuestCallFastEntryPayloadFlushPlanInputs'
$planLine = Find-Line $functionHeader 'struct A64GuestCallFastEntryPayloadFlushPlan'
$builderLine = Find-Line $functionSource 'BuildA64GuestCallFastEntryPayloadFlushPlan'
$directCallLine = Find-Line $emitter 'reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)'

Write-Output "audit=a64_fast_entry_payload_flush_plan"
Emit-Check "payload_flush_plan_blockers_cover_required_classes" ($planBlockerCount -eq 6) $functionHeader $blockerLine "Plan blockers must name missing source payload, writable slots, dirty-flush coverage, context writeback, stackpoint resume, and debug/exception flush."
Emit-Check "payload_flush_plan_inputs_cover_protocols" ($planInputCount -eq 8) $functionHeader $inputsLine "Plan inputs must represent payload source/writable masks, dirty payload, dirty-flush coverage, and mandatory boundary writeback support."
Emit-Check "payload_flush_plan_model_present" $planModelPresent $functionSource $builderLine "Source-only planner must conservatively block when payload or flush protocol inputs are missing."
Emit-Check "required_payload_mask_still_r3_r10_lr" $requiredPayloadMaskPresent $functionHeader (Find-Line $functionHeader 'kA64GuestCallFastEntryRequiredPayloadMask') "The payload contract must still cover r3-r10 plus lr."
Emit-Check "required_dirty_flush_mask_still_all_boundaries" $requiredDirtyFlushMaskPresent $functionHeader (Find-Line $functionHeader 'kA64GuestCallFastEntryRequiredDirtyFlushMask') "The dirty-flush contract must still cover context barrier, helper/host/debug/tail/return/exception/unresolved paths."
Emit-Check "guard_protocol_still_present" $guardProtocolStillPresent $functionHeader (Find-Line $functionHeader 'EvaluateA64GuestCallFastEntryGuard') "The direct-call guard source model must remain present."
Emit-Check "normal_entry_abi_still_return_x0_only" $normalEntryStillOnlyReturn $stackLayout (Find-Line $stackLayout 'x0 holds') "Normal guest entry remains the existing x0 return-address ABI."
Emit-Check "direct_calls_still_use_normal_entry" $directCallsStillNormal $emitter $directCallLine "Direct calls still branch to fn->machine_code() and pass only the guest return address."
Emit-Check "payload_flush_plan_unused_by_generated_code" $planUnusedByGeneratedCode $emitter $directCallLine "Generated code does not consult the source-only planner yet."
Emit-Check "generated_payload_behavior_absent" (-not $generatedPayloadBehaviorPresent) $emitter 0 "No generated path populates fast-entry payload state."
Emit-Check "generated_dirty_flush_behavior_absent" (-not $generatedDirtyFlushBehaviorPresent) $emitter 0 "No generated path flushes fast-entry payload state to PPCContext."
Emit-Check "fast_entry_callsite_behavior_absent" (-not $fastEntryCallsiteBehaviorPresent) $emitter $directCallLine "Callsites still do not guard or branch to alternate fast-entry code."
Emit-Check "guard_report_matches_current_blocked_state" $guardReportAgrees $functionHeader $planLine "The previous guard protocol note agrees generated behavior remains blocked."
Emit-Check "dirty_flush_report_matches_current_blocked_state" $dirtyFlushReportAgrees $functionHeader $planLine "The previous dirty-flush protocol note agrees payload and dirty-flush behavior are still not generated."

Write-Output ("payload_flush_plan blockers={0}/6 inputs={1}/8 behavior_changed=0" -f $planBlockerCount, $planInputCount)
Write-Output "required_before_behavior=source_value_mapping_for_r3_r10_lr;payload_slot_writes;dirty_payload_tracking;PPCContext_writeback_for_all_required_boundaries;stackpoint_resume_flush_or_invalidate;debug_exception_flush_or_normal_fallback;late_bound_target_fallback"

if ($planBlockerCount -eq 6 -and $planInputCount -eq 8 -and
    $planModelPresent -and $requiredPayloadMaskPresent -and
    $requiredDirtyFlushMaskPresent -and $guardProtocolStillPresent -and
    $normalEntryStillOnlyReturn -and $directCallsStillNormal -and
    $planUnusedByGeneratedCode -and
    -not $generatedPayloadBehaviorPresent -and
    -not $generatedDirtyFlushBehaviorPresent -and
    -not $fastEntryCallsiteBehaviorPresent) {
    Write-Output "decision=payload_flush_plan_source_ready_behavior_blocked"
    Write-Output "safe_next_patch=noop_codegen_protocol_helpers_or_lane_switch"
    Write-Output "do_not_patch=direct_callsites;machine_code_pointer;global_indirection;payload_materialization;dirty_flush_codegen;speed_ab"
} else {
    Write-Output "decision=needs_manual_review_before_any_payload_or_dirty_flush_behavior"
    Write-Output "safe_next_patch=fix_failed_payload_flush_plan_checks_first"
}
