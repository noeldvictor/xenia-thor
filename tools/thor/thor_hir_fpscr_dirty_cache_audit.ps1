param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [string]$EdgeProfileLog = "",
    [string]$ParentFunction = "",
    [string]$CalleeFunction = "",
    [int]$Top = 16
)

$ErrorActionPreference = "Stop"

function Add-Count {
    param(
        [hashtable]$Table,
        [string]$Key,
        [int64]$Amount = 1
    )
    if ([string]::IsNullOrWhiteSpace($Key)) {
        return
    }
    if ($Table.ContainsKey($Key)) {
        $Table[$Key] += $Amount
    } else {
        $Table[$Key] = $Amount
    }
}

function Get-TopPairs {
    param(
        [hashtable]$Table,
        [int]$Limit = 12
    )
    if ($Table.Count -eq 0) {
        return "-"
    }
    return (($Table.GetEnumerator() |
        Sort-Object -Property @{ Expression = "Value"; Descending = $true },
                              @{ Expression = "Name"; Ascending = $true } |
        Select-Object -First $Limit |
        ForEach-Object { "{0}:{1}" -f $_.Name, $_.Value }) -join ",")
}

function Parse-ContextOffset {
    param(
        [string]$Text,
        [string]$Op
    )
    if ($Op -eq "store_context" -and $Text -match '^store_context\s+\+([0-9]+),') {
        return [int]$Matches[1]
    }
    if ($Op -eq "load_context" -and $Text -match '=\s*load_context\s+\+([0-9]+)\b') {
        return [int]$Matches[1]
    }
    return $null
}

function New-HirRow {
    param(
        [int]$Ordinal,
        [string]$Kind,
        [string]$Text,
        [string]$PpcAddress,
        [string]$PpcOp
    )

    $op = ""
    if ($Kind -eq "ppc") {
        $op = $PpcOp
    } elseif ($Text -match '^\S+\s*=\s*(?<op>[A-Za-z0-9_\.]+)\b') {
        $op = $Matches.op.ToLowerInvariant()
    } elseif ($Text -match '^(?<op>[A-Za-z0-9_\.]+)\b') {
        $op = $Matches.op.ToLowerInvariant()
    }
    $offset = Parse-ContextOffset $Text $op

    [pscustomobject][ordered]@{
        ordinal = $Ordinal
        kind = $Kind
        text = $Text
        ppc_address = $PpcAddress
        ppc_op = $PpcOp
        op = $op
        context_offset = $offset
    }
}

function Read-HirFunction {
    param(
        [string]$Path,
        [string]$Function,
        [string]$Phase
    )

    if (!(Test-Path -LiteralPath $Path)) {
        throw "LogPath not found: $Path"
    }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $functionUpper = $Function.ToUpperInvariant()
    $needle = "Filtered function dump $functionUpper ${Phase}:"
    $rows = New-Object System.Collections.Generic.List[object]
    $currentPpc = ""
    $currentOp = ""
    $ordinal = 0

    foreach ($line in Get-Content -LiteralPath $resolved) {
        $needleIndex = $line.IndexOf($needle)
        if ($needleIndex -lt 0) {
            continue
        }
        $text = $line.Substring($needleIndex + $needle.Length).Trim()
        if ([string]::IsNullOrWhiteSpace($text) -or $text -match '^<entry>:') {
            continue
        }
        if ($text -match '^;\s*(in:|out:|default|$)') {
            continue
        }
        if ($text -match '^(loc_[0-9A-Fa-f]+|_label[0-9A-Fa-f]+):') {
            $rows.Add((New-HirRow $ordinal "label" $text $currentPpc $currentOp)) | Out-Null
            ++$ordinal
            continue
        }
        if ($text -match '^;\s*([0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+([A-Za-z0-9_\.]+)\b') {
            $currentPpc = $Matches[1].ToUpperInvariant()
            $currentOp = $Matches[2].ToLowerInvariant()
            $rows.Add((New-HirRow $ordinal "ppc" $text $currentPpc $currentOp)) | Out-Null
            ++$ordinal
            continue
        }
        if ($text -match '^;') {
            continue
        }

        $rows.Add((New-HirRow $ordinal "hir" $text $currentPpc $currentOp)) | Out-Null
        ++$ordinal
    }

    if ($rows.Count -eq 0) {
        throw "No $Phase filtered dump for $functionUpper in $resolved."
    }

    [pscustomobject][ordered]@{
        log = $resolved
        function = $functionUpper
        rows = $rows
    }
}

