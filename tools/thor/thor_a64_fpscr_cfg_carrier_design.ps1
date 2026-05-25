param(
    [string]$AuditLogPath = "scratch\thor-debug\20260525-132931-speed-logcat-filtered.txt",
    [string]$AuditDocPath = "docs\research\20260525-133353-blue-dragon-fpscr-cfg-writeback-audit.md",
    [string]$EmitterPath = "src\xenia\cpu\backend\a64\a64_emitter.cc",
    [string]$SequencesPath = "src\xenia\cpu\backend\a64\a64_sequences.cc",
    [string]$BackendHeaderPath = "src\xenia\cpu\backend\a64\a64_backend.h"
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path

function Resolve-RepoPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return (Join-Path $RepoRoot $Path)
}

function Read-RequiredText {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        throw "Path not found: $Path"
    }
    return Get-Content -Raw -LiteralPath $resolved
}

function Read-RequiredLines {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        throw "Path not found: $Path"
    }
    return @(Get-Content -LiteralPath $resolved)
}

function Read-OptionalText {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if ([string]::IsNullOrWhiteSpace($resolved) -or
        !(Test-Path -LiteralPath $resolved)) {
        return ""
    }
    return Get-Content -Raw -LiteralPath $resolved
}

function Test-Pattern {
    param(
        [string]$Text,
        [string]$Pattern
    )

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return $false
    }
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

function Emit-SourceCheck {
    param(
        [string]$Name,
        [bool]$Ok,
        [string]$File,
        [int]$Line,
        [string]$Impact
    )

    $status = if ($Ok) { "ok" } else { "missing" }
    $location = if ($Line -gt 0) { ("{0}:{1}" -f $File, $Line) } else { $File }
    Write-Output ("source_check name={0} status={1} source={2} impact={3}" -f
        $Name, $status, $location, $Impact)
}

function Format-Percent {
    param(
        [double]$Numerator,
        [double]$Denominator
    )

    if ($Denominator -eq 0) {
        return "0.00%"
    }
    return ("{0:N2}%" -f (($Numerator * 100.0) / $Denominator))
}

function Get-LastFpscrAuditPayload {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        throw "AuditLogPath not found: $Path"
    }

    $marker = "A64 Blue Dragon fpscr CFG writeback audit:"
    $payload = ""
    $current = ""
    foreach ($line in Get-Content -LiteralPath $resolved) {
        $index = $line.IndexOf($marker)
        if ($index -ge 0) {
            if (![string]::IsNullOrWhiteSpace($current)) {
                $payload = $current.Trim()
            }
            $current = $line.Substring($index + $marker.Length).Trim()
            continue
        }

        if (![string]::IsNullOrWhiteSpace($current)) {
            if ($line -match '^\d\d-\d\d \d\d:\d\d:\d\d\.\d+\s+') {
                $payload = $current.Trim()
                $current = ""
            } else {
                $current += " " + $line.Trim()
            }
        }
    }
    if (![string]::IsNullOrWhiteSpace($current)) {
        $payload = $current.Trim()
    }
    if ([string]::IsNullOrWhiteSpace($payload)) {
        throw "No A64 Blue Dragon fpscr CFG writeback audit row found in $Path."
    }
    return $payload
}

function Read-CounterTotals {
    param([string]$Payload)

    $counters = @{}
    foreach ($match in [regex]::Matches(
            $Payload,
            '(?<name>[A-Za-z0-9_]+)=(?<delta>[0-9]+)/(?<total>[0-9]+)')) {
        $name = $match.Groups["name"].Value
        $counters[$name] = [pscustomobject][ordered]@{
            delta = [int64]$match.Groups["delta"].Value
            total = [int64]$match.Groups["total"].Value
        }
    }

    foreach ($required in @(
            "static_load_sites",
            "static_store_sites",
            "static_cfg_transition_sites",
            "static_external_transition_sites",
            "static_call_writeback_sites",
            "loads",
            "stores",
            "cfg_transition_stores",
            "external_transition_stores",
            "required_call_writebacks",
            "call_82287ED4",
            "call_82287EDC",
            "call_82287EE4",
            "call_82288220",
            "payload_materializations",
            "unclassified_stores")) {
        if (!$counters.ContainsKey($required)) {
            throw "Counter row missing $required."
        }
    }

    return $counters
}

