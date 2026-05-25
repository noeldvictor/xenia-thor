param(
    [string]$EdgeCounterLogPath = "",
    [string]$StoragePlanScriptPath = "tools/thor/thor_a64_edge_variant_storage_plan.ps1",
    [string]$StateCarrierDesignPath = "docs/research/20260524-133027-8228252c-state-carrier-design-audit.md",
    [string]$FpscrCfgPlanPath = "docs/research/20260524-184918-82287788-fpscr-cfg-writeback-plan.md",
    [string]$EmitterPath = "src/xenia/cpu/backend/a64/a64_emitter.cc",
    [string]$SequencesPath = "src/xenia/cpu/backend/a64/a64_sequences.cc",
    [string]$BackendHeaderPath = "src/xenia/cpu/backend/a64/a64_backend.h",
    [string]$A64FunctionHeaderPath = "src/xenia/cpu/backend/a64/a64_function.h",
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

function Resolve-ExistingPath {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if ([string]::IsNullOrWhiteSpace($resolved) -or
        !(Test-Path -LiteralPath $resolved)) {
        throw "Path not found: $Path"
    }
    return (Resolve-Path -LiteralPath $resolved).Path
}

function Read-RequiredText {
    param([string]$Path)

    return Get-Content -Raw -LiteralPath (Resolve-ExistingPath $Path)
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

function Find-DefaultEdgeCounterLog {
    $scratch = Join-Path $RepoRoot "scratch\thor-debug"
    if (!(Test-Path -LiteralPath $scratch)) {
        return ""
    }

    $logs = Get-ChildItem -LiteralPath $scratch -Recurse `
        -Filter "*-speed-logcat.txt" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending
    foreach ($log in $logs) {
        if (Select-String -LiteralPath $log.FullName `
                -Pattern "A64 Blue Dragon edge-variant audit:" -Quiet) {
            return $log.FullName
        }
    }
    return ""
}

function Get-FirstOutputLine {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )

    foreach ($line in $Lines) {
        if ($line -match $Pattern) {
            return $line
        }
    }
    return ""
}

function Get-NamedValue {
    param(
        [string]$Text,
        [string]$Name,
        [string]$Default = "-"
    )

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return $Default
    }
    $escaped = [regex]::Escape($Name)
    $match = [regex]::Match($Text, "(?:^|\s|,)$escaped=(?<value>[^\s,]+)")
    if (!$match.Success) {
        return $Default
    }
    return $match.Groups["value"].Value
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
        [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if (!$match.Success -or $match.Groups.Count -le 1) {
        return $Default
    }
    return $match.Groups[1].Value
}

