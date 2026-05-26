[CmdletBinding()]
param(
    [string]$FastEntryTargetRowReportPath = "docs\research\20260526-053600-a64-guest-call-fast-entry-target-row-capture.md",
    [string]$FastEntryDataModelReportPath = "docs\research\20260526-093500-a64-fast-entry-data-model-rerank.md"
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
$assembler = Read-RepoFile "src\xenia\cpu\backend\a64\a64_assembler.cc"
$stackLayout = Read-RepoFile "src\xenia\cpu\backend\a64\a64_stack_layout.h"
$codeCache = Read-RepoFile "src\xenia\cpu\backend\code_cache_base.h"

$targetRowReportText = Read-OptionalText $FastEntryTargetRowReportPath
$dataModelReportText = Read-OptionalText $FastEntryDataModelReportPath

$payloadPatterns = @(
    'kA64GuestCallFastEntryPayloadGpr3',
    'kA64GuestCallFastEntryPayloadGpr4',
    'kA64GuestCallFastEntryPayloadGpr5',
    'kA64GuestCallFastEntryPayloadGpr6',
    'kA64GuestCallFastEntryPayloadGpr7',
    'kA64GuestCallFastEntryPayloadGpr8',
    'kA64GuestCallFastEntryPayloadGpr9',
    'kA64GuestCallFastEntryPayloadGpr10',
    'kA64GuestCallFastEntryPayloadLr'
)
$dirtyFlushPatterns = @(
    'kA64GuestCallFastEntryFlushContextBarrier',
    'kA64GuestCallFastEntryFlushHelperCall',
    'kA64GuestCallFastEntryFlushHostCall',
    'kA64GuestCallFastEntryFlushDebugTrap',
    'kA64GuestCallFastEntryFlushTailCall',
    'kA64GuestCallFastEntryFlushReturn',
    'kA64GuestCallFastEntryFlushException',
    'kA64GuestCallFastEntryFlushUnresolvedTarget'
)

$payloadBitCount = Count-PresentPatterns $functionHeader $payloadPatterns
$dirtyFlushBitCount = Count-PresentPatterns $functionHeader $dirtyFlushPatterns

$fastEntryStorage =
    (Test-Pattern $functionHeader 'guest_call_fast_entry_code_') -and
    (Test-Pattern $functionHeader 'guest_call_fast_entry_code_length_') -and
    (Test-Pattern $functionSource 'SetupGuestCallFastEntry') -and
    (Test-Pattern $functionSource 'guest_call_fast_entry_code_\.store')

$contractStorage =
    (Test-Pattern $functionHeader 'A64GuestCallFastEntryContract') -and
    (Test-Pattern $functionHeader 'guest_call_fast_entry_payload_gpr_mask_') -and
    (Test-Pattern $functionHeader 'guest_call_fast_entry_dirty_flush_mask_') -and
    (Test-Pattern $functionHeader 'guest_call_fast_entry_flags_') -and
    (Test-Pattern $functionSource 'guest_call_fast_entry_contract')

$stubSkeletonProtocol =
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryFlagStubSkeleton') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryFlagBehaviorEnabled') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryRequiredPayloadMask') -and
    (Test-Pattern $functionHeader 'kA64GuestCallFastEntryRequiredDirtyFlushMask') -and
    (Test-Pattern $functionHeader 'MakeA64GuestCallFastEntryStubSkeletonContract') -and
    (Test-Pattern $functionHeader 'A64GuestCallFastEntryContractCoversStubSkeleton') -and
    (Test-Pattern $functionSource 'SetupGuestCallFastEntryStubSkeleton') -and
    (Test-Pattern $functionSource 'A64GuestCallFastEntryContractEnablesBehavior')

$stubSkeletonDefaultOffCvar =
    (Test-Pattern $backend 'DEFINE_bool\(\s*arm64_guest_call_fast_entry_stub_skeleton,\s*false') -and
    (Test-Pattern $backend 'alternate_codegen=0 direct_calls=unchanged') -and
    (Test-Pattern $backend 'global_indirection=unchanged behavior_changed=0')

$normalEntryStillOnlyReturnX0 =
    (Test-Pattern $stackLayout 'x0 holds the guest PPC return') -and
    (Test-Pattern $backend 'Pass guest return address in x0') -and
    (Test-Pattern $backend 'mov\(x0, x2\)')

$directCallsStillUseNormalEntry =
    (Test-Pattern $emitter 'if \(fn->machine_code\(\)\)') -and
    (Test-Pattern $emitter 'reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)') -and
    (Test-Pattern $emitter 'StackLayout::GUEST_CALL_RET_ADDR') -and
    (Test-Pattern $emitter 'blr\(x9\)')