function Total {
    param(
        [hashtable]$Counters,
        [string]$Name
    )
    return [int64]$Counters[$Name].total
}

function Delta {
    param(
        [hashtable]$Counters,
        [string]$Name
    )
    return [int64]$Counters[$Name].delta
}

$auditPayload = Get-LastFpscrAuditPayload -Path $AuditLogPath
$counters = Read-CounterTotals -Payload $auditPayload

$emitterText = Read-RequiredText $EmitterPath
$emitterLines = Read-RequiredLines $EmitterPath
$sequencesText = Read-RequiredText $SequencesPath
$sequencesLines = Read-RequiredLines $SequencesPath
$backendHeaderText = Read-RequiredText $BackendHeaderPath
$backendHeaderLines = Read-RequiredLines $BackendHeaderPath
$auditDocText = Read-OptionalText $AuditDocPath

$normalEntryStoresGuestReturn =
    Test-Pattern $emitterText 'str\(x0,\s*ptr\(sp,\s*static_cast<uint32_t>\(StackLayout::GUEST_RET_ADDR\)\)\)'
$directCallGuestReturnOnly =
    Test-Pattern $emitterText 'ldr\(x0,\s*ptr\(sp,\s*static_cast<uint32_t>\(StackLayout::GUEST_CALL_RET_ADDR\)\)\);\s*blr\(x9\);'
$tailCallGuestReturnOnly =
    Test-Pattern $emitterText 'ldr\(x0,\s*ptr\(sp,\s*static_cast<uint32_t>\(StackLayout::GUEST_RET_ADDR\)\)\).*?br\(x9\);'
$calleeLocalStackSlotExample =
    Test-Pattern $emitterText 'arm64_blue_dragon_f1_carrier_fastpath\s*&&\s*current_guest_function_\s*==\s*0x82287788'
$fpscrLoadAuditExists =
    Test-Pattern $sequencesText 'EmitBlueDragonFpscrCfgWritebackLoadAudit'
$fpscrStoreClassificationExists =
    Test-Pattern $sequencesText 'GetBlueDragonFpscrCfgStoreKind'
$fpscrCallWritebackCountersExist =
    Test-Pattern $emitterText 'blue_dragon_fpscr_cfg_call_writeback_82287edc_count'
$payloadCounterExists =
    Test-Pattern $backendHeaderText 'blue_dragon_fpscr_cfg_payload_materialized_count_'
$unclassifiedCounterExists =
    Test-Pattern $backendHeaderText 'blue_dragon_fpscr_cfg_unclassified_store_count_'
$parentFpscrSeedAuditExists =
    Test-Pattern $sequencesText 'guest_function\s*==\s*0x82282490\s*&&\s*guest_pc\s*==\s*0x82282594.*?offset\s*==\s*2628'

$requiredWritebacks = @(
    [pscustomobject][ordered]@{
        pc = "82287ED4"
        total = Total $counters "call_82287ED4"
        role = "static_required_route_dormant_in_latest_capture"
    },
    [pscustomobject][ordered]@{
        pc = "82287EDC"
        total = Total $counters "call_82287EDC"
        role = "dominant_dynamic_writeback"
    },
    [pscustomobject][ordered]@{
        pc = "82287EE4"
        total = Total $counters "call_82287EE4"
        role = "secondary_dynamic_writeback"
    },
    [pscustomobject][ordered]@{
        pc = "82288220"
        total = Total $counters "call_82288220"
        role = "secondary_dynamic_writeback"
    }
)

