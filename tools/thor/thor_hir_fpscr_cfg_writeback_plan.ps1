param(
    [string]$LogPath = "scratch\thor-debug\20260524-041413-speed-logcat.txt",
    [string]$Function = "82287788",
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [string]$EdgeProfileLog = "scratch\thor-debug\20260524-030450-speed-logcat.txt",
    [string]$ParentFunction = "82282490",
    [string]$CalleeFunction = "82287788",
    [int]$Top = 40
)

$ErrorActionPreference = "Stop"

function Get-LineValue {
    param(
        [string[]]$Lines,
        [string]$Prefix
    )
    foreach ($line in $Lines) {
        if ($line.StartsWith($Prefix)) {
            return $line.Substring($Prefix.Length)
        }
    }
    return ""
}

function Get-LineByPrefix {
    param(
        [string[]]$Lines,
        [string]$Prefix
    )
    foreach ($line in $Lines) {
        if ($line.StartsWith($Prefix)) {
            return $line
        }
    }
    return ""
}

function Get-NamedInt {
    param(
        [string]$Text,
        [string]$Name
    )
    if ($Text -match ("(?:^|\s)" + [regex]::Escape($Name) + "=([0-9]+)")) {
        return [int64]$Matches[1]
    }
    return [int64]0
}

function Convert-CountList {
    param([string]$Text)
    $result = @{}
    if ([string]::IsNullOrWhiteSpace($Text) -or $Text.Trim() -eq "-") {
        return $result
    }
    foreach ($part in $Text.Split(",")) {
        $trimmed = $part.Trim()
        if ($trimmed -match "^([^:]+):([0-9]+)$") {
            $result[$Matches[1]] = [int64]$Matches[2]
        }
    }
    return $result
}

function Join-CountKeys {
    param([hashtable]$Table)
    if ($Table.Count -eq 0) {
        return "-"
    }
    return (($Table.GetEnumerator() |
        Sort-Object -Property @{ Expression = "Name"; Ascending = $true } |
        ForEach-Object { $_.Name }) -join ",")
}

function Get-CountValue {
    param(
        [hashtable]$Table,
        [string]$Key
    )
    if ($Table.ContainsKey($Key)) {
        return [int64]$Table[$Key]
    }
    return [int64]0
}

$dirtyTool = Join-Path $PSScriptRoot "thor_hir_fpscr_dirty_cache_audit.ps1"
if (!(Test-Path -LiteralPath $dirtyTool)) {
    throw "Required dirty-cache auditor not found: $dirtyTool"
}