$globalIndirectionStillNormal =
    (Test-Pattern $assembler 'AddIndirection\(function->address\(\)') -and
    (Test-Pattern $codeCache 'void AddIndirection\(uint32_t guest_address, uint32_t host_address\)') -and
    (Test-Pattern $emitter 'ResolveFunction')

$payloadPopulationCodePresent =
    (Test-Pattern $emitter 'PopulateGuestCallFastEntryPayload|EmitGuestCallFastEntryPayload|guest_call_fast_entry_payload.*store|payload_population') -or
    (Test-Pattern $backend 'PopulateGuestCallFastEntryPayload|EmitGuestCallFastEntryPayload|payload_population')

$dirtyFlushCodePresent =
    (Test-Pattern $emitter 'FlushGuestCallFastEntryPayload|EmitGuestCallFastEntryDirtyFlush|GuestCallFastEntryDirtyFlush|payload_dirty_flush') -or
    (Test-Pattern $backend 'FlushGuestCallFastEntryPayload|EmitGuestCallFastEntryDirtyFlush|GuestCallFastEntryDirtyFlush|payload_dirty_flush')

$stubCodegenPresent =
    (Test-Pattern $emitter 'EmitGuestCallFastEntry|fast_entry_stub|late_bound_entry|alternate_codegen=1') -or
    (Test-Pattern $assembler 'EmitGuestCallFastEntry|fast_entry_stub|late_bound_entry')

$directCallsiteGuardPresent =
    (Test-Pattern $emitter 'guest_call_fast_entry_code\(\)') -or
    (Test-Pattern $emitter 'guest_call_fast_entry_contract\(\)')

$stackpointResumeProtocolPresent =
    (Test-Pattern $emitter 'GuestCallFastEntryStackpoint|FastEntryStackpoint|fast_entry_stackpoint|stackpoint_fast_entry') -or
    (Test-Pattern $backend 'GuestCallFastEntryStackpoint|FastEntryStackpoint|fast_entry_stackpoint|stackpoint_fast_entry')

$payloadLine = Find-Line $functionHeader 'enum A64GuestCallFastEntryPayloadMask'
$dirtyLine = Find-Line $functionHeader 'enum A64GuestCallFastEntryDirtyFlushMask'
$storageLine = Find-Line $functionHeader 'guest_call_fast_entry_code_'
$contractLine = Find-Line $functionHeader 'A64GuestCallFastEntryContract'
$stubSkeletonLine = Find-Line $functionHeader 'MakeA64GuestCallFastEntryStubSkeletonContract'
$stubSkeletonCvarLine = Find-Line $backend 'arm64_guest_call_fast_entry_stub_skeleton'
$directCallLine = Find-Line $emitter 'reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)'
$indirectionLine = Find-Line $emitter 'Load host code address from indirection table'
$stackpointLine = Find-Line $emitter 'PushStackpoint'

$runtimeUnresolvedTargets = Find-ReportInt $targetRowReportText "unresolved_direct_targets"
$runtimeNormalFallback = Find-ReportInt $targetRowReportText "normal_entry_fallback"
$runtimeStackpointSensitive = Find-ReportInt $targetRowReportText "stackpoint_sensitive"
$runtimeDirtyFlushPoints = Find-ReportInt $targetRowReportText "dirty_flush_points"
$runtimeFlushContextBarrier = Find-ReportInt $targetRowReportText "flush_context_barrier"
$runtimeParentPreCallFlushPoints = Find-ReportInt $targetRowReportText "parent_pre_call_flush_points"
$runtimeKnownFirstUse = Find-ReportInt $targetRowReportText "callee_first_use_known"

$dataModelReportAgrees =
    $dataModelReportText -match 'fast_entry_data_model_present=true' -and
    $dataModelReportText -match 'behavior_fast_entry_present=false'

