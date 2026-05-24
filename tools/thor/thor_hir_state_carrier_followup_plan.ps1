param(
    [Parameter(Mandatory = $true)]
    [string]$CounterLogPath,
    [string]$StaticAuditPath = "",
    [string]$PairEntryRoiPath = "",
    [string]$F1FastpathAbPath = "",
    [string]$FpscrDirtyCacheAuditPath = ""
)

$ErrorActionPreference = "Stop"

function Resolve-OptionalPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }
    if (!(Test-Path -LiteralPath $Path)) {
        throw "Path not found: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
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

function Get-FirstCapture {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Default = "-"
    )

    if ([string]::IsNullOrWhiteSpace($Path) -or !(Test-Path -LiteralPath $Path)) {
        return $Default
    }
    $text = Get-Content -Raw -LiteralPath $Path
    $match = [regex]::Match(
        $text,
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

function Get-LatestCounterPayload {
    param([string]$Path)

    if (!(Test-Path -LiteralPath $Path)) {
        throw "CounterLogPath not found: $Path"
    }
    $payload = $null
    foreach ($line in Get-Content -LiteralPath $Path) {
        $index = $line.IndexOf("A64 Blue Dragon state-carrier design audit:")
        if ($index -lt 0) {
            continue
        }
        $payload = $line.Substring(
            $index + "A64 Blue Dragon state-carrier design audit:".Length).Trim()
    }
    if ([string]::IsNullOrWhiteSpace($payload)) {
        throw "No A64 Blue Dragon state-carrier design audit row found in $Path."
    }
    return $payload
}

function Read-CounterTotals {
    param([string]$Payload)

    $counters = @{}
    $counterPattern =
        '(?<name>f1_seed|f1_read|f1_helper_read|f1_child_read|f1_child_call|f1_fallback|fpscr_seed|fpscr_read|fpscr_dirty_write|fpscr_required_writeback|fpscr_call_kill|fpscr_fallback)=(?<delta>[0-9]+)/(?<total>[0-9]+)'
    foreach ($match in [regex]::Matches($Payload, $counterPattern)) {
        $name = $match.Groups["name"].Value
        $counters[$name] = [pscustomobject][ordered]@{
            delta = [int64]$match.Groups["delta"].Value
            total = [int64]$match.Groups["total"].Value
        }
    }

    foreach ($required in @(
            "f1_seed",
            "f1_read",
            "f1_helper_read",
            "f1_child_read",
            "f1_child_call",
            "f1_fallback",
            "fpscr_read",
            "fpscr_dirty_write",
            "fpscr_required_writeback",
            "fpscr_call_kill",
            "fpscr_fallback")) {
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

$counterLog = Resolve-OptionalPath $CounterLogPath
$staticAudit = Resolve-OptionalPath $StaticAuditPath
$pairEntryRoi = Resolve-OptionalPath $PairEntryRoiPath
$f1Ab = Resolve-OptionalPath $F1FastpathAbPath
$fpscrAudit = Resolve-OptionalPath $FpscrDirtyCacheAuditPath

$payload = Get-LatestCounterPayload -Path $counterLog
$counters = Read-CounterTotals -Payload $payload

$f1Read = Total $counters "f1_read"
$f1Helper = Total $counters "f1_helper_read"
$f1Child = Total $counters "f1_child_read"
$f1ChildCalls = Total $counters "f1_child_call"
$f1Fallback = Total $counters "f1_fallback"
$f1Seed = Total $counters "f1_seed"

$fpscrRead = Total $counters "fpscr_read"
$fpscrDirty = Total $counters "fpscr_dirty_write"
$fpscrRequiredWriteback = Total $counters "fpscr_required_writeback"
$fpscrCallKill = Total $counters "fpscr_call_kill"
$fpscrFallback = Total $counters "fpscr_fallback"

$pairIncrementalUpper = Get-FirstCapture `
    -Path $pairEntryRoi `
    -Pattern 'pair_incremental_static_upper=([0-9]+)'
$existingSeedLoads = Get-FirstCapture `
    -Path $pairEntryRoi `
    -Pattern 'existing_seed_context_loads=([0-9]+)'
$existingReuseLoads = Get-FirstCapture `
    -Path $pairEntryRoi `
    -Pattern 'existing_reuse_loads=([0-9]+)'
$fpscrStrictWindow = Get-FirstCapture `
    -Path $fpscrAudit `
    -Pattern 'strict_same_window=([0-9]+)'
$fpscrCfgRequired = Get-FirstCapture `
    -Path $fpscrAudit `
    -Pattern 'cfg_phi_or_stack_carrier_required=([0-9]+)'
$fpscrExternalFlush = Get-FirstCapture `
    -Path $fpscrAudit `
    -Pattern 'external_visibility_flush_required=([0-9]+)'
$f1PreviousDecision = "not_parsed"
if (![string]::IsNullOrWhiteSpace($f1Ab) -and (Test-Path -LiteralPath $f1Ab)) {
    $f1AbText = Get-Content -Raw -LiteralPath $f1Ab
    if ($f1AbText -match 'not\s+a\s+speed\s+win') {
        $f1PreviousDecision = "no_speed_win"
    }
}

Write-Output "# HIR State-Carrier Follow-up Plan"
Write-Output ""
Write-Output ("counter_log={0}" -f $counterLog)
Write-Output ("static_audit={0}" -f $(if ($staticAudit) { $staticAudit } else { "-" }))
Write-Output ("pair_entry_roi={0}" -f $(if ($pairEntryRoi) { $pairEntryRoi } else { "-" }))
Write-Output ("f1_fastpath_ab={0}" -f $(if ($f1Ab) { $f1Ab } else { "-" }))
Write-Output ("fpscr_dirty_cache_audit={0}" -f $(if ($fpscrAudit) { $fpscrAudit } else { "-" }))
Write-Output ("latest_counter_row={0}" -f $payload)
Write-Output ""

Write-Output "## Dynamic Counter Summary"
Write-Output ("f1 total_reads={0} delta_reads={1} helper_reads={2} helper_share={3} child_reads={4} child_share={5} child_calls={6} reads_per_child_call={7} reads_per_seed={8} fallback_total={9}" -f `
    $f1Read, (Delta $counters "f1_read"), $f1Helper, (Format-Percent $f1Helper $f1Read), `
    $f1Child, (Format-Percent $f1Child $f1Read), $f1ChildCalls, `
    (Format-Ratio $f1Child $f1ChildCalls), (Format-Ratio $f1Read $f1Seed), $f1Fallback)
Write-Output ("fpscr total_reads={0} delta_reads={1} dirty_writes={2} dirty_per_read={3} required_writebacks={4} writeback_share={5} call_kills={6} call_kill_share={7} fallback_total={8}" -f `
    $fpscrRead, (Delta $counters "fpscr_read"), $fpscrDirty, (Format-Percent $fpscrDirty $fpscrRead), `
    $fpscrRequiredWriteback, (Format-Percent $fpscrRequiredWriteback $fpscrDirty), `
    $fpscrCallKill, (Format-Percent $fpscrCallKill $fpscrDirty), $fpscrFallback)
Write-Output ""

Write-Output "## Candidate Classification"
Write-Output ("candidate field=f[1] class=read_only_parent_callee_candidate dynamic_reads={0} helper_share={1} child_share={2} fallbacks={3} previous_stack_slot_ab={4} existing_seed_context_loads={5} existing_reuse_loads={6} pair_incremental_static_upper={7} decision=design_only_broader_parent_callee_carrier reason=volume_is_real_but_narrow_stack_slot_and_pair_seed_are_too_small" -f `
    $f1Read, (Format-Percent $f1Helper $f1Read), (Format-Percent $f1Child $f1Read), `
    $f1Fallback, $f1PreviousDecision, $existingSeedLoads, $existingReuseLoads, $pairIncrementalUpper)
Write-Output ("candidate field=fpscr class=dirty_cfg_carrier dynamic_reads={0} dirty_writes={1} required_writebacks={2} writeback_share={3} call_kills={4} call_kill_share={5} strict_same_window={6} cfg_required={7} external_flush_required={8} fallbacks={9} decision=audit_only_no_behavior_patch reason=large_raw_volume_is_outweighed_by_mutable_dirty_visibility_and_call_kill_pressure" -f `
    $fpscrRead, $fpscrDirty, $fpscrRequiredWriteback, (Format-Percent $fpscrRequiredWriteback $fpscrDirty), `
    $fpscrCallKill, (Format-Percent $fpscrCallKill $fpscrDirty), $fpscrStrictWindow, `
    $fpscrCfgRequired, $fpscrExternalFlush, $fpscrFallback)
Write-Output ""

Write-Output "## Decision"
Write-Output "decision=no_generated_behavior_patch_yet"
Write-Output "blocked_experiments=standalone_f1_stack_slot,pair_entry_seed_only,fpscr_shortcut,stvewx_lane_fold,mul_add_three_pc_repeat"
Write-Output "next_experiment=design a broader parent/callee 82282490->82287788 carrier plan that either combines multiple safe state lanes or proves exact CFG writeback rules; add compile-time/runtime audit counters before a quiet speed A/B."
Write-Output "thor_policy=no Thor run needed for this offline planner; next runtime behavior change still needs Thor build/deploy/capture proof."