$stores = Total $counters "stores"
$cfgStores = Total $counters "cfg_transition_stores"
$externalStores = Total $counters "external_transition_stores"
$requiredCalls = Total $counters "required_call_writebacks"
$payloadMaterializations = Total $counters "payload_materializations"
$unclassifiedStores = Total $counters "unclassified_stores"
$staticLoads = Total $counters "static_load_sites"
$staticStores = Total $counters "static_store_sites"
$staticCfgSites = Total $counters "static_cfg_transition_sites"
$staticExternalSites = Total $counters "static_external_transition_sites"
$staticCallSites = Total $counters "static_call_writeback_sites"
$writebackCoverageOk =
    ($staticLoads -eq 26) -and ($staticStores -eq 26) -and
    ($staticCfgSites -eq 24) -and ($staticExternalSites -eq 2) -and
    ($staticCallSites -eq 4) -and ($unclassifiedStores -eq 0) -and
    ($payloadMaterializations -eq 0)
$sourceCoverageOk =
    $normalEntryStoresGuestReturn -and $directCallGuestReturnOnly -and
    $tailCallGuestReturnOnly -and $fpscrLoadAuditExists -and
    $fpscrStoreClassificationExists -and $fpscrCallWritebackCountersExist -and
    $payloadCounterExists -and $unclassifiedCounterExists
$normalEntryUnchanged =
    ($auditPayload -match 'normal_entry=unchanged') -and $normalEntryStoresGuestReturn

Write-Output "# A64 FPSCR CFG Carrier Design"
Write-Output ""
Write-Output ("audit_log={0}" -f $AuditLogPath)
Write-Output ("audit_doc={0}" -f $(if ($auditDocText) { $AuditDocPath } else { "-" }))
Write-Output ("latest_audit_row={0}" -f $auditPayload)
Write-Output ""

Write-Output "## Source Checks"
Emit-SourceCheck "normal_entry_stores_x0_guest_return" $normalEntryStoresGuestReturn $EmitterPath `
    (Find-Line $emitterLines 'StackLayout::GUEST_RET_ADDR') `
    "normal 82287788 entry must keep x0 guest-return semantics"
Emit-SourceCheck "direct_call_uses_guest_return_only_x0" $directCallGuestReturnOnly $EmitterPath `
    (Find-Line $emitterLines 'ldr\(x0, ptr\(sp, static_cast<uint32_t>\(StackLayout::GUEST_CALL_RET_ADDR\)') `
    "direct call ABI has no fpscr carrier argument today"
Emit-SourceCheck "tail_call_uses_guest_return_only_x0" $tailCallGuestReturnOnly $EmitterPath `
    (Find-Line $emitterLines 'Tail call: pass our return address to the callee') `
    "tail calls also cannot receive an extra fpscr payload today"
Emit-SourceCheck "callee_local_stack_slot_precedent" $calleeLocalStackSlotExample $EmitterPath `
    (Find-Line $emitterLines 'blue_dragon_f1_carrier_stack_slot_offset_') `
    "the existing f1 carrier shape is callee-local, not a parent-callee ABI"
Emit-SourceCheck "fpscr_load_audit_exists" $fpscrLoadAuditExists $SequencesPath `
    (Find-Line $sequencesLines 'EmitBlueDragonFpscrCfgWritebackLoadAudit') `
    "load-side coverage exists for the fpscr context offset"
Emit-SourceCheck "fpscr_store_classification_exists" $fpscrStoreClassificationExists $SequencesPath `
    (Find-Line $sequencesLines 'GetBlueDragonFpscrCfgStoreKind') `
    "store-side CFG/external/unclassified site coverage exists"
Emit-SourceCheck "fpscr_call_writeback_counters_exist" $fpscrCallWritebackCountersExist $EmitterPath `
    (Find-Line $emitterLines 'blue_dragon_fpscr_cfg_call_writeback_82287edc_count') `
    "call-visible writeback PCs are counted before calls"
Emit-SourceCheck "payload_and_unclassified_counters_exist" ($payloadCounterExists -and $unclassifiedCounterExists) $BackendHeaderPath `
    (Find-Line $backendHeaderLines 'blue_dragon_fpscr_cfg_payload_materialized_count_') `
    "future behavior patch can prove materialization and bailouts"