function Emit-SourceCheck {
    param(
        [string]$Name,
        [bool]$Ok,
        [string]$Impact
    )

    $status = if ($Ok) { "ok" } else { "missing" }
    Write-Output ("source_check name={0} status={1} impact={2}" -f `
            $Name, $status, $Impact)
}

if ([string]::IsNullOrWhiteSpace($EdgeCounterLogPath)) {
    $EdgeCounterLogPath = Find-DefaultEdgeCounterLog
}
if ([string]::IsNullOrWhiteSpace($EdgeCounterLogPath)) {
    throw "No edge-variant counter log found. Pass -EdgeCounterLogPath."
}

$edgeCounterLog = Resolve-ExistingPath $EdgeCounterLogPath
$storagePlanScript = Resolve-ExistingPath $StoragePlanScriptPath
$storagePlanOutput = @(& $storagePlanScript -EdgeCounterLogPath $edgeCounterLog)
$storagePlanText = $storagePlanOutput -join "`n"

$edgeLine = Get-FirstOutputLine $storagePlanOutput '^edge parent='
$payloadScopeLine = Get-FirstOutputLine $storagePlanOutput '^payload_scope '
$payloadTaxonomyLine = Get-FirstOutputLine $storagePlanOutput '^payload_f1_taxonomy '
$activeF1SitesLine = Get-FirstOutputLine $storagePlanOutput '^payload_pc active_f1_sites='
$activeKillSitesLine = Get-FirstOutputLine $storagePlanOutput '^payload_pc active_kill_sites='
$f1EvidenceLine = Get-FirstOutputLine $storagePlanOutput '^payload field=f\[1\] '
$fpscrEvidenceLine = Get-FirstOutputLine $storagePlanOutput '^payload field=fpscr '

$emitterText = Read-RequiredText $EmitterPath
$sequencesText = Read-RequiredText $SequencesPath
$backendHeaderText = Read-RequiredText $BackendHeaderPath
$a64FunctionHeaderText = Read-RequiredText $A64FunctionHeaderPath
$functionHeaderText = Read-RequiredText $FunctionHeaderPath
$stateCarrierText = Read-RequiredText $StateCarrierDesignPath
$fpscrCfgText = Read-RequiredText $FpscrCfgPlanPath

$joinedSource = $emitterText + "`n" + $sequencesText + "`n" + $backendHeaderText
$normalEntrySingleton =
    (Test-Pattern $a64FunctionHeaderText 'std::atomic<uint8_t\*> machine_code_\{nullptr\}') -and
    (Test-Pattern $functionHeaderText 'virtual uint8_t\* machine_code\(\) const = 0;')
$hotEdgeCallSeam =
    Test-Pattern $emitterText 'blue_dragon_edge_variant_hot_edge.*?0x82282490.*?0x82282598.*?0x82287788'
$normalFallbackOnly =
    (Test-Pattern $emitterText 'blue_dragon_edge_variant_normal_entry_fallback_count') -and
    !(Test-Pattern $emitterText 'blue_dragon_edge_variant_payload_materialized_count\(\)')
$f1ScopeCounters =
    (Test-Pattern $sequencesText 'EmitBlueDragonEdgeVariantF1ReadAudit') -and
    (Test-Pattern $emitterText 'blue_dragon_edge_variant_active_f1_unknown_call_kill_count')
$fpscrCfgLane =
    (Test-Pattern $joinedSource 'arm64_blue_dragon_fpscr_cfg_writeback_audit') -and
    (Test-Pattern $joinedSource 'blue_dragon_fpscr_cfg_call_writeback_82287edc_count')
$edgePayloadStorageSkeleton =
    Test-Pattern $joinedSource 'arm64_blue_dragon_edge_payload_storage_audit'

$eligibleCalls = Get-NamedValue $edgeLine "eligible_calls"
$normalFallbackShare = Get-NamedValue $edgeLine "normal_fallback_share"
$payloadMaterializations = Get-NamedValue $edgeLine "payload_materializations"
$storageMissing = Get-NamedValue $edgeLine "storage_missing"
$activeF1Reads = Get-NamedValue $payloadScopeLine "active_f1_reads"
$activeF1ReadsPerCall = Get-NamedValue $payloadScopeLine "active_f1_reads_per_call"
$activeCallKills = Get-NamedValue $payloadScopeLine "active_call_kills"
$unknownKills = Get-NamedValue $payloadTaxonomyLine "unknown_call_kills"
$unknownKillsPerCall = Get-NamedValue $payloadTaxonomyLine "unknown_kills_per_call"
$nonblockingCalls = Get-NamedValue $payloadTaxonomyLine "nonblocking_calls"

$f1ParentStore = Get-NamedValue $f1EvidenceLine "parent_store_pc"
$f1CalleeLoads = Get-NamedValue $f1EvidenceLine "callee_loads"
$f1DynamicReads = Get-NamedValue $f1EvidenceLine "dynamic_reads"
$f1Fallbacks = Get-NamedValue $f1EvidenceLine "fallbacks"

$fpscrCalleeLoads = Get-NamedValue $fpscrEvidenceLine "callee_loads"
$fpscrCalleeStores = Get-NamedValue $fpscrEvidenceLine "callee_stores"
$fpscrDynamicReads = Get-NamedValue $fpscrEvidenceLine "dynamic_reads"
$fpscrDirtyWrites = Get-NamedValue $fpscrEvidenceLine "dirty_writes"
$fpscrRequiredWritebacks = Get-NamedValue $fpscrEvidenceLine "required_writebacks"

$r3ParentSeed = Get-FirstValue $stateCarrierText 'r\[3\]`: .*?Parent seed is at `([0-9A-Fa-f]+)`' "82282550"
$r3CalleeLoads = Get-FirstValue $stateCarrierText 'r\[3\]`: .*?Callee has `loads=([0-9]+)`' "12"
$r3CalleeStores = Get-FirstValue $stateCarrierText 'r\[3\]`: .*?`stores=([0-9]+)`' "15"
$r3LoadUpper = Get-FirstValue $stateCarrierText 'r\[3\]`: .*?dynamic upper `([0-9]+)` loads' "20295264"
$r3StoreUpper = Get-FirstValue $stateCarrierText 'r\[3\]`: .*?and `([0-9]+)` stores' "25369080"
$fpscrRequiredCallPcs = Get-FirstValue $fpscrCfgText 'required call writebacks at ([^\r\n.]+)' "82287ED4, 82287EDC, 82287EE4, and 82288220"

Write-Output "# A64 Edge Payload Storage Design"
Write-Output ""
Write-Output ("edge_counter_log={0}" -f $edgeCounterLog)
Write-Output ("storage_plan_script={0}" -f $storagePlanScript)
Write-Output ("state_carrier_design={0}" -f (Resolve-ExistingPath $StateCarrierDesignPath))
Write-Output ("fpscr_cfg_plan={0}" -f (Resolve-ExistingPath $FpscrCfgPlanPath))
Write-Output ""

Write-Output "## Source Checks"
Emit-SourceCheck "normal_entry_is_singleton" $normalEntrySingleton "variant must not replace A64Function::machine_code or GuestFunction::machine_code"
Emit-SourceCheck "hot_edge_call_seam_exists" $hotEdgeCallSeam "counter-only skeleton can live in A64Emitter::Call at exact edge 82282490:82282598->82287788"
Emit-SourceCheck "current_edge_path_is_normal_fallback_only" $normalFallbackOnly "latest audit still records payload_materializations=0 and no behavior-changing payload path"
Emit-SourceCheck "f1_scope_and_taxonomy_counters_exist" $f1ScopeCounters "f[1] route scope is measured, including zero unknown call kills"
Emit-SourceCheck "fpscr_cfg_writeback_lane_exists" $fpscrCfgLane "fpscr remains separate from the read-only f[1] seed until writebacks are exact"
Emit-SourceCheck "edge_payload_storage_audit_skeleton_exists" $edgePayloadStorageSkeleton "expected missing now; next code slice should add this default-off counter-only skeleton"
Write-Output ""

Write-Output "## Latest Edge Snapshot"
Write-Output ("edge parent=82282490 call_pc=82282598 callee=82287788 eligible_calls={0} normal_fallback_share={1} payload_materializations={2} storage_missing={3}" -f `
    $eligibleCalls, $normalFallbackShare, $payloadMaterializations, $storageMissing)
Write-Output ("f1_scope active_reads={0} active_reads_per_call={1} active_call_kills={2} nonblocking_calls={3} unknown_call_kills={4} unknown_kills_per_call={5}" -f `
    $activeF1Reads, $activeF1ReadsPerCall, $activeCallKills,
    $nonblockingCalls, $unknownKills, $unknownKillsPerCall)
Write-Output $activeF1SitesLine
Write-Output $activeKillSitesLine
Write-Output ""

Write-Output "## Payload Bundle Cut"
Write-Output ("payload field=f[1] role=route_proven_read_only_seed parent_store_pc={0} callee_loads={1} dynamic_reads={2} fallbacks={3} verdict=include_in_counter_skeleton_not_standalone_behavior" -f `
    $f1ParentStore, $f1CalleeLoads, $f1DynamicReads, $f1Fallbacks)
Write-Output ("payload field=fpscr role=dirty_cfg_carrier callee_loads={0} callee_stores={1} dynamic_reads={2} dirty_writes={3} required_writebacks={4} required_call_pcs={5} verdict=separate_cfg_writeback_lane_before_behavior" -f `
    $fpscrCalleeLoads, $fpscrCalleeStores, $fpscrDynamicReads,
    $fpscrDirtyWrites, $fpscrRequiredWritebacks, $fpscrRequiredCallPcs)
Write-Output ("payload field=r[3] role=mutable_state_cache parent_seed_pc={0} callee_loads={1} callee_stores={2} dynamic_load_upper={3} dynamic_store_upper={4} verdict=count_only_later_after_f1_fpscr_shape" -f `
    $r3ParentSeed, $r3CalleeLoads, $r3CalleeStores, $r3LoadUpper, $r3StoreUpper)
Write-Output "payload field=lr role=call_link_state verdict=blocked_keep_normal_ppc_call_return_path"
Write-Output ""

Write-Output "## Storage Shape"
Write-Output "storage_shape name=caller_local_patchpoint verdict=preferred reason=zero hot lookup cost and no normal-entry or indirection change"
Write-Output "storage_shape name=edge_side_table verdict=second reason=cleaner multi-caller ownership but only acceptable if lookup is compile-time or outside the four-tick hot call path"
Write-Output "storage_shape name=normal_entry_replacement verdict=rejected reason=breaks host, resolved, direct, tail, source-map, and profiling ownership"
Write-Output "storage_shape name=global_indirection_slot verdict=rejected reason=indirection key is callee guest address and cannot distinguish 82282490:82282598"
Write-Output ""

Write-Output "## Counter-Only Patch Contract"
Write-Output "next_cvar=arm64_blue_dragon_edge_payload_storage_audit default=false"
Write-Output "payload_materializations_allowed=0"
Write-Output "behavior_changed=0"
Write-Output "normal_entry_fallback=required"
Write-Output "compile_counters=eligible_edge_compiles,variant_codegen_skipped,storage_missing,normal_entry_owned"
Write-Output "runtime_counters=eligible_edge_calls,normal_entry_fallbacks,indirection_fallbacks,variant_misses,payload_materializations"
Write-Output "payload_counters=f1_seed_candidates,f1_active_reads_covered,f1_unknown_kills,fpscr_seed_candidates,fpscr_dirty_writes,fpscr_required_writebacks,r3_seed_candidates,r3_mutable_writes"
Write-Output "flush_counters=helper_preserved,child_preserved,return_exit,unknown_call,context_barrier,exception_or_trap,external_visibility"
Write-Output "source_files=src/xenia/cpu/backend/a64/a64_backend.h,src/xenia/cpu/backend/a64/a64_backend.cc,src/xenia/cpu/backend/a64/a64_emitter.cc,src/xenia/cpu/backend/a64/a64_sequences.cc"
Write-Output "validation=PowerShell_parser,NativeCore,FullDeploy,route_clean_Thor_capture_before_any_speed_AB"
Write-Output ""

Write-Output "## Decision"
Write-Output "decision=no_generated_behavior_patch_yet"
Write-Output "reason=f1 taxonomy is clean but f1-only already missed speed; real upside requires a measured bundle shape while fpscr stays CFG-writeback gated and r3 stays count-only"
Write-Output "safe_next_patch=default_off_counter_only_arm64_blue_dragon_edge_payload_storage_audit"
Write-Output "do_not_run=quiet_speed_AB_or_payload_materialization_until_route_clean_counter_rows_exist"
