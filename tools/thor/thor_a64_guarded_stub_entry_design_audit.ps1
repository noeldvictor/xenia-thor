[CmdletBinding()]
param(
    [string]$FastEntryTargetRowReportPath = "docs\research\20260526-053600-a64-guest-call-fast-entry-target-row-capture.md"
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

$functionHeader = Read-RepoFile "src\xenia\cpu\backend\a64\a64_function.h"
$functionSource = Read-RepoFile "src\xenia\cpu\backend\a64\a64_function.cc"
$assembler = Read-RepoFile "src\xenia\cpu\backend\a64\a64_assembler.cc"
$emitter = Read-RepoFile "src\xenia\cpu\backend\a64\a64_emitter.cc"
$backend = Read-RepoFile "src\xenia\cpu\backend\a64\a64_backend.cc"
$backendHeader = Read-RepoFile "src\xenia\cpu\backend\a64\a64_backend.h"
$stackLayout = Read-RepoFile "src\xenia\cpu\backend\a64\a64_stack_layout.h"
$codeCache = Read-RepoFile "src\xenia\cpu\backend\code_cache_base.h"

$reportPath = Join-Path $repoRoot $FastEntryTargetRowReportPath
$reportText = ""
if (Test-Path -LiteralPath $reportPath) {
    $reportText = Get-Content -LiteralPath $reportPath -Raw
}

$machineCodeLine = Find-Line $functionHeader 'std::atomic<uint8_t\*> machine_code_'
$setupLine = Find-Line $functionSource 'void A64Function::Setup'
$assemblerIndirectionLine = Find-Line $assembler 'AddIndirection\(function->address\(\)'
$codeCacheIndirectionLine = Find-Line $codeCache 'void AddIndirection'
$normalEntryDocLine = Find-Line $stackLayout 'Convention: at guest function entry, x0 holds'
$hostEntryLine = Find-Line $backend 'Pass guest return address in x0'
$directCallLine = Find-Line $emitter 'mov\(x9, reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)\)'
$normalCallReturnLine = Find-Line $emitter 'GUEST_CALL_RET_ADDR'
$indirectionPathLine = Find-Line $emitter 'Load host code address from indirection table'
$resolveThunkLine = Find-Line $backend 'ResolveFunctionThunk A64HelperEmitter::EmitResolveFunctionThunk'
$pushStackpointLine = Find-Line $emitter 'void A64Emitter::PushStackpoint'
$syncLine = Find-Line $emitter 'void A64Emitter::EnsureSynchronizedGuestAndHostStack'
$guestToHostLine = Find-Line $backend 'GuestToHostThunk A64HelperEmitter::EmitGuestToHostThunk'
$exceptionLine = Find-Line $backend 'bool A64Backend::ExceptionCallback'
$backendContextLine = Find-Line $backendHeader 'struct A64BackendContext'

$normalEntrySingleton =
    (Test-Pattern $functionHeader 'std::atomic<uint8_t\*> machine_code_') -and
    (Test-Pattern $functionSource 'machine_code_\.store') -and
    -not (Test-Pattern $functionHeader 'fast_entry|alternate_entry|late_bound_entry|stub_entry')

$globalIndirectionSingleton =
    (Test-Pattern $assembler 'AddIndirection\(function->address\(\)') -and
    (Test-Pattern $codeCache 'void AddIndirection\(uint32_t guest_address, uint32_t host_address\)')

$normalEntryAbiOnlyReturnX0 =
    (Test-Pattern $stackLayout 'x0 holds the guest PPC return') -and
    (Test-Pattern $backend 'Pass guest return address in x0') -and
    (Test-Pattern $backend 'mov\(x0, x2\)')

$directCallsUseNormalMachineCode =
    (Test-Pattern $emitter 'if \(fn->machine_code\(\)\)') -and
    (Test-Pattern $emitter 'mov\(x9, reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)\)') -and
    (Test-Pattern $emitter 'StackLayout::GUEST_CALL_RET_ADDR') -and
    (Test-Pattern $emitter 'blr\(x9\)')

$lateBoundUnresolvedStaysNormal =
    (Test-Pattern $emitter 'code_cache_->has_indirection_table\(\)') -and
    (Test-Pattern $emitter 'ResolveFunction') -and
    (Test-Pattern $backend 'ResolveFunctionThunk A64HelperEmitter::EmitResolveFunctionThunk') -and
    (Test-Pattern $backend 'br\(x9\)')

$stackpointReentryContract =
    (Test-Pattern $emitter 'PushStackpoint\(\)') -and
    (Test-Pattern $emitter 'synchronize_stack_on_next_instruction_ = true') -and
    (Test-Pattern $emitter 'EnsureSynchronizedGuestAndHostStack') -and
    (Test-Pattern $backend 'EmitGuestAndHostSynchronizeStackHelper')

$hostDebugExceptionBoundaries =
    (Test-Pattern $backend 'EmitGuestToHostThunk') -and
    (Test-Pattern $backend 'Host callbacks may change FPCR') -and
    (Test-Pattern $backend 'ExceptionCallback') -and
    (Test-Pattern $emitter 'DebugBreak')

$alternateEntryStoragePresent =
    (Test-Pattern $functionHeader 'fast_entry|alternate_entry|late_bound_entry|stub_entry') -or
    (Test-Pattern $functionSource 'fast_entry|alternate_entry|late_bound_entry|stub_entry')

$payloadAbiStoragePresent =
    (Test-Pattern $backendHeader 'fast_entry_payload|guest_call_payload|arg_payload|dirty_payload') -or
    (Test-Pattern $emitter 'EmitGuestCallFastEntry|fast_entry_stub|late_bound_entry')

$behaviorFastEntryPresent =
    (Test-Pattern $emitter 'EmitGuestCallFastEntry|alternate_codegen=1|fast_entry_stub|late_bound_entry') -or
    (Test-Pattern $assembler 'fast_entry|alternate_entry|late_bound_entry|stub_entry')

$runtimeUnresolvedTargets = Find-ReportInt $reportText "unresolved_direct_targets"
$runtimeNormalFallback = Find-ReportInt $reportText "normal_entry_fallback"
$runtimeStackpointSensitive = Find-ReportInt $reportText "stackpoint_sensitive"
$runtimeDirtyFlushPoints = Find-ReportInt $reportText "dirty_flush_points"
$runtimeKnownFirstUse = Find-ReportInt $reportText "callee_first_use_known"

$runtimeBlockersPresent =
    ($runtimeUnresolvedTargets -gt 0) -or
    ($runtimeNormalFallback -gt 0) -or
    ($runtimeStackpointSensitive -gt 0) -or
    ($runtimeDirtyFlushPoints -gt 0)

Write-Output "audit=a64_guarded_stub_entry_design"
Emit-Check "normal_entry_singleton" $normalEntrySingleton $functionHeader $machineCodeLine "A64Function still exposes one normal machine_code pointer and no alternate entry storage."
Emit-Check "global_indirection_singleton" $globalIndirectionSingleton $assembler $assemblerIndirectionLine "The assembler publishes one normal-entry target through the code-cache indirection table."
Emit-Check "normal_entry_abi_only_return_x0" $normalEntryAbiOnlyReturnX0 $stackLayout $normalEntryDocLine "Normal guest entry receives only the PPC return address in x0."
Emit-Check "direct_calls_use_normal_machine_code" $directCallsUseNormalMachineCode $emitter $directCallLine "Compiled direct calls load fn->machine_code(), load GUEST_CALL_RET_ADDR into x0, and branch to normal entry."
Emit-Check "late_bound_unresolved_stays_normal" $lateBoundUnresolvedStaysNormal $emitter $indirectionPathLine "Indirection and ResolveFunction paths jump to the same normal entry, so unresolved targets cannot assume a payload ABI."
Emit-Check "stackpoint_reentry_contract" $stackpointReentryContract $emitter $pushStackpointLine "Guest frames push stackpoints and synchronize after calls, constraining any fast-entry frame shape."
Emit-Check "host_debug_exception_boundaries" $hostDebugExceptionBoundaries $backend $guestToHostLine "Host callbacks, debug breaks, exceptions, and FPCR restore points require normal visibility unless an explicit flush model exists."
Emit-Check "alternate_entry_storage_present" $alternateEntryStoragePresent $functionHeader $machineCodeLine "A separate guarded entry pointer/offset is required before behavior work."
Emit-Check "payload_abi_storage_present" $payloadAbiStoragePresent $backendHeader $backendContextLine "No generic r3-r10/lr payload ABI or dirty-state storage exists for guarded stubs."
Emit-Check "behavior_fast_entry_present" $behaviorFastEntryPresent $emitter $directCallLine "Current fast-entry support is audit-only; there is no alternate codegen path."

Write-Output ("previous_runtime_report={0}" -f $FastEntryTargetRowReportPath)
Write-Output ("previous_runtime_blockers unresolved_direct_targets={0} normal_entry_fallback={1} stackpoint_sensitive={2} dirty_flush_points={3} callee_first_use_known={4}" -f $runtimeUnresolvedTargets, $runtimeNormalFallback, $runtimeStackpointSensitive, $runtimeDirtyFlushPoints, $runtimeKnownFirstUse)

if ($normalEntrySingleton -and $globalIndirectionSingleton -and
    $directCallsUseNormalMachineCode -and $lateBoundUnresolvedStaysNormal -and
    $stackpointReentryContract -and -not $alternateEntryStoragePresent -and
    -not $payloadAbiStoragePresent -and -not $behaviorFastEntryPresent) {
    Write-Output "decision=behavior_blocked_until_contract_exists"
    Write-Output "blocked_behavior_patch=do_not_replace_A64Function_machine_code;do_not_rewrite_global_indirection;do_not_materialize_r3_r10_lr_payload_without_dirty_flush_abi"
    Write-Output "missing_contracts=separate_fast_entry_pointer_or_offset;direct_callsite_guard_and_fallback;payload_abi_for_r3_r10_lr;dirty_flush_before_context_barrier_helper_host_call_debug_trap_tail_return_exception;late_bound_target_resolution_model;stackpoint_resume_protocol"
    if ($runtimeBlockersPresent) {
        Write-Output "runtime_blockers=present"
    } else {
        Write-Output "runtime_blockers=not_proven_from_report"
    }
    Write-Output "recommended_next_slice=do_not_continue_fast_entry_by_default;move_to_guest_state_cache_or_other_structural_a64_lane_unless_next_slice_adds_the_missing_alternate_entry_data_model_only"
} else {
    Write-Output "decision=needs_manual_review"
    Write-Output "recommended_next_slice=inspect_failed_checks_before_any_runtime_or_behavior_patch"
}
