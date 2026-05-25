param(
    [string]$EdgeCounterLogPath = "",
    [string]$EdgeDesignAuditPath = "docs/research/20260524-165127-a64-edge-variant-design-audit.md",
    [string]$StateCarrierFollowupPath = "docs/research/20260524-145241-state-carrier-followup-plan.md",
    [string]$RoundtripAuditPath = "docs/research/20260524-095137-82282598-82287788-state-roundtrip-audit.md",
    [string]$FpscrDirtyCacheAuditPath = "docs/research/20260524-114614-82287788-fpscr-dirty-cache-audit.md",
    [string]$BackendPath = "src/xenia/cpu/backend/a64/a64_backend.cc",
    [string]$EmitterPath = "src/xenia/cpu/backend/a64/a64_emitter.cc",
    [string]$A64FunctionHeaderPath = "src/xenia/cpu/backend/a64/a64_function.h",
    [string]$CodeCachePath = "src/xenia/cpu/backend/code_cache_base.h",
    [string]$FunctionHeaderPath = "src/xenia/cpu/function.h"
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

function Resolve-OptionalPath {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if ([string]::IsNullOrWhiteSpace($resolved)) {
        return ""
    }
    if (!(Test-Path -LiteralPath $resolved)) {
        return ""
    }
    return (Resolve-Path -LiteralPath $resolved).Path
}

function Read-RequiredText {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        throw "Path not found: $Path"
    }
    return Get-Content -Raw -LiteralPath $resolved
}

