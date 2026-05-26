[CmdletBinding()]
param()

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

$backend = Read-RepoFile "src\xenia\cpu\backend\a64\a64_backend.cc"
$emitter = Read-RepoFile "src\xenia\cpu\backend\a64\a64_emitter.cc"
$function = Read-RepoFile "src\xenia\cpu\backend\a64\a64_function.cc"
$functionHeader = Read-RepoFile "src\xenia\cpu\backend\a64\a64_function.h"
$stackLayout = Read-RepoFile "src\xenia\cpu\backend\a64\a64_stack_layout.h"
$codeCache = Read-RepoFile "src\xenia\cpu\backend\code_cache_base.h"
$genericFunction = Read-RepoFile "src\xenia\cpu\function.cc"

$hostToGuestReturnLine = Find-Line $backend 'Pass guest return address in x0'
$directCallMachineCodeLine = Find-Line $emitter 'mov\(x9, reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)\)'
$directCallReturnLine = Find-Line $emitter 'StackLayout::GUEST_CALL_RET_ADDR'
$tailCallReturnLine = Find-Line $emitter 'Tail call: pass our return address to the callee'
$indirectionLine = Find-Line $codeCache 'Fix up indirection table'
$machineCodeMemberLine = Find-Line $functionHeader 'std::atomic<uint8_t\*> machine_code_'
$callImplLine = Find-Line $function 'bool A64Function::CallImpl'
$pushStackpointLine = Find-Line $emitter 'void A64Emitter::PushStackpoint'
$syncLine = Find-Line $emitter 'EnsureSynchronizedGuestAndHostStack'
$exceptionLine = Find-Line $backend 'bool A64Backend::ExceptionCallback'
$debugBreakLine = Find-Line $emitter 'void A64Emitter::DebugBreak'
$guestToHostLine = Find-Line $backend 'GuestToHostThunk A64HelperEmitter::EmitGuestToHostThunk'
$registerSetLine = Find-Line $backend 'GPR set: x22-x28'
$normalEntryDocLine = Find-Line $stackLayout 'Convention: at guest function entry, x0 holds'

$normalEntrySingleton =
    (Test-Pattern $functionHeader 'std::atomic<uint8_t\*> machine_code_') -and
    (Test-Pattern $function 'machine_code_\.store') -and
    (Test-Pattern $codeCache 'Fix up indirection table') -and
    (Test-Pattern $codeCache 'guest_address - kIndirectionTableBaseLow')

$hostToGuestPassesOnlyReturn =
    (Test-Pattern $backend 'Pass guest return address in x0') -and
    (Test-Pattern $backend 'mov\(x0, x2\).*guest return address')

$directGuestCallPassesOnlyReturn =
    (Test-Pattern $emitter 'fn->machine_code\(\)') -and
    (Test-Pattern $emitter 'StackLayout::GUEST_CALL_RET_ADDR') -and
    (Test-Pattern $emitter 'blr\(x9\)')

$tailAndIndirectNeedFallback =
    (Test-Pattern $emitter 'CALL_TAIL') -and
    (Test-Pattern $emitter 'CallIndirect') -and
    (Test-Pattern $backend 'ResolveFunctionThunk')

$stackpointLongjmpRequired =
    (Test-Pattern $backend 'a64_enable_host_guest_stack_synchronization') -and
    (Test-Pattern $emitter 'PushStackpoint') -and
    (Test-Pattern $emitter 'EnsureSynchronizedGuestAndHostStack') -and
    (Test-Pattern $backend 'longjmp')

$guestToHostBoundaryClobbers =
    (Test-Pattern $backend 'EmitGuestToHostThunk') -and
    (Test-Pattern $backend 'Save guest-allocated VEC regs') -and
    (Test-Pattern $backend 'Restore the guest scalar FPCR')

$debugExceptionVisibility =
    (Test-Pattern $backend 'ExceptionCallback') -and
    (Test-Pattern $emitter 'DebugBreak') -and
    (Test-Pattern $genericFunction 'LookupMachineCodeOffset')

$fixedRegisterPressure =
    (Test-Pattern $backend 'x19=backend ctx, x20=context, x21=membase') -and
    (Test-Pattern $backend 'gpr_set\.count = A64Emitter::GPR_COUNT')

$fastEntryDataModelPresent =
    (Test-Pattern $functionHeader 'A64GuestCallFastEntryContract') -and
    (Test-Pattern $functionHeader 'guest_call_fast_entry_code') -and
    (Test-Pattern $functionHeader 'dirty_flush')

$existingFastEntryBehavior =
    (Test-Pattern $backend 'EmitGuestCallFastEntry|late_bound_entry|alternate_codegen=1') -or
    (Test-Pattern $emitter 'EmitGuestCallFastEntry|alternate_codegen=1|fast_entry_stub|late_bound_entry')