Emit-SourceCheck "parent_fpscr_seed_audit_exists" $parentFpscrSeedAuditExists $SequencesPath `
    (Find-Line $sequencesLines 'guest_pc == 0x82282594') `
    "caller edge may still seed fpscr, but current ABI cannot pass it into normal callee entry"
Write-Output ""

Write-Output "## Dynamic Counter Contract"
Write-Output ("static_sites loads={0} stores={1} cfg_transitions={2} external_transitions={3} call_writebacks={4}" -f
    $staticLoads, $staticStores, $staticCfgSites, $staticExternalSites, $staticCallSites)
Write-Output ("dynamic_totals loads={0} stores={1} cfg_transition_stores={2} external_transition_stores={3} required_call_writebacks={4}" -f
    (Total $counters "loads"), $stores, $cfgStores, $externalStores, $requiredCalls)
Write-Output ("dynamic_shares cfg_store_share={0} external_store_share={1} call_writebacks_per_store={2}" -f
    (Format-Percent $cfgStores $stores),
    (Format-Percent $externalStores $stores),
    (Format-Percent $requiredCalls $stores))
foreach ($site in $requiredWritebacks) {
    Write-Output ("required_writeback pc={0} total={1} share_of_call_writebacks={2} role={3}" -f
        $site.pc,
        $site.total,
        (Format-Percent $site.total $requiredCalls),
        $site.role)
}
Write-Output ("payload_materializations={0} unclassified_stores={1} normal_entry={2}" -f
    $payloadMaterializations,
    $unclassifiedStores,
    $(if ($normalEntryUnchanged) { "unchanged" } else { "needs_manual_review" }))
Write-Output ""

Write-Output "## Candidate Shape"
Write-Output "candidate=callee_local_fpscr_dirty_stack_slot"
Write-Output "normal_entry_behavior=unchanged"
Write-Output "parent_to_callee_payload=current_direct_call_abi_blocks_extra_payload"
Write-Output "seed_source=first_82287788_context_load_or_normal_context_fallback"
Write-Output "dirty_state=callee_local_dirty_flag"
Write-Output "load_rule=serve_from_carrier_only_after_seed"
Write-Output "store_rule=update_carrier_and_mark_dirty"
Write-Output "writeback_rule=flush_dirty_before_cfg_external_visibility_and_required_call_pcs"
Write-Output "required_call_pcs=82287ED4,82287EDC,82287EE4,82288220"
Write-Output "fallback_rule=write_through_or_disable_on_unclassified_store_or_unknown_visibility"
Write-Output "future_behavior_cvar=arm64_blue_dragon_fpscr_cfg_carrier_fastpath"
Write-Output ""

Write-Output "## Decision"
if (!$sourceCoverageOk) {
    Write-Output "decision=blocked_source_coverage_incomplete"
    Write-Output "reason=one or more required source checks is missing; inspect Source Checks before any codegen behavior patch."
} elseif (!$writebackCoverageOk) {
    Write-Output "decision=blocked_counter_contract_incomplete"
    Write-Output ("reason=static/counter contract expected 26 loads, 26 stores, 24 cfg stores, 2 external stores, 4 call sites, 0 payload materializations, and 0 unclassified stores; observed payload_materializations={0} unclassified_stores={1}." -f
        $payloadMaterializations, $unclassifiedStores)
} else {
    Write-Output "decision=no_behavior_patch_yet"
    Write-Output ("reason=latest route proves dynamic fpscr traffic and exact call writeback demand, but current direct/tail call ABI still passes only guest-return metadata in x0; use a callee-local dirty carrier or return to caller-local edge storage, not a silent ABI change.")
}
Write-Output "safe_next_patch=default-off callee-local fpscr carrier skeleton with counters and normal-entry fallback, or caller-local 82282490:82282598 edge-variant storage."
Write-Output "do_not_run=quiet fpscr speed A/B before behavior materialization counters are intentionally nonzero and writebacks remain accounted."
