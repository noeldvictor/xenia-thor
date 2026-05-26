[CmdletBinding()]
param(
    [string]$CodegenProtocolReportPath = "docs\research\20260526-110000-a64-fast-entry-codegen-protocol.md"
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
        [string]$Pattern,
        [int]$StartLine = 1
    )

    $startIndex = [Math]::Max(0, $StartLine - 1)
    for ($i = $startIndex; $i -lt $File.Lines.Count; $i++) {
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

$functionHeader = Read-RepoFile "src\xenia\cpu\backend\a64\a64_function.h"
$functionSource = Read-RepoFile "src\xenia\cpu\backend\a64\a64_function.cc"
$emitter = Read-RepoFile "src\xenia\cpu\backend\a64\a64_emitter.cc"
$backend = Read-RepoFile "src\xenia\cpu\backend\a64\a64_backend.cc"
$stackLayout = Read-RepoFile "src\xenia\cpu\backend\a64\a64_stack_layout.h"
$genericFunction = Read-RepoFile "src\xenia\cpu\function.cc"

$codegenReportText = Read-OptionalText $CodegenProtocolReportPath

$directCallGuardLine = Find-Line $emitter 'if \(fn->machine_code\(\)\)'
$directCallCodeLine = Find-Line $emitter 'mov\(x9, reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)\)' $directCallGuardLine
$directCallReturnLine = Find-Line $emitter 'StackLayout::GUEST_CALL_RET_ADDR' $directCallCodeLine
$directCallBranchLine = Find-Line $emitter 'blr\(x9\)' $directCallReturnLine
$lateBoundIndirectionLine = Find-Line $emitter 'Load host code address from indirection table'
$lateBoundResolveLine = Find-Line $emitter 'ResolveFunction' $lateBoundIndirectionLine
$stackpointLine = Find-Line $emitter 'void A64Emitter::PushStackpoint'
$stackSyncLine = Find-Line $emitter 'void A64Emitter::EnsureSynchronizedGuestAndHostStack'
$debugLine = Find-Line $emitter 'void A64Emitter::DebugBreak'
$exceptionLine = Find-Line $backend 'bool A64Backend::ExceptionCallback'
$lookupLine = Find-Line $genericFunction 'LookupMachineCodeOffset'
$protocolLine = Find-Line $functionSource 'EvaluateA64GuestCallFastEntryCodegenProtocol'
$storageLine = Find-Line $functionHeader 'guest_call_fast_entry_code_'
$contractLine = Find-Line $functionHeader 'A64GuestCallFastEntryContract'

$directCallAttachPointPresent =
    $directCallGuardLine -gt 0 -and
    $directCallCodeLine -gt 0 -and
    $directCallReturnLine -gt 0 -and
    $directCallBranchLine -gt 0 -and
    $directCallCodeLine -lt $directCallReturnLine -and
    $directCallReturnLine -lt $directCallBranchLine

$lateBoundFallbackAttachPointPresent =
    $lateBoundIndirectionLine -gt 0 -and
    $lateBoundResolveLine -gt 0 -and
    (Test-Pattern $emitter 'ResolveFunction') -and
    (Test-Pattern $backend 'ResolveFunctionThunk') -and
    (Test-Pattern $backend 'processor\(\)->ResolveFunction') -and
    (Test-Pattern $backend 'guest_fn->machine_code\(\)')

$stackpointResumeSurfacePresent =
    $stackpointLine -gt 0 -and
    $stackSyncLine -gt 0 -and
    (Test-Pattern $emitter 'PushStackpoint\(\)') -and
    (Test-Pattern $emitter 'EnsureSynchronizedGuestAndHostStack') -and
    (Test-Pattern $stackLayout 'Stackpoint depth after PushStackpoint')

$debugExceptionVisibilitySurfacePresent =
    $debugLine -gt 0 -and
    $exceptionLine -gt 0 -and
    $lookupLine -gt 0 -and
    (Test-Pattern $emitter 'TrapDebugBreak') -and
    (Test-Pattern $backend 'ExceptionCallbackThunk') -and
    (Test-Pattern $genericFunction 'LookupMachineCodeOffset')

$sourceProtocolReady =
    (Test-Pattern $functionHeader 'guest_call_fast_entry_code\(\)') -and
    (Test-Pattern $functionHeader 'guest_call_fast_entry_contract\(\)') -and
    (Test-Pattern $functionHeader 'A64GuestCallFastEntryCodegenProtocolInputs') -and
    (Test-Pattern $functionHeader 'A64GuestCallFastEntryCodegenProtocolDecision') -and
    (Test-Pattern $functionSource 'EvaluateA64GuestCallFastEntryCodegenProtocol') -and
    (Test-Pattern $functionSource 'EvaluateA64GuestCallFastEntryGuard\(contract, inputs\.guard_inputs') -and
    (Test-Pattern $functionSource 'BuildA64GuestCallFastEntryPayloadFlushPlan\(\s*contract, inputs\.payload_flush_inputs')

$normalEntryStillNormal =
    (Test-Pattern $stackLayout 'x0 holds the guest PPC return') -and
    (Test-Pattern $backend 'Pass guest return address in x0') -and
    (Test-Pattern $emitter 'reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)') -and
    (Test-Pattern $emitter 'StackLayout::GUEST_CALL_RET_ADDR') -and
    (Test-Pattern $emitter 'blr\(x9\)')

$globalIndirectionStillNormal =
    (Test-Pattern $emitter 'Load host code address from indirection table') -and
    (Test-Pattern $backend 'ResolveFunctionThunk') -and
    (Test-Pattern $backend 'return reinterpret_cast<uint64_t>\(code\);')

$protocolUnusedByGeneratedCode =
    -not (Test-Pattern $emitter 'EvaluateA64GuestCallFastEntryCodegenProtocol|A64GuestCallFastEntryCodegenProtocolInputs|ready_for_behavior_codegen') -and
    -not (Test-Pattern $backend 'EvaluateA64GuestCallFastEntryCodegenProtocol|A64GuestCallFastEntryCodegenProtocolInputs|ready_for_behavior_codegen')

$guardUnusedByGeneratedCode =
    -not (Test-Pattern $emitter 'EvaluateA64GuestCallFastEntryGuard|A64GuestCallFastEntryGuardInputs') -and
    -not (Test-Pattern $backend 'EvaluateA64GuestCallFastEntryGuard|A64GuestCallFastEntryGuardInputs')

$payloadFlushPlanUnusedByGeneratedCode =
    -not (Test-Pattern $emitter 'BuildA64GuestCallFastEntryPayloadFlushPlan|A64GuestCallFastEntryPayloadFlushPlanInputs') -and
    -not (Test-Pattern $backend 'BuildA64GuestCallFastEntryPayloadFlushPlan|A64GuestCallFastEntryPayloadFlushPlanInputs')

$directCallsiteFastEntryBehaviorAbsent =
    -not (Test-Pattern $emitter 'guest_call_fast_entry_code\(\)|guest_call_fast_entry_contract\(\)|alternate_codegen=1|ready_for_behavior_codegen')

$payloadPopulationBehaviorAbsent =
    -not (Test-Pattern $emitter 'PopulateGuestCallFastEntryPayload|EmitGuestCallFastEntryPayload|payload_population') -and
    -not (Test-Pattern $backend 'PopulateGuestCallFastEntryPayload|EmitGuestCallFastEntryPayload|payload_population')

$dirtyFlushBehaviorAbsent =
    -not (Test-Pattern $emitter 'FlushGuestCallFastEntryPayload|EmitGuestCallFastEntryDirtyFlush|GuestCallFastEntryDirtyFlush|payload_dirty_flush') -and
    -not (Test-Pattern $backend 'FlushGuestCallFastEntryPayload|EmitGuestCallFastEntryDirtyFlush|GuestCallFastEntryDirtyFlush|payload_dirty_flush')

$stubCodegenAbsent =
    -not (Test-Pattern $emitter 'EmitGuestCallFastEntry|fast_entry_stub_codegen|late_bound_entry|alternate_codegen=1') -and
    -not (Test-Pattern $backend 'EmitGuestCallFastEntry|fast_entry_stub_codegen|late_bound_entry|alternate_codegen=1')

$codegenReportAgrees =
    $codegenReportText -match 'codegen_protocol_source_ready_behavior_blocked' -and
    $codegenReportText -match 'direct calls still branch to `fn->machine_code\(\)`'

Write-Output "audit=a64_fast_entry_emitter_planning"
Emit-Check "source_protocol_ready" $sourceProtocolReady $functionSource $protocolLine "The source-only fast-entry protocol gate, contract, and separate storage model exist."
Emit-Check "direct_call_attach_point_present" $directCallAttachPointPresent $emitter $directCallCodeLine "Future direct-call guard and branch selection would attach at the current fn->machine_code() callsite."
Emit-Check "late_bound_fallback_attach_point_present" $lateBoundFallbackAttachPointPresent $emitter $lateBoundIndirectionLine "Future late-bound fallback must preserve ResolveFunction and indirection-table normal-entry paths."
Emit-Check "stackpoint_resume_surface_present" $stackpointResumeSurfacePresent $emitter $stackpointLine "Future payload state must be flushed or invalidated across stackpoint resume/longjmp handling."
Emit-Check "debug_exception_visibility_surface_present" $debugExceptionVisibilitySurfacePresent $emitter $debugLine "Future alternate entry must preserve debug break, exception callback, and source-map visibility."
Emit-Check "normal_entry_still_unchanged" $normalEntryStillNormal $emitter $directCallCodeLine "Normal machine_code() entry still receives only the guest return address and remains the only emitted target."
Emit-Check "global_indirection_still_unchanged" $globalIndirectionStillNormal $emitter $lateBoundIndirectionLine "Global indirection and ResolveFunction still return/use normal machine_code()."
Emit-Check "codegen_protocol_unused_by_generated_code" $protocolUnusedByGeneratedCode $emitter $directCallCodeLine "Emitter/backend do not consult the source-only protocol gate yet."
Emit-Check "guard_model_unused_by_generated_code" $guardUnusedByGeneratedCode $emitter $directCallCodeLine "Emitter/backend do not consult the guard model yet."
Emit-Check "payload_flush_plan_unused_by_generated_code" $payloadFlushPlanUnusedByGeneratedCode $emitter $directCallCodeLine "Emitter/backend do not consult the payload/dirty-flush planner yet."
Emit-Check "direct_callsite_fast_entry_behavior_absent" $directCallsiteFastEntryBehaviorAbsent $emitter $directCallCodeLine "Direct callsites still do not test or branch to fast-entry code."
Emit-Check "payload_population_behavior_absent" $payloadPopulationBehaviorAbsent $emitter 0 "No generated path populates r3-r10/lr payload slots."
Emit-Check "dirty_flush_behavior_absent" $dirtyFlushBehaviorAbsent $emitter 0 "No generated path flushes payload state back to PPCContext."
Emit-Check "stub_codegen_absent" $stubCodegenAbsent $emitter 0 "No generated fast-entry stub or alternate callsite code exists."
Emit-Check "codegen_protocol_report_matches_current_blocked_state" $codegenReportAgrees $functionSource $protocolLine "The previous codegen-protocol note agrees behavior is still blocked."

Write-Output ("emitter_planning_points direct_call_guard_site={0} return_addr_site={1} direct_branch_site={2} late_bound_site={3} resolve_call_site={4} stackpoint_site={5} stack_sync_site={6} debug_site={7} exception_site={8} lookup_site={9} storage_site={10} contract_site={11}" -f `
    (Format-Source $emitter $directCallCodeLine),
    (Format-Source $emitter $directCallReturnLine),
    (Format-Source $emitter $directCallBranchLine),
    (Format-Source $emitter $lateBoundIndirectionLine),
    (Format-Source $emitter $lateBoundResolveLine),
    (Format-Source $emitter $stackpointLine),
    (Format-Source $emitter $stackSyncLine),
    (Format-Source $emitter $debugLine),
    (Format-Source $backend $exceptionLine),
    (Format-Source $genericFunction $lookupLine),
    (Format-Source $functionHeader $storageLine),
    (Format-Source $functionHeader $contractLine))
Write-Output "required_before_behavior=guard_emission_at_direct_call;payload_population_codegen;r3_r10_lr_dirty_flush_codegen;late_bound_normal_fallback;stackpoint_resume_flush_or_invalidate;debug_exception_visibility;route_clean_stub_proof;quiet_ab_after_route_safety"
Write-Output "behavior_changed=0 alternate_codegen=0 normal_entry=unchanged global_indirection=unchanged"

if ($sourceProtocolReady -and
    $directCallAttachPointPresent -and
    $lateBoundFallbackAttachPointPresent -and
    $stackpointResumeSurfacePresent -and
    $debugExceptionVisibilitySurfacePresent -and
    $normalEntryStillNormal -and
    $globalIndirectionStillNormal -and
    $protocolUnusedByGeneratedCode -and
    $guardUnusedByGeneratedCode -and
    $payloadFlushPlanUnusedByGeneratedCode -and
    $directCallsiteFastEntryBehaviorAbsent -and
    $payloadPopulationBehaviorAbsent -and
    $dirtyFlushBehaviorAbsent -and
    $stubCodegenAbsent) {
    Write-Output "decision=emitter_planning_source_ready_behavior_blocked"
    Write-Output "safe_next_patch=lane_switch_or_compile_time_route_ranking_for_non_fast_entry_structural_lane"
    Write-Output "do_not_patch=direct_callsites;machine_code_pointer;global_indirection;payload_materialization;dirty_flush_codegen;fast_entry_stub_codegen;speed_ab"
} else {
    Write-Output "decision=needs_manual_review_before_any_fast_entry_codegen_behavior"
    Write-Output "safe_next_patch=fix_failed_emitter_planning_checks_first"
}
