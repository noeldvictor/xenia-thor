param(
    [string]$BackendPath = "src/xenia/cpu/backend/a64/a64_backend.cc",
    [string]$EmitterPath = "src/xenia/cpu/backend/a64/a64_emitter.cc",
    [string]$A64FunctionHeaderPath = "src/xenia/cpu/backend/a64/a64_function.h",
    [string]$A64FunctionPath = "src/xenia/cpu/backend/a64/a64_function.cc",
    [string]$CodeCachePath = "src/xenia/cpu/backend/code_cache_base.h",
    [string]$FunctionHeaderPath = "src/xenia/cpu/function.h",
    [string]$AbiAuditPath = "docs/research/20260524-163338-a64-state-carrier-abi-audit.md"
)

$ErrorActionPreference = "Stop"

function Read-TextFile {
    param([string]$Path)

    if (!(Test-Path -LiteralPath $Path)) {
        throw "Path not found: $Path"
    }
    return Get-Content -Raw -LiteralPath $Path
}

function Read-LineFile {
    param([string]$Path)

    if (!(Test-Path -LiteralPath $Path)) {
        throw "Path not found: $Path"
    }
    return @(Get-Content -LiteralPath $Path)
}

function Test-Pattern {
    param(
        [string]$Text,
        [string]$Pattern
    )

    return [regex]::IsMatch(
        $Text,
        $Pattern,
        [System.Text.RegularExpressions.RegexOptions]::Singleline)
}

function Find-Line {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )

    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -match $Pattern) {
            return ($i + 1)
        }
    }
    return 0
}