$audit = @(& $dirtyTool `
    -LogPath $LogPath `
    -Function $Function `
    -Phase $Phase `
    -EdgeProfileLog $EdgeProfileLog `
    -ParentFunction $ParentFunction `
    -CalleeFunction $CalleeFunction `
    -Top $Top)

$summaryLine = Get-LineByPrefix $audit "loads="
$dynamicLine = Get-LineValue $audit "dynamic_context_access_upper "
$transitionLine = Get-LineByPrefix $audit "store_to_next_load_transitions="
$boundaryLine = Get-LineValue $audit "boundary_totals_between_store_and_next_load="
$callFlushLine = Get-LineValue $audit "call_flush_pcs="
$returnFlushLine = Get-LineValue $audit "return_flush_pcs="
$strictDynamicLine = Get-LineByPrefix $audit "strict_dynamic_upper="
$edgeLine = Get-LineByPrefix $audit "edge_parent="

$loads = Get-NamedInt $summaryLine "loads"
$stores = Get-NamedInt $summaryLine "stores"
$pairedUpdates = Get-NamedInt $summaryLine "paired_load_store_updates"
$dynamicLoads = Get-NamedInt $dynamicLine "loads"
$dynamicStores = Get-NamedInt $dynamicLine "stores"
$dynamicTotal = Get-NamedInt $dynamicLine "total"
$transitions = Get-NamedInt $transitionLine "store_to_next_load_transitions"
$strictTransitions = Get-NamedInt $transitionLine "strict_same_window"
$cfgTransitions = Get-NamedInt $transitionLine "cfg_phi_or_stack_carrier_required"
$externalTransitions = Get-NamedInt $transitionLine "external_visibility_flush_required"
$exitTransitions = Get-NamedInt $transitionLine "exit_flush_required"
$strictDynamicUpper = Get-NamedInt $strictDynamicLine "strict_dynamic_upper"
$cfgDynamicUpper = Get-NamedInt $strictDynamicLine "cfg_dynamic_upper"
$edgeCalls = Get-NamedInt $edgeLine "calls_total"
$edgeBodyTicks = Get-NamedInt $edgeLine "body_ticks_total"
$edgeTicksPerCall = Get-NamedInt $edgeLine "ticks_per_call"

$boundaryCounts = Convert-CountList $boundaryLine
$callFlushPcs = Convert-CountList $callFlushLine
$returnFlushPcs = Convert-CountList $returnFlushLine

$transitionRows = New-Object System.Collections.Generic.List[object]
foreach ($line in $audit) {
    if ($line -match "^store_pc=([0-9A-Fa-f]+) next_load_pc=([0-9A-Fa-f-]+) class=([^ ]+) boundaries=([^ ]+) call_pcs=(.*)$") {
        $transitionRows.Add([pscustomobject][ordered]@{
            store_pc = $Matches[1].ToUpperInvariant()
            next_load_pc = $Matches[2].ToUpperInvariant()
            class = $Matches[3]
            boundaries = $Matches[4]
            call_pcs = $Matches[5]
        }) | Out-Null
    }
}

$cfgBoundaryKinds = @()
foreach ($kind in @("branch", "label", "context_barrier")) {
    if (Get-CountValue $boundaryCounts $kind) {
        $cfgBoundaryKinds += $kind
    }
}
$externalBoundaryKinds = @()
foreach ($kind in @("call", "return")) {
    if (Get-CountValue $boundaryCounts $kind) {
        $externalBoundaryKinds += $kind
    }
}
$cfgBoundaryText = if ($cfgBoundaryKinds.Count -gt 0) { $cfgBoundaryKinds -join "," } else { "-" }
$externalBoundaryText = if ($externalBoundaryKinds.Count -gt 0) { $externalBoundaryKinds -join "," } else { "-" }

$behaviorAllowed = ($strictTransitions -gt 0 -and $cfgTransitions -eq 0 -and $externalTransitions -eq 0 -and $exitTransitions -eq 0)
$minimumCallWritebackSites = $callFlushPcs.Count
$minimumReturnWritebackSites = $returnFlushPcs.Count

Write-Output "# HIR FPSCR CFG Writeback Plan"
Write-Output ""
Write-Output ("log={0}" -f (Get-LineValue $audit "log="))
Write-Output ("function={0} phase={1}" -f $Function.ToUpperInvariant(), $Phase)
Write-Output ("parent_edge={0}->{1} calls_total={2} body_ticks_total={3} ticks_per_call={4}" -f
    $ParentFunction.ToUpperInvariant(), $CalleeFunction.ToUpperInvariant(), $edgeCalls, $edgeBodyTicks, $edgeTicksPerCall)
Write-Output ""
Write-Output "## Source Dirty-Cache Evidence"
Write-Output ("fpscr_static_loads={0} fpscr_static_stores={1} paired_updates={2}" -f $loads, $stores, $pairedUpdates)
Write-Output ("dynamic_context_access_upper loads={0} stores={1} total={2}" -f $dynamicLoads, $dynamicStores, $dynamicTotal)
Write-Output ("transitions={0} strict_same_window={1} cfg_carrier_required={2} external_visibility_required={3} exit_flush_required={4}" -f
    $transitions, $strictTransitions, $cfgTransitions, $externalTransitions, $exitTransitions)
Write-Output ("boundary_totals branch={0} label={1} context_barrier={2} call={3} return={4}" -f
    (Get-CountValue $boundaryCounts "branch"),
    (Get-CountValue $boundaryCounts "label"),
    (Get-CountValue $boundaryCounts "context_barrier"),
    (Get-CountValue $boundaryCounts "call"),
    (Get-CountValue $boundaryCounts "return"))
Write-Output ("dynamic_upper strict={0} cfg={1}" -f $strictDynamicUpper, $cfgDynamicUpper)
Write-Output ""
Write-Output "## Required Writeback Sites"
Write-Output ("minimum_call_writeback_sites={0}" -f $minimumCallWritebackSites)
foreach ($item in ($callFlushPcs.GetEnumerator() | Sort-Object -Property Name)) {
    Write-Output ("call_writeback_pc={0} transitions={1}" -f $item.Name, $item.Value)
}
Write-Output ("minimum_return_writeback_sites={0}" -f $minimumReturnWritebackSites)
foreach ($item in ($returnFlushPcs.GetEnumerator() | Sort-Object -Property Name)) {
    Write-Output ("return_writeback_pc={0} transitions={1}" -f $item.Name, $item.Value)
}
Write-Output ""
Write-Output "## Counter-Only Patch Contract"
Write-Output "behavior_patch_allowed=false"
Write-Output ("normal_entry_behavior=unchanged")
Write-Output ("payload_materializations_allowed=0")
Write-Output ("required_runtime_cvar=arm64_blue_dragon_fpscr_cfg_writeback_audit")
Write-Output ("counter fpscr_load_static_sites={0}" -f $loads)
Write-Output ("counter fpscr_store_static_sites={0}" -f $stores)
Write-Output ("counter fpscr_cfg_transition_sites={0}" -f $cfgTransitions)
Write-Output ("counter fpscr_external_visibility_transition_sites={0}" -f $externalTransitions)
Write-Output ("counter fpscr_required_call_writeback_sites={0}" -f $minimumCallWritebackSites)
Write-Output ("counter fpscr_cfg_boundary_kinds={0}" -f $cfgBoundaryText)
Write-Output ("counter fpscr_external_boundary_kinds={0}" -f $externalBoundaryText)
Write-Output ""
Write-Output "## Transition Plan"
foreach ($row in $transitionRows) {
    $action = "carry_dirty_value"
    if ($row.class -eq "external_visibility_flush_required") {
        $action = "writeback_before_call_or_external_reader"
    } elseif ($row.class -eq "exit_flush_required") {
        $action = "writeback_before_exit"
    } elseif ($row.class -eq "strict_same_window") {
        $action = "local_forwarding_possible"
    }
    Write-Output ("store_pc={0} next_load_pc={1} class={2} action={3} boundaries={4} call_pcs={5}" -f
        $row.store_pc, $row.next_load_pc, $row.class, $action, $row.boundaries, $row.call_pcs)
}
Write-Output ""
Write-Output "## Decision"
if ($behaviorAllowed) {
    Write-Output "decision=strict_local_probe_possible_but_not_selected"
    Write-Output "reason=all transitions are strict same-window; a local probe could be considered after route proof."
} else {
    Write-Output "decision=no_behavior_patch_yet"
    Write-Output ("reason=fpscr has {0} strict same-window transitions, {1} CFG-carrier transitions, {2} external-visibility transitions, and needs exact dirty writeback at {3} call PCs before behavior can change." -f
        $strictTransitions, $cfgTransitions, $externalTransitions, $minimumCallWritebackSites)
}
Write-Output "next_patch=default-off counter-only CFG writeback skeleton for 82287788 fpscr, or return to caller-local edge-variant storage; do not quiet-A/B until counters prove nonzero materializable coverage with all writebacks accounted."