function Read-OptionalText {
    param([string]$Path)

    $resolved = Resolve-OptionalPath $Path
    if ([string]::IsNullOrWhiteSpace($resolved)) {
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

function Format-Ratio {
    param(
        [double]$Numerator,
        [double]$Denominator
    )

    if ($Denominator -eq 0) {
        return "0.00"
    }
    return ("{0:N2}" -f ($Numerator / $Denominator))
}

function Get-FirstValue {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Default = "-"
    )

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return $Default
    }
    $match = [regex]::Match(
        $Text,
        $Pattern,
        [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (!$match.Success) {
        return $Default
    }
    if ($match.Groups.Count -gt 1) {
        return $match.Groups[1].Value
    }
    return $match.Value
}

function Find-DefaultEdgeCounterLog {
    $scratch = Join-Path $RepoRoot "scratch\thor-debug"
    if (!(Test-Path -LiteralPath $scratch)) {
        return ""
    }

    $logs = Get-ChildItem -LiteralPath $scratch -Recurse -Filter "*-speed-logcat.txt" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending
    foreach ($log in $logs) {
        if (Select-String -LiteralPath $log.FullName -Pattern "A64 Blue Dragon edge-variant audit:" -Quiet) {
            return $log.FullName
        }
    }
    return ""
}

function Get-LatestEdgeCounterPayload {
    param([string]$Path)

    if (!(Test-Path -LiteralPath $Path)) {
        throw "EdgeCounterLogPath not found: $Path"
    }

    $payload = $null
    foreach ($line in Get-Content -LiteralPath $Path) {
        $index = $line.IndexOf("A64 Blue Dragon edge-variant audit:")
        if ($index -lt 0) {
            continue
        }
        $payload = $line.Substring(
            $index + "A64 Blue Dragon edge-variant audit:".Length).Trim()
    }
    if ([string]::IsNullOrWhiteSpace($payload)) {
        throw "No A64 Blue Dragon edge-variant audit row found in $Path."
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
            "eligible_compiles",
            "variant_storage_missing",
            "eligible_calls",
            "payload_materializations",
            "normal_entry_fallbacks",
            "indirection_fallbacks",
            "call_kills",
            "variant_misses")) {
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

function Total-Optional {
    param(
        [hashtable]$Counters,
        [string]$Name
    )
    if (!$Counters.ContainsKey($Name)) {
        return 0
    }
    return [int64]$Counters[$Name].total
}

function Delta {
    param(
        [hashtable]$Counters,
        [string]$Name
    )
    return [int64]$Counters[$Name].delta
}

function Delta-Optional {
    param(
        [hashtable]$Counters,
        [string]$Name
    )
    if (!$Counters.ContainsKey($Name)) {
        return 0
    }
    return [int64]$Counters[$Name].delta
}

function Emit-SourceCheck {
    param(
        [string]$Name,
        [bool]$Ok,
        [string]$Impact
    )

    $status = if ($Ok) { "ok" } else { "missing" }
    Write-Output ("source_check name={0} status={1} impact={2}" -f $Name, $status, $Impact)
}

if ([string]::IsNullOrWhiteSpace($EdgeCounterLogPath)) {
    $EdgeCounterLogPath = Find-DefaultEdgeCounterLog
}
if ([string]::IsNullOrWhiteSpace($EdgeCounterLogPath)) {
    throw "No edge-variant counter log found. Pass -EdgeCounterLogPath."
}
$edgeCounterLog = (Resolve-Path -LiteralPath (Resolve-RepoPath $EdgeCounterLogPath)).Path
$edgePayload = Get-LatestEdgeCounterPayload -Path $edgeCounterLog
$edgeCounters = Read-CounterTotals -Payload $edgePayload

$backendText = Read-RequiredText $BackendPath
$emitterText = Read-RequiredText $EmitterPath
$a64FunctionHeaderText = Read-RequiredText $A64FunctionHeaderPath
$codeCacheText = Read-RequiredText $CodeCachePath
$functionHeaderText = Read-RequiredText $FunctionHeaderPath

$edgeDesignText = Read-OptionalText $EdgeDesignAuditPath
$stateFollowupText = Read-OptionalText $StateCarrierFollowupPath
$roundtripText = Read-OptionalText $RoundtripAuditPath
$fpscrText = Read-OptionalText $FpscrDirtyCacheAuditPath

$normalEntrySingleton =
    (Test-Pattern $a64FunctionHeaderText 'std::atomic<uint8_t\*> machine_code_\{nullptr\}') -and
    (Test-Pattern $functionHeaderText 'virtual uint8_t\* machine_code\(\) const = 0;') -and
    (Test-Pattern $backendText 'guest_fn->machine_code\(\)')
$guestAddressOnlyIndirection =
    (Test-Pattern $emitterText 'code_cache_->has_indirection_table\(\).*?function->address\(\)') -and
    (Test-Pattern $codeCacheText 'void AddIndirection\(uint32_t guest_address,\s*uint32_t host_address\)')
$edgeCounterSourceText = $backendText + "`n" + $emitterText
$edgeCounterOnlyPatch =
    (Test-Pattern $edgeCounterSourceText 'blue_dragon_edge_variant_payload_materialized_count') -and
    (Test-Pattern $edgeCounterSourceText 'blue_dragon_edge_variant_variant_storage_missing_count') -and
    (Test-Pattern $edgeCounterSourceText 'blue_dragon_edge_variant_normal_entry_fallback_count')
$payloadScopeCounters =
    (Test-Pattern $edgeCounterSourceText 'blue_dragon_edge_variant_marker_set_count') -and
    (Test-Pattern $edgeCounterSourceText 'blue_dragon_edge_variant_active_f1_read_count') -and
    (Test-Pattern $edgeCounterSourceText 'blue_dragon_edge_variant_active_call_kill_count')
$payloadPcCounters =
    (Test-Pattern $edgeCounterSourceText 'blue_dragon_edge_variant_active_f1_read_site_count') -and
    (Test-Pattern $edgeCounterSourceText 'blue_dragon_edge_variant_active_call_kill_site_count') -and
    (Test-Pattern $edgeCounterSourceText 'active_f1_82287798') -and
    (Test-Pattern $edgeCounterSourceText 'active_kill_8228778c')
$directCallUsesNormalMachineCode =
    Test-Pattern $emitterText 'if \(fn->machine_code\(\)\).*?reinterpret_cast<uint64_t>\(fn->machine_code\(\)\).*?blr\(x9\);'
$designAuditRequiresVariant =
    Test-Pattern $edgeDesignText 'edge_variant_without_global_entrypoint=caller_local_or_side_table_required'

$eligibleCalls = Total $edgeCounters "eligible_calls"
$normalFallbacks = Total $edgeCounters "normal_entry_fallbacks"
$indirectionFallbacks = Total $edgeCounters "indirection_fallbacks"
$variantMisses = Total $edgeCounters "variant_misses"
$callKills = Total $edgeCounters "call_kills"
$payloadMaterializations = Total $edgeCounters "payload_materializations"
$storageMissing = Total $edgeCounters "variant_storage_missing"
$markerSets = Total-Optional $edgeCounters "marker_sets"
$markerClears = Total-Optional $edgeCounters "marker_clears"
$activeF1Reads = Total-Optional $edgeCounters "active_f1_reads"
$inactiveF1Reads = Total-Optional $edgeCounters "inactive_f1_reads"
$activeCallKills = Total-Optional $edgeCounters "active_call_kills"
$activeF1SiteNames = @(
    "active_f1_82287798",
    "active_f1_82287828",
    "active_f1_82287a1c",
    "active_f1_82287a2c",
    "active_f1_82287aa4",
    "active_f1_82287cf8",
    "active_f1_82287d10",
    "active_f1_82287d8c",
    "active_f1_82287ea8",
    "active_f1_82287f1c")
$activeKillSiteNames = @(
    "active_kill_8228778c",
    "active_kill_82287854",
    "active_kill_82287ed4",
    "active_kill_82287edc",
    "active_kill_82287ee4",
    "active_kill_82288220")

$f1TotalReads = Get-FirstValue $stateFollowupText 'f1 total_reads=([0-9]+)' "unknown"
$f1Fallbacks = Get-FirstValue $stateFollowupText 'f1 .*?fallback_total=([0-9]+)' "unknown"
$f1HelperShare = Get-FirstValue $stateFollowupText 'helper_share=([0-9.]+%)' "unknown"
$f1ChildShare = Get-FirstValue $stateFollowupText 'child_share=([0-9.]+%)' "unknown"
$fpscrTotalReads = Get-FirstValue $stateFollowupText 'fpscr total_reads=([0-9]+)' "unknown"
$fpscrDirtyWrites = Get-FirstValue $stateFollowupText 'fpscr_dirty_write=[0-9]+/([0-9]+)' "unknown"
$fpscrRequiredWritebacks = Get-FirstValue $stateFollowupText 'required_writebacks=([0-9]+)' "unknown"
$fpscrCallKills = Get-FirstValue $stateFollowupText 'call_kills=([0-9]+)' "unknown"

$f1ParentStorePc = Get-FirstValue $roundtripText 'f\[1\].*?PPC `([0-9A-Fa-f]+)`' "82282594"
$f1CalleeLoads = Get-FirstValue $roundtripText 'f\[1\]`?: `loads=([0-9]+)' "10"
$fpscrCalleeLoads = Get-FirstValue $roundtripText 'fpscr`?: `loads=([0-9]+)' "26"
$fpscrCalleeStores = Get-FirstValue $roundtripText 'fpscr`?: `loads=[0-9]+`, `stores=([0-9]+)' "26"
$fpscrTransitionLine = Get-FirstValue $fpscrText '(store_to_next_load_transitions=[^\r\n]+)' "-"
$fpscrCallFlushPcs = Get-FirstValue $fpscrText 'call_flush_pcs=([^\r\n]+)' "-"

Write-Output "# A64 Edge Variant Storage Plan"
Write-Output ""
Write-Output ("edge_counter_log={0}" -f $edgeCounterLog)
Write-Output ("edge_design_audit={0}" -f $(if (Resolve-OptionalPath $EdgeDesignAuditPath) { Resolve-OptionalPath $EdgeDesignAuditPath } else { "-" }))
Write-Output ("state_carrier_followup={0}" -f $(if (Resolve-OptionalPath $StateCarrierFollowupPath) { Resolve-OptionalPath $StateCarrierFollowupPath } else { "-" }))
Write-Output ("roundtrip_audit={0}" -f $(if (Resolve-OptionalPath $RoundtripAuditPath) { Resolve-OptionalPath $RoundtripAuditPath } else { "-" }))
Write-Output ("fpscr_dirty_cache_audit={0}" -f $(if (Resolve-OptionalPath $FpscrDirtyCacheAuditPath) { Resolve-OptionalPath $FpscrDirtyCacheAuditPath } else { "-" }))
Write-Output ("latest_edge_counter_row={0}" -f $edgePayload)
Write-Output ""

Write-Output "## Source Contract Checks"
Emit-SourceCheck "normal_entry_is_singleton" $normalEntrySingleton "do not replace 82287788 machine_code with an edge variant"
Emit-SourceCheck "indirection_key_is_guest_address_only" $guestAddressOnlyIndirection "do not change the global 82287788 indirection slot for one caller"
Emit-SourceCheck "direct_call_uses_normal_machine_code" $directCallUsesNormalMachineCode "a behavior patch needs explicit variant storage or caller-local dispatch"
Emit-SourceCheck "edge_probe_is_counter_only" $edgeCounterOnlyPatch "current cvar produced audit rows but did not change generated behavior"
Emit-SourceCheck "payload_scope_marker_is_counter_only" $payloadScopeCounters "marker tracks hot-edge lifetime but still does not materialize guest payload"
Emit-SourceCheck "payload_pc_attribution_is_counter_only" $payloadPcCounters "per-PC counters attribute active reads/kills without changing guest payload or entry behavior"
Emit-SourceCheck "design_audit_requires_variant_storage" $designAuditRequiresVariant "only caller-local or side-table storage fits the current contracts"
Write-Output ""

Write-Output "## Dynamic Edge Counters"
Write-Output ("edge parent=82282490 call_pc=82282598 callee=82287788 eligible_calls={0} delta_calls={1} normal_fallbacks={2} normal_fallback_share={3} indirection_fallbacks={4} variant_misses={5} payload_materializations={6} storage_missing={7} call_kills={8} call_kills_per_call={9}" -f `
    $eligibleCalls, (Delta $edgeCounters "eligible_calls"), $normalFallbacks,
    (Format-Percent $normalFallbacks $eligibleCalls), $indirectionFallbacks,
    $variantMisses, $payloadMaterializations, $storageMissing, $callKills,
    (Format-Ratio $callKills $eligibleCalls))
Write-Output ("payload_scope marker_sets={0} marker_clears={1} active_f1_reads={2} inactive_f1_reads={3} active_call_kills={4} active_f1_reads_per_call={5} active_call_kills_per_call={6}" -f `
    $markerSets, $markerClears, $activeF1Reads, $inactiveF1Reads,
    $activeCallKills, (Format-Ratio $activeF1Reads $eligibleCalls),
    (Format-Ratio $activeCallKills $eligibleCalls))
$f1Sites = foreach ($name in $activeF1SiteNames) {
    "{0}={1}" -f $name, (Total-Optional $edgeCounters $name)
}
$killSites = foreach ($name in $activeKillSiteNames) {
    "{0}={1}" -f $name, (Total-Optional $edgeCounters $name)
}
Write-Output ("payload_pc active_f1_sites={0}" -f ($f1Sites -join ","))
Write-Output ("payload_pc active_kill_sites={0}" -f ($killSites -join ","))
Write-Output ""

Write-Output "## Prior Payload Evidence"
Write-Output ("payload field=f[1] class=read_only_entry_seed parent_store_pc={0} callee_loads={1} dynamic_reads={2} helper_share={3} child_share={4} fallbacks={5} status=possible_but_not_a_standalone_speed_patch" -f `
    $f1ParentStorePc, $f1CalleeLoads, $f1TotalReads, $f1HelperShare,
    $f1ChildShare, $f1Fallbacks)
Write-Output ("payload field=fpscr class=dirty_cfg_carrier callee_loads={0} callee_stores={1} dynamic_reads={2} dirty_writes={3} required_writebacks={4} call_kills={5} transitions={6} call_flush_pcs={7} status=blocked_until_cfg_writeback_model" -f `
    $fpscrCalleeLoads, $fpscrCalleeStores, $fpscrTotalReads,
    $fpscrDirtyWrites, $fpscrRequiredWritebacks, $fpscrCallKills,
    $fpscrTransitionLine, $fpscrCallFlushPcs)
Write-Output ""

Write-Output "## Storage Options"
Write-Output "option name=caller_local_variant_patchpoint verdict=best_first reason=keeps normal 82287788 entry and global indirection untouched; compile can branch to edge code only from 82282490:82282598; unresolved and host entries remain normal-entry fallbacks"
Write-Output "option name=edge_side_table verdict=second reason=architecturally clean for multiple callers but a per-call lookup risks erasing a four-tick edge; acceptable only if lookup is compile-time or outside the hot call path"
Write-Output "option name=replace_82287788_machine_code verdict=rejected reason=A64Function and GuestFunction expose one normal machine_code slot used by host, resolve, direct call, tail call, and profiling ownership"
Write-Output "option name=global_indirection_variant verdict=rejected reason=the indirection key is just the callee guest address and cannot distinguish caller 82282490:82282598 from other callers"
Write-Output ""

Write-Output "## Required Rules Before Behavior"
Write-Output "rule normal_entry_fallback=required"
Write-Output "rule unresolved_call_fallback=required"
Write-Output "rule host_to_guest_entry_unchanged=required"
Write-Output "rule source_map_unwind_ownership=required_for_variant_code"
Write-Output "rule f1_visibility=parent_store_must_remain_or_variant_must_prove_equivalent_context_visibility"
Write-Output "rule fpscr_visibility=dirty_value_must_write_back_before calls, exits, exceptions, context barriers that expose PPCContext, and all external readers"
Write-Output "rule payload_kill=helper_or_child_call_must_kill_or_flush_payload unless separately proven preserving"
Write-Output ""

Write-Output "## Decision"
Write-Output "decision=no_generated_behavior_patch_yet"
Write-Output "reason=edge is hot and storage is missing; per-PC f[1] reads are real, but the current kill model is too broad for a read-only f[1] payload and fpscr still needs CFG-aware writeback proof"
Write-Output "safe_next_patch=f1_payload_kill_taxonomy_source_review_counter_only"
Write-Output "alternate_next_patch=broader_82282490_82287788_state_roundtrip_design"
Write-Output "do_not_run=quiet_speed_ab_or_payload_materialization_from_current_audit_patch"