function Emit-Check {
    param(
        [string]$Name,
        [bool]$Ok,
        [string]$File,
        [int]$Line,
        [string]$Impact
    )

    $status = if ($Ok) { "ok" } else { "missing" }
    $location = if ($Line -gt 0) { ("{0}:{1}" -f $File, $Line) } else { $File }
    Write-Output ("check name={0} status={1} source={2} impact={3}" -f `
            $Name, $status, $location, $Impact)
}

function Bool-Word {
    param([bool]$Value)
    if ($Value) { return "true" }
    return "false"
}

$backendText = Read-TextFile $BackendPath
$backendLines = Read-LineFile $BackendPath
$emitterText = Read-TextFile $EmitterPath
$emitterLines = Read-LineFile $EmitterPath
$a64FunctionHeaderText = Read-TextFile $A64FunctionHeaderPath
$a64FunctionHeaderLines = Read-LineFile $A64FunctionHeaderPath
$a64FunctionText = Read-TextFile $A64FunctionPath
$a64FunctionLines = Read-LineFile $A64FunctionPath
$codeCacheText = Read-TextFile $CodeCachePath
$codeCacheLines = Read-LineFile $CodeCachePath
$functionHeaderText = Read-TextFile $FunctionHeaderPath
$functionHeaderLines = Read-LineFile $FunctionHeaderPath
$abiAuditText = ""
if (![string]::IsNullOrWhiteSpace($AbiAuditPath) -and
    (Test-Path -LiteralPath $AbiAuditPath)) {
    $abiAuditText = Read-TextFile $AbiAuditPath
}

$hostToGuestX0Return =
    Test-Pattern $backendText 'HostToGuestThunk A64HelperEmitter::EmitHostToGuestThunk\(\).*?mov\(x9,\s*x0\).*?mov\(x0,\s*x2\).*?blr\(x9\);'
$resolveThunkNormalEntry =
    Test-Pattern $backendText 'ResolveFunctionThunk A64HelperEmitter::EmitResolveFunctionThunk\(\).*?stp\(x0,\s*x19,\s*ptr\(sp,\s*0x00\)\).*?ResolveFunction\(context,\s*target_address\).*?ldp\(x0,\s*x19,\s*ptr\(sp,\s*0x00\)\).*?br\(x9\);'
$resolveFunctionNormalMachineCode =
    Test-Pattern $backendText 'uint64_t ResolveFunction\(void\* raw_context,\s*uint64_t target_address\).*?processor\(\)->ResolveFunction.*?guest_fn->machine_code\(\).*?return reinterpret_cast<uint64_t>\(code\);'
$directCallNormalMachineCode =
    Test-Pattern $emitterText 'if \(fn->machine_code\(\)\).*?mov\(x9,\s*reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)\).*?GUEST_CALL_RET_ADDR.*?blr\(x9\);'
$tailCallNormalMachineCode =
    Test-Pattern $emitterText 'mov\(x9,\s*reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)\).*?GUEST_RET_ADDR.*?br\(x9\);'
$unresolvedIndirectionGuestAddress =
    Test-Pattern $emitterText 'code_cache_->has_indirection_table\(\).*?mov\(w16,\s*function->address\(\)\).*?ldr\(w9,\s*ptr\(x16'
$unresolvedResolveNormalMachineCode =
    Test-Pattern $emitterText 'mov\(x0,\s*x20\);\s*// context.*?mov\(x1,\s*static_cast<uint64_t>\(function->address\(\)\)\).*?ResolveFunction.*?mov\(x9,\s*x0\);'
$callEdgeCountersNoPayload =
    Test-Pattern $emitterText 'set_profile_call_edge_addresses\(.*?current_block_guest_address_,\s*function->address\(\)\)'
$placeGuestCodeGuestAddress =
    Test-Pattern $emitterText 'code_cache_->PlaceGuestCode\(\s*function->address\(\)'
$addIndirectionGuestAddressOnly =
    Test-Pattern $codeCacheText 'void AddIndirection\(uint32_t guest_address,\s*uint32_t host_address\).*?guest_address - kIndirectionTableBaseLow'
$placeGuestCodeSingleMapping =
    Test-Pattern $codeCacheText 'void PlaceGuestCode\(uint32_t guest_address,\s*void\* machine_code,.*?GuestFunction\* function_info.*?generated_code_map_\.emplace_back.*?function_info'
$a64FunctionSingleSlot =
    (Test-Pattern $a64FunctionHeaderText 'std::atomic<uint8_t\*> machine_code_\{nullptr\}') -and
    (Test-Pattern $a64FunctionText 'void A64Function::Setup\(uint8_t\* machine_code,\s*size_t machine_code_length\).*?machine_code_\.store\(machine_code')
$a64FunctionHostEntrySingleSlot =
    Test-Pattern $a64FunctionText 'A64Function::CallImpl\(ThreadState\* thread_state,\s*uint32_t return_address\).*?machine_code_\.load.*?thunk\(code,\s*thread_state->context\(\)'
$guestFunctionSingleMachineCodeInterface =
    (Test-Pattern $functionHeaderText 'virtual uint8_t\* machine_code\(\) const = 0;') -and
    (Test-Pattern $functionHeaderText 'virtual size_t machine_code_length\(\) const = 0;')
$priorAbiAuditSaysBlocked =
    Test-Pattern $abiAuditText 'parent_to_callee_payload_status=blocked_without_call_abi_extension_or_edge_specific_entry'

Write-Output "# A64 Edge Variant Design Audit"
Write-Output ""
Write-Output ("backend={0}" -f $BackendPath)
Write-Output ("emitter={0}" -f $EmitterPath)
Write-Output ("a64_function_header={0}" -f $A64FunctionHeaderPath)
Write-Output ("a64_function={0}" -f $A64FunctionPath)
Write-Output ("code_cache={0}" -f $CodeCachePath)
Write-Output ("function_header={0}" -f $FunctionHeaderPath)
Write-Output ("prior_abi_audit={0}" -f $(if ($abiAuditText) { $AbiAuditPath } else { "-" }))
Write-Output ""

Write-Output "## Source Checks"
Emit-Check "host_to_guest_entry_passes_guest_return_in_x0" $hostToGuestX0Return `
    $BackendPath (Find-Line $backendLines 'mov\(x0,\s*x2\)') `
    "normal host entry cannot repurpose x0 for a state payload"
Emit-Check "resolve_thunk_preserves_x0_and_jumps_normal_entry" $resolveThunkNormalEntry `
    $BackendPath (Find-Line $backendLines 'ResolveFunctionThunk A64HelperEmitter::EmitResolveFunctionThunk') `
    "uncompiled direct-call resolution restores guest-return x0 before jumping"
Emit-Check "resolve_function_returns_normal_machine_code" $resolveFunctionNormalMachineCode `
    $BackendPath (Find-Line $backendLines 'guest_fn->machine_code\(\)') `
    "runtime resolve has no edge-specific variant lookup"
Emit-Check "compiled_direct_call_uses_normal_machine_code_and_x0_return" $directCallNormalMachineCode `
    $EmitterPath (Find-Line $emitterLines 'fn->machine_code\(\)') `
    "already-compiled calls have no explicit state-payload argument"
Emit-Check "compiled_tail_call_uses_normal_machine_code_and_x0_return" $tailCallNormalMachineCode `
    $EmitterPath (Find-Line $emitterLines 'Tail call: pass our return address') `
    "tail-call behavior must stay normal-entry compatible"
Emit-Check "unresolved_indirection_key_is_guest_address" $unresolvedIndirectionGuestAddress `
    $EmitterPath (Find-Line $emitterLines 'mov\(w16,\s*function->address\(\)\)') `
    "the indirection table cannot distinguish caller edge variants"
Emit-Check "unresolved_resolve_path_returns_normal_entry" $unresolvedResolveNormalMachineCode `
    $EmitterPath (Find-Line $emitterLines 'Fallback: resolve at runtime') `
    "fallback resolution needs normal-entry fallback semantics"
Emit-Check "call_edge_profile_records_edge_but_not_payload" $callEdgeCountersNoPayload `
    $EmitterPath (Find-Line $emitterLines 'set_profile_call_edge_addresses') `
    "existing call-edge data is useful for counters but not an ABI"
Emit-Check "emitter_places_one_guest_code_entry" $placeGuestCodeGuestAddress `
    $EmitterPath (Find-Line $emitterLines 'PlaceGuestCode') `
    "current emit path installs one normal guest entry"
Emit-Check "code_cache_indirection_slot_is_guest_address_only" $addIndirectionGuestAddressOnly `
    $CodeCachePath (Find-Line $codeCacheLines 'void AddIndirection') `
    "do not replace the global 82287788 indirection slot with an edge variant"
Emit-Check "code_cache_generated_map_points_to_one_guest_function" $placeGuestCodeSingleMapping `
    $CodeCachePath (Find-Line $codeCacheLines 'generated_code_map_\.emplace_back') `
    "variant code would need explicit source-map/unwind/profiling ownership"
Emit-Check "a64_function_has_single_machine_code_slot" $a64FunctionSingleSlot `
    $A64FunctionHeaderPath (Find-Line $a64FunctionHeaderLines 'machine_code_\{nullptr\}') `
    "normal A64Function entry is a singleton"
Emit-Check "host_call_entry_uses_single_machine_code_slot" $a64FunctionHostEntrySingleSlot `
    $A64FunctionPath (Find-Line $a64FunctionLines 'A64Function::CallImpl') `
    "host-side function calls also enter the singleton normal entry"
Emit-Check "guest_function_interface_exposes_single_machine_code" $guestFunctionSingleMachineCodeInterface `
    $FunctionHeaderPath (Find-Line $functionHeaderLines 'virtual uint8_t\* machine_code') `
    "the shared GuestFunction contract has no variant ABI"
Emit-Check "prior_state_carrier_abi_audit_blocks_plain_payload" $priorAbiAuditSaysBlocked `
    $AbiAuditPath 0 `
    "this design audit must not reopen plain register preloading as a behavior patch"
Write-Output ""

$normalEntrySingleton =
    $resolveFunctionNormalMachineCode -and $directCallNormalMachineCode -and
    $placeGuestCodeGuestAddress -and $a64FunctionSingleSlot -and
    $guestFunctionSingleMachineCodeInterface
$guestAddressOnlyIndirection =
    $unresolvedIndirectionGuestAddress -and $addIndirectionGuestAddressOnly
$currentCallAbiGuestReturnOnly =
    $hostToGuestX0Return -and $directCallNormalMachineCode -and
    $tailCallNormalMachineCode -and $unresolvedResolveNormalMachineCode
$needsSeparateVariantPath =
    $normalEntrySingleton -and $guestAddressOnlyIndirection -and
    $currentCallAbiGuestReturnOnly

Write-Output "## Target Edge"
Write-Output "parent_function=82282490"
Write-Output "call_pc=82282598"
Write-Output "callee_function=82287788"
Write-Output "first_payload_candidate=f1_read_only"
Write-Output "fpscr_payload_candidate=audit_only_until_exact_cfg_writebacks"
Write-Output ""

Write-Output "## Contract"
Write-Output ("normal_entry_singleton={0}" -f (Bool-Word $normalEntrySingleton))
Write-Output ("indirection_key={0}" -f $(if ($guestAddressOnlyIndirection) { "guest_address_only" } else { "needs_manual_review" }))
Write-Output ("machine_code_slot={0}" -f $(if ($a64FunctionSingleSlot) { "single_per_guest_function" } else { "needs_manual_review" }))
Write-Output ("resolve_path={0}" -f $(if ($resolveFunctionNormalMachineCode) { "normal_machine_code_only" } else { "needs_manual_review" }))
Write-Output ("current_call_abi={0}" -f $(if ($currentCallAbiGuestReturnOnly) { "guest_return_only_in_x0" } else { "needs_manual_review" }))
Write-Output ("edge_variant_without_global_entrypoint={0}" -f $(if ($needsSeparateVariantPath) { "caller_local_or_side_table_required" } else { "needs_manual_review" }))
Write-Output "full_call_abi_extension_risk=high"
Write-Output "safe_behavior_patch=none"
Write-Output "recommended_next_patch=default_off_compile_runtime_counter_only"
Write-Output ""

Write-Output "## Required Counter Shape"
Write-Output "compile_time_counters=eligible_edge_compiles,normal_function_compiles,variant_codegen_skipped,variant_storage_missing"
Write-Output "runtime_counters=eligible_edge_calls,normal_entry_fallbacks,payload_candidate_reads,payload_materializations,child_call_kills,variant_miss_or_unresolved"
Write-Output "fallback_rules=normal_entry_context_load_fallback,unresolved_indirection_normal_entry,tail_call_unchanged,host_to_guest_entry_unchanged,source_map_unwind_unchanged"
Write-Output ""

Write-Output "## Do Not"
Write-Output "replace_82287788_machine_code=false"
Write-Output "change_indirection_slot=false"
Write-Output "use_x1_payload_globally=false"
Write-Output "quiet_speed_ab_before_counters=false"