function Read-CallEdgeUpper {
    param(
        [string]$Path,
        [string]$ParentFunction,
        [string]$CalleeFunction
    )
    if ([string]::IsNullOrWhiteSpace($Path) -or
        [string]::IsNullOrWhiteSpace($ParentFunction) -or
        [string]::IsNullOrWhiteSpace($CalleeFunction) -or
        !(Test-Path -LiteralPath $Path)) {
        return $null
    }

    $parentUpper = $ParentFunction.ToUpperInvariant()
    $calleeUpper = $CalleeFunction.ToUpperInvariant()
    $best = $null
    foreach ($line in Get-Content -LiteralPath (Resolve-Path -LiteralPath $Path).Path) {
        if ($line -match "A64 speed profile call edge.*fn $parentUpper .*target=$calleeUpper .*calls_total=([0-9]+).*body_ticks_total=([0-9]+).*ticks_per_call=([0-9]+)") {
            $best = [pscustomobject][ordered]@{
                calls_total = [int64]$Matches[1]
                body_ticks_total = [int64]$Matches[2]
                ticks_per_call = [int64]$Matches[3]
            }
        }
    }
    return $best
}

function Get-BoundaryKind {
    param([object]$Row)
    if ($Row.kind -eq "label") { return "label" }
    if ($Row.op -eq "context_barrier") { return "context_barrier" }
    if ($Row.op -like "call*") { return "call" }
    if ($Row.op -like "branch*") { return "branch" }
    if ($Row.op -eq "return") { return "return" }
    return ""
}

$hir = Read-HirFunction -Path $LogPath -Function $Function -Phase $Phase
$edge = Read-CallEdgeUpper -Path $EdgeProfileLog -ParentFunction $ParentFunction -CalleeFunction $CalleeFunction

$fpscrRows = @($hir.rows | Where-Object {
    $_.kind -eq "hir" -and
    ($_.op -eq "load_context" -or $_.op -eq "store_context") -and
    $null -ne $_.context_offset -and
    [int]$_.context_offset -eq 2628
})
$loads = @($fpscrRows | Where-Object { $_.op -eq "load_context" })
$stores = @($fpscrRows | Where-Object { $_.op -eq "store_context" })

$loadPcs = @{}
$storePcs = @{}
foreach ($row in $loads) { Add-Count $loadPcs $row.ppc_address }
foreach ($row in $stores) { Add-Count $storePcs $row.ppc_address }

$pairedUpdates = 0
$unpairedLoads = 0
$unpairedStores = 0
$storeByOrdinal = @{}
foreach ($store in $stores) {
    $storeByOrdinal[$store.ordinal] = $store
}
foreach ($load in $loads) {
    $nextTarget = $fpscrRows |
        Where-Object { $_.ordinal -gt $load.ordinal } |
        Select-Object -First 1
    if ($null -ne $nextTarget -and $nextTarget.op -eq "store_context") {
        ++$pairedUpdates
    } else {
        ++$unpairedLoads
    }
}
foreach ($store in $stores) {
    $prevTarget = $fpscrRows |
        Where-Object { $_.ordinal -lt $store.ordinal } |
        Select-Object -Last 1
    if ($null -eq $prevTarget -or $prevTarget.op -ne "load_context") {
        ++$unpairedStores
    }
}

$strictTransitions = 0
$cfgTransitions = 0
$callVisibilityTransitions = 0
$exitFlushTransitions = 0
$transitionBoundaryTotals = @{}
$callFlushPcs = @{}
$returnFlushPcs = @{}
$topTransitions = New-Object System.Collections.Generic.List[object]

for ($i = 0; $i -lt $stores.Count; ++$i) {
    $store = $stores[$i]
    $nextLoad = $loads | Where-Object { $_.ordinal -gt $store.ordinal } | Select-Object -First 1
    $endOrdinal = if ($null -ne $nextLoad) { $nextLoad.ordinal } else { [int]::MaxValue }
    $between = @($hir.rows | Where-Object {
        $_.ordinal -gt $store.ordinal -and $_.ordinal -lt $endOrdinal
    })
    $boundaries = @{}
    $callPcs = @{}
    $hasCall = $false
    $hasReturn = $false
    foreach ($row in $between) {
        $boundary = Get-BoundaryKind $row
        if ([string]::IsNullOrWhiteSpace($boundary)) {
            continue
        }
        Add-Count $boundaries $boundary
        Add-Count $transitionBoundaryTotals $boundary
        if ($boundary -eq "call") {
            $hasCall = $true
            Add-Count $callPcs $row.ppc_address
            Add-Count $callFlushPcs $row.ppc_address
        } elseif ($boundary -eq "return") {
            $hasReturn = $true
            Add-Count $returnFlushPcs $row.ppc_address
        }
    }

    $classification = "strict_same_window"
    if ($hasCall -or $hasReturn) {
        $classification = "external_visibility_flush_required"
        ++$callVisibilityTransitions
    } elseif ($null -eq $nextLoad) {
        $classification = "exit_flush_required"
        ++$exitFlushTransitions
    } elseif ($boundaries.Count -gt 0) {
        $classification = "cfg_phi_or_stack_carrier_required"
        ++$cfgTransitions
    } else {
        ++$strictTransitions
    }

    $topTransitions.Add([pscustomobject][ordered]@{
        store_pc = $store.ppc_address
        next_load_pc = if ($null -ne $nextLoad) { $nextLoad.ppc_address } else { "-" }
        classification = $classification
        boundaries = Get-TopPairs $boundaries $Top
        call_pcs = Get-TopPairs $callPcs $Top
    }) | Out-Null
}