Write-Output "audit=a64_guest_call_fast_entry_feasibility"
Emit-Check "normal_entry_singleton" $normalEntrySingleton $functionHeader $machineCodeMemberLine "A64Function exposes one machine_code pointer and the code cache publishes one guest-address indirection target."
Emit-Check "host_to_guest_entry_passes_only_return_x0" $hostToGuestPassesOnlyReturn $backend $hostToGuestReturnLine "Host entry sets x19/x20/x21 and passes only the guest PPC return address in x0."
Emit-Check "guest_entry_abi_documented_return_x0" ($normalEntryDocLine -gt 0) $stackLayout $normalEntryDocLine "The generated guest-function ABI documents x0 as guest PPC return address at entry."
Emit-Check "direct_guest_call_passes_only_return_x0" $directGuestCallPassesOnlyReturn $emitter $directCallMachineCodeLine "Direct guest calls load callee machine_code and pass only GUEST_CALL_RET_ADDR in x0 before blr."
Emit-Check "tail_indirect_unresolved_need_normal_fallback" $tailAndIndirectNeedFallback $emitter $tailCallReturnLine "Tail calls, indirect calls, and unresolved calls use separate paths that cannot assume a fast-entry payload."
Emit-Check "stackpoint_longjmp_constraints_active" $stackpointLongjmpRequired $emitter $pushStackpointLine "Function prolog/return paths maintain host/guest stackpoints for setjmp/longjmp recovery."
Emit-Check "guest_to_host_boundary_clobbers_payload" $guestToHostBoundaryClobbers $backend $guestToHostLine "Host calls save/restore wide guest state and FPCR, so fast-entry state cannot cross host callbacks implicitly."
Emit-Check "debug_exception_visibility_requires_normal_mapping" $debugExceptionVisibility $backend $exceptionLine "Breakpoints, traps, and source maps use normal machine-code mapping; variants must preserve debuggability/unwind assumptions."
Emit-Check "fixed_register_pressure_is_real" $fixedRegisterPressure $backend $registerSetLine "x19/x20/x21 are fixed and only x22-x28 are allocatable GPRs in the current backend."
Emit-Check "fast_entry_data_model_present" $fastEntryDataModelPresent $functionHeader $machineCodeMemberLine "A64Function has separate fast-entry storage plus payload/dirty-flush contract metadata."
Emit-Check "existing_fast_entry_behavior_absent" (-not $existingFastEntryBehavior) $emitter 0 "No generated fast-entry callsite behavior exists yet."

if ($normalEntrySingleton -and $hostToGuestPassesOnlyReturn -and
    $directGuestCallPassesOnlyReturn -and $stackpointLongjmpRequired -and
    $fastEntryDataModelPresent -and -not $existingFastEntryBehavior) {
    Write-Output "verdict=separate_fast_entry_data_model_present_behavior_absent"
    Write-Output "behavior_status=normal_entry_unchanged;global_indirection_unchanged;direct_calls_still_use_normal_entry"
    Write-Output "required_model=direct_callsite_guard;generated_fast_entry_stub_or_offset;explicit_argument_payload_for_r3_r10_lr;explicit_dirty_flush_to_PPCContext_before_barrier_helper_host_call_debug_trap_tail_return_exception;indirect_unresolved_extern_tail_normal_fallback"
    Write-Output "recommended_next_slice=no-op codegen protocol helpers with generated behavior unchanged, or lane switch"
} elseif ($normalEntrySingleton -and $hostToGuestPassesOnlyReturn -and
    $directGuestCallPassesOnlyReturn -and $stackpointLongjmpRequired -and
    -not $fastEntryDataModelPresent -and -not $existingFastEntryBehavior) {
    Write-Output "verdict=feasible_only_as_separate_fast_entry_path"
    Write-Output "blocked_behavior_patch=do_not_replace_A64Function_machine_code_or_global_indirection"
    Write-Output "required_model=normal_entry_unchanged; direct_callsite_guard; separate_fast_entry_stub_or_offset; explicit_argument_payload_for_r3_r10_lr; explicit_dirty_flush_to_PPCContext_before_barrier_helper_host_call_debug_trap_tail_return_exception; indirect_unresolved_extern_tail_normal_fallback"
    Write-Output "recommended_next_slice=default-off counter-only arm64_guest_call_fast_entry_audit skeleton that compiles no alternate code and counts eligible direct calls, tail/indirect/extern/unresolved blockers, stackpoint-sync boundaries, callee first-use coverage, and estimated avoidable context store/load traffic"
} else {
    Write-Output "verdict=blocked_until_source_contracts_are_clear"
    Write-Output "recommended_next_slice=write a blocker note before any runtime skeleton"
}