Write-Output "audit=a64_fast_entry_dirty_flush_protocol"
Emit-Check "payload_mask_covers_r3_r10_lr" ($payloadBitCount -eq 9) $functionHeader $payloadLine "Fast-entry payload metadata must explicitly represent r3-r10 plus lr."
Emit-Check "dirty_flush_mask_covers_required_boundaries" ($dirtyFlushBitCount -eq 8) $functionHeader $dirtyLine "Dirty payloads need named flush causes for barrier, helper, host call, debug, tail, return, exception, and unresolved target."
Emit-Check "fast_entry_storage_present" $fastEntryStorage $functionHeader $storageLine "A64Function has separate fast-entry code storage without changing normal machine_code()."
Emit-Check "payload_contract_storage_present" $contractStorage $functionHeader $contractLine "A64Function stores payload mask, dirty-flush mask, and flags for a future guarded entry."
Emit-Check "stub_skeleton_protocol_present" $stubSkeletonProtocol $functionHeader $stubSkeletonLine "Source has a no-op contract helper that covers r3-r10/lr payloads and dirty-flush boundaries while behavior remains disabled."
Emit-Check "stub_skeleton_default_off_cvar_present" $stubSkeletonDefaultOffCvar $backend $stubSkeletonCvarLine "The launchable skeleton cvar is default-off and logs unchanged direct calls, indirection, and behavior."
Emit-Check "normal_entry_abi_still_return_x0_only" $normalEntryStillOnlyReturnX0 $stackLayout (Find-Line $stackLayout 'Convention: at guest function entry, x0 holds') "Normal entry still receives only the guest return address in x0."
Emit-Check "direct_calls_still_use_normal_entry" $directCallsStillUseNormalEntry $emitter $directCallLine "Direct calls still branch to fn->machine_code() and do not consume payload metadata."
Emit-Check "late_bound_paths_still_use_normal_entry" $globalIndirectionStillNormal $emitter $indirectionLine "Indirection and resolve paths still resolve to the normal entry."
Emit-Check "payload_population_codegen_absent" (-not $payloadPopulationCodePresent) $emitter 0 "No generated path populates r3-r10/lr payload state yet."
Emit-Check "dirty_flush_codegen_absent" (-not $dirtyFlushCodePresent) $emitter 0 "No generated path flushes dirty payload state back to PPCContext yet."
Emit-Check "fast_entry_stub_codegen_absent" (-not $stubCodegenPresent) $emitter 0 "No generated fast-entry stub or alternate callsite behavior exists yet."
Emit-Check "direct_callsite_guard_absent" (-not $directCallsiteGuardPresent) $emitter $directCallLine "Callsites do not test guest_call_fast_entry_code() or contract metadata yet."
Emit-Check "stackpoint_resume_protocol_absent" (-not $stackpointResumeProtocolPresent) $emitter $stackpointLine "No fast-entry-specific stackpoint resume protocol is defined yet."
Emit-Check "data_model_report_matches_source" $dataModelReportAgrees $functionHeader $contractLine "The latest rerank report agrees that data model exists and behavior is absent."

Write-Output ("payload_protocol payload_slots={0}/9 slots=r3,r4,r5,r6,r7,r8,r9,r10,lr" -f $payloadBitCount)
Write-Output ("dirty_flush_protocol flush_causes={0}/8 causes=context_barrier,helper_call,host_call,debug_trap,tail_call,return,exception,unresolved_target" -f $dirtyFlushBitCount)
Write-Output ("previous_runtime_report={0}" -f $FastEntryTargetRowReportPath)
Write-Output ("previous_runtime_blockers unresolved_direct_targets={0} normal_entry_fallback={1} stackpoint_sensitive={2} dirty_flush_points={3} flush_context_barrier={4} parent_pre_call_flush_points={5} callee_first_use_known={6}" -f `
    $runtimeUnresolvedTargets,
    $runtimeNormalFallback,
    $runtimeStackpointSensitive,
    $runtimeDirtyFlushPoints,
    $runtimeFlushContextBarrier,
    $runtimeParentPreCallFlushPoints,
    $runtimeKnownFirstUse)
Write-Output "required_protocol_before_behavior=payload_population_from_parent_context_or_live_values;dirty_flush_to_PPCContext_before_each_named_boundary;normal_entry_fallback_for_unresolved_indirect_extern_tail_and_debug_paths;stackpoint_resume_payload_invalidation_or_flush;late_bound_target_guard;no_global_indirection_rewrite"

if ($payloadBitCount -eq 9 -and $dirtyFlushBitCount -eq 8 -and
    $fastEntryStorage -and $contractStorage -and
    $stubSkeletonProtocol -and $stubSkeletonDefaultOffCvar -and
    $normalEntryStillOnlyReturnX0 -and $directCallsStillUseNormalEntry -and
    $globalIndirectionStillNormal -and
    -not $payloadPopulationCodePresent -and
    -not $dirtyFlushCodePresent -and
    -not $stubCodegenPresent) {
    Write-Output "decision=stub_skeleton_source_ready_but_behavior_blocked"
    Write-Output "behavior_status=unchanged"
    Write-Output "safe_next_patch=guard_payload_flush_and_codegen_protocol_gates_present_continue_with_lane_switch_or_behavior_neutral_emitter_planning_audit"
    Write-Output "do_not_patch=direct_callsites;machine_code_pointer;global_indirection;payload_materialization;quiet_speed_ab"
} else {
    Write-Output "decision=needs_manual_review_before_any_stub_or_behavior_patch"
    Write-Output "safe_next_patch=fix_failed_protocol_checks_first"
}