$dynamicCalls = if ($null -ne $edge) { [int64]$edge.calls_total } else { 0 }
$dynamicLoadUpper = if ($dynamicCalls -gt 0) { $dynamicCalls * [int64]$loads.Count } else { 0 }
$dynamicStoreUpper = if ($dynamicCalls -gt 0) { $dynamicCalls * [int64]$stores.Count } else { 0 }
$strictDynamicUpper = if ($dynamicCalls -gt 0) { $dynamicCalls * [int64]$strictTransitions } else { 0 }
$cfgDynamicUpper = if ($dynamicCalls -gt 0) { $dynamicCalls * [int64]$cfgTransitions } else { 0 }

Write-Output "# HIR FPSCR Dirty-Cache Audit"
Write-Output ""
Write-Output ("log={0}" -f $hir.log)
Write-Output ("function={0} phase={1}" -f $hir.function, $Phase)
Write-Output ("parsed_rows={0}" -f $hir.rows.Count)
if ($null -ne $edge) {
    Write-Output ("edge_parent={0} edge_callee={1} calls_total={2} body_ticks_total={3} ticks_per_call={4}" -f
        $ParentFunction.ToUpperInvariant(), $CalleeFunction.ToUpperInvariant(),
        $edge.calls_total, $edge.body_ticks_total, $edge.ticks_per_call)
}
Write-Output ""
Write-Output "## FPSCR Access Summary"
Write-Output ("loads={0} stores={1} paired_load_store_updates={2} unpaired_loads={3} unpaired_stores={4}" -f
    $loads.Count, $stores.Count, $pairedUpdates, $unpairedLoads, $unpairedStores)
Write-Output ("load_pcs={0}" -f (Get-TopPairs $loadPcs $Top))
Write-Output ("store_pcs={0}" -f (Get-TopPairs $storePcs $Top))
if ($dynamicCalls -gt 0) {
    Write-Output ("dynamic_context_access_upper loads={0} stores={1} total={2}" -f
        $dynamicLoadUpper, $dynamicStoreUpper, ($dynamicLoadUpper + $dynamicStoreUpper))
}
Write-Output ""
Write-Output "## Dirty-Cache Transition Shape"
Write-Output ("store_to_next_load_transitions={0} strict_same_window={1} cfg_phi_or_stack_carrier_required={2} external_visibility_flush_required={3} exit_flush_required={4}" -f
    $stores.Count, $strictTransitions, $cfgTransitions, $callVisibilityTransitions, $exitFlushTransitions)
Write-Output ("boundary_totals_between_store_and_next_load={0}" -f (Get-TopPairs $transitionBoundaryTotals $Top))
Write-Output ("call_flush_pcs={0}" -f (Get-TopPairs $callFlushPcs $Top))
Write-Output ("return_flush_pcs={0}" -f (Get-TopPairs $returnFlushPcs $Top))
if ($dynamicCalls -gt 0) {
    Write-Output ("strict_dynamic_upper={0} cfg_dynamic_upper={1}" -f $strictDynamicUpper, $cfgDynamicUpper)
}
Write-Output ""
Write-Output "## Transition Detail"
foreach ($item in ($topTransitions | Select-Object -First $Top)) {
    Write-Output ("store_pc={0} next_load_pc={1} class={2} boundaries={3} call_pcs={4}" -f
        $item.store_pc, $item.next_load_pc, $item.classification,
        $item.boundaries, $item.call_pcs)
}
Write-Output ""
Write-Output "## Decision"
if ($strictTransitions -eq 0 -and $cfgTransitions -gt 0) {
    Write-Output "decision=no_behavior_patch_yet"
    Write-Output "reason=fpscr has no strict same-window reload wins; useful savings require CFG-aware dirty state through labels/branches/context barriers plus exact writeback before external visibility."
} elseif ($strictTransitions -gt 0) {
    Write-Output "decision=small_strict_probe_possible"
    Write-Output "reason=some fpscr store-to-load transitions stay inside a strict local window, but mutable stores still require exact dirty writeback before calls/exits."
} else {
    Write-Output "decision=blocked_without_cfg_model"
    Write-Output "reason=fpscr updates are mutable and only cross-boundary opportunities remain."
}
Write-Output "next_experiment=do not patch fpscr forwarding from this report alone; design a CFG-aware dirty carrier with explicit call/exit writebacks, or return to broader 8228252C-822825C4 state/vector work."
