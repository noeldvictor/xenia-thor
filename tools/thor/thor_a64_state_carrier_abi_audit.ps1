param(
    [string]$EmitterPath = "src/xenia/cpu/backend/a64/a64_emitter.cc",
    [string]$SequencesPath = "src/xenia/cpu/backend/a64/a64_sequences.cc",
    [string]$BackendHeaderPath = "src/xenia/cpu/backend/a64/a64_backend.h",
    [string]$FollowupPlanPath = "docs/research/20260524-145241-state-carrier-followup-plan.md"
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

function Get-FirstMatch {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Default = "-"
    )

    $match = [regex]::Match($Text, $Pattern)
    if (!$match.Success -or $match.Groups.Count -le 1) {
        return $Default
    }
    return $match.Groups[1].Value
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

$emitterText = Read-TextFile $EmitterPath
$emitterLines = Read-LineFile $EmitterPath
$sequencesText = Read-TextFile $SequencesPath
$sequencesLines = Read-LineFile $SequencesPath
$backendHeaderText = Read-TextFile $BackendHeaderPath
$backendHeaderLines = Read-LineFile $BackendHeaderPath
$followupText = ""
if (![string]::IsNullOrWhiteSpace($FollowupPlanPath) -and
    (Test-Path -LiteralPath $FollowupPlanPath)) {
    $followupText = Read-TextFile $FollowupPlanPath
}

$prologStoresX0 =
    Test-Pattern $emitterText 'str\(x0,\s*ptr\(sp,\s*static_cast<uint32_t>\(StackLayout::GUEST_RET_ADDR\)\)\)'
$directCallPassesGuestReturnOnly =
    Test-Pattern $emitterText 'ldr\(x0,\s*ptr\(sp,\s*static_cast<uint32_t>\(StackLayout::GUEST_CALL_RET_ADDR\)\)\);\s*blr\(x9\);'
$tailCallPassesGuestReturnOnly =
    Test-Pattern $emitterText 'ldr\(x0,\s*ptr\(sp,\s*static_cast<uint32_t>\(StackLayout::GUEST_RET_ADDR\)\)\).*?br\(x9\);'
$f1FastpathLocalToCallee =
    Test-Pattern $emitterText 'arm64_blue_dragon_f1_carrier_fastpath.*?current_guest_function_\s*==\s*0x82287788'
$callAuditInCallee =
    Test-Pattern $emitterText 'current_guest_function_\s*==\s*0x82287788.*?blue_dragon_state_carrier_f1_child_call_count'
$fpscrWritebackAudit =
    Test-Pattern $emitterText 'blue_dragon_state_carrier_fpscr_writeback_count'
$stateLoadAudit =
    Test-Pattern $sequencesText 'EmitBlueDragonStateCarrierDesignLoadAudit'
$stateStoreAudit =
    Test-Pattern $sequencesText 'EmitBlueDragonStateCarrierDesignStoreAudit'
$backendCounters =
    Test-Pattern $backendHeaderText 'blue_dragon_state_carrier_f1_read_count_.*?blue_dragon_state_carrier_fpscr_fallback_count_'

$f1Read = Get-FirstMatch $followupText 'f1 total_reads=([0-9]+)'
$f1Fallback = Get-FirstMatch $followupText 'fallback_total=([0-9]+)'
$fpscrRead = Get-FirstMatch $followupText 'fpscr total_reads=([0-9]+)'
$fpscrWritebacks = Get-FirstMatch $followupText 'required_writebacks=([0-9]+)'
$fpscrCallKills = Get-FirstMatch $followupText 'call_kills=([0-9]+)'

Write-Output "# A64 State-Carrier ABI Audit"
Write-Output ""
Write-Output ("emitter={0}" -f $EmitterPath)
Write-Output ("sequences={0}" -f $SequencesPath)
Write-Output ("backend_header={0}" -f $BackendHeaderPath)
Write-Output ("followup_plan={0}" -f $(if ($followupText) { $FollowupPlanPath } else { "-" }))
Write-Output ""

Write-Output "## Source Checks"
Emit-Check "callee_prolog_stores_x0_guest_return" $prologStoresX0 $EmitterPath `
    (Find-Line $emitterLines 'StackLayout::GUEST_RET_ADDR') `
    "normal compiled entries consume x0 as guest return metadata"
Emit-Check "direct_call_loads_x0_guest_call_return_then_blr" $directCallPassesGuestReturnOnly $EmitterPath `
    (Find-Line $emitterLines 'ldr\(x0, ptr\(sp, static_cast<uint32_t>\(StackLayout::GUEST_CALL_RET_ADDR\)') `
    "current direct-call ABI has no explicit state-carrier argument"
Emit-Check "tail_call_loads_x0_guest_return_then_br" $tailCallPassesGuestReturnOnly $EmitterPath `
    (Find-Line $emitterLines 'br\(x9\)') `
    "tail calls share the same guest-return-only x0 convention"
Emit-Check "existing_f1_fastpath_is_callee_local" $f1FastpathLocalToCallee $EmitterPath `
    (Find-Line $emitterLines 'current_guest_function_ == 0x82287788') `
    "the proven f1 fastpath is a local callee stack slot, not a parent-callee ABI"
Emit-Check "callee_call_audit_counts_child_edges" $callAuditInCallee $EmitterPath `
    (Find-Line $emitterLines 'blue_dragon_state_carrier_f1_child_call_count') `
    "current counters know child pressure but do not carry state across calls"
Emit-Check "fpscr_writeback_audit_exists" $fpscrWritebackAudit $EmitterPath `
    (Find-Line $emitterLines 'blue_dragon_state_carrier_fpscr_writeback_count') `
    "fpscr behavior needs exact writeback placement before a fastpath"
Emit-Check "state_load_audit_exists" $stateLoadAudit $SequencesPath `
    (Find-Line $sequencesLines 'EmitBlueDragonStateCarrierDesignLoadAudit') `
    "load-side f1/fpscr counter coverage exists"
Emit-Check "state_store_audit_exists" $stateStoreAudit $SequencesPath `
    (Find-Line $sequencesLines 'EmitBlueDragonStateCarrierDesignStoreAudit') `
    "store-side seed/dirty/fallback counter coverage exists"
Emit-Check "backend_state_carrier_counters_exist" $backendCounters $BackendHeaderPath `
    (Find-Line $backendHeaderLines 'blue_dragon_state_carrier_f1_read_count_') `
    "runtime counters are wired through A64Backend"
Write-Output ""

Write-Output "## Prior Route-Clean Counter Context"
Write-Output ("f1_reads={0} f1_fallbacks={1} fpscr_reads={2} fpscr_required_writebacks={3} fpscr_call_kills={4}" -f `
        $f1Read, $f1Fallback, $fpscrRead, $fpscrWritebacks, $fpscrCallKills)
Write-Output ""

$abiIsGuestReturnOnly =
    $prologStoresX0 -and $directCallPassesGuestReturnOnly -and $tailCallPassesGuestReturnOnly
$auditCoverageExists =
    $callAuditInCallee -and $fpscrWritebackAudit -and $stateLoadAudit -and
    $stateStoreAudit -and $backendCounters

Write-Output "## Contract"
Write-Output ("current_direct_call_abi={0}" -f $(if ($abiIsGuestReturnOnly) { "guest_return_only_in_x0" } else { "needs_manual_review" }))
Write-Output ("existing_f1_carrier_scope={0}" -f $(if ($f1FastpathLocalToCallee) { "callee_local_stack_slot" } else { "needs_manual_review" }))
Write-Output ("existing_counter_coverage={0}" -f $(if ($auditCoverageExists) { "enough_for_design_audit" } else { "incomplete" }))
Write-Output "parent_to_callee_payload_status=blocked_without_call_abi_extension_or_edge_specific_entry"
Write-Output "normal_entry_requirement=must_keep_context_load_fallback"
Write-Output "safe_next_behavior_patch=none"
Write-Output "safe_next_tooling_patch=edge-specific ABI/variant design audit with compile-time and runtime counters"
Write-Output ""

Write-Output "## Recommendation"
Write-Output "Do not make another quiet speed A/B from the current state-carrier evidence."
Write-Output "A broader 82282490->82287788 carrier needs either a new edge-specific compiled entry or a deliberate call-ABI extension; merely preloading caller registers cannot affect normal callee context loads."
Write-Output "Treat f[1] as the first read-only payload candidate only after the ABI shape is explicit, and keep fpscr audit-only until exact CFG writeback/flush points are proven."
