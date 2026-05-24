param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [string]$Offsets = "296,2628",
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

function Get-ContextOffsetName {
    param([int]$Offset)

    if ($Offset -eq 0) { return "thread_state" }
    if ($Offset -eq 8) { return "virtual_membase" }
    if ($Offset -eq 16) { return "lr" }
    if ($Offset -eq 24) { return "ctr" }
    if ($Offset -ge 32 -and $Offset -lt 288) {
        $index = [Math]::Floor(($Offset - 32) / 8)
        $lane = ($Offset - 32) % 8
        if ($lane -eq 0) { return ("r[{0}]" -f $index) }
        return ("r[{0}]+{1}" -f $index, $lane)
    }
    if ($Offset -ge 288 -and $Offset -lt 544) {
        $index = [Math]::Floor(($Offset - 288) / 8)
        $lane = ($Offset - 288) % 8
        if ($lane -eq 0) { return ("f[{0}]" -f $index) }
        return ("f[{0}]+{1}" -f $index, $lane)
    }
    if ($Offset -ge 544 -and $Offset -lt 2592) {
        $index = [Math]::Floor(($Offset - 544) / 16)
        $lane = ($Offset - 544) % 16
        if ($lane -eq 0) { return ("v[{0}]" -f $index) }
        return ("v[{0}]+{1}" -f $index, $lane)
    }
    if ($Offset -ge 2592 -and $Offset -lt 2596) {
        $names = @("xer_ca", "xer_ov", "xer_so", "xer_pad")
        return $names[$Offset - 2592]
    }
    if ($Offset -ge 2596 -and $Offset -lt 2628) {
        $crIndex = [Math]::Floor(($Offset - 2596) / 4)
        $fieldIndex = ($Offset - 2596) % 4
        $fields = @(
            @("lt", "gt", "eq", "so"),
            @("fx", "fex", "vx", "ox"),
            @("0", "1", "2", "3"),
            @("0", "1", "2", "3"),
            @("0", "1", "2", "3"),
            @("0", "1", "2", "3"),
            @("all_equal", "1", "none_equal", "3"),
            @("0", "1", "2", "3")
        )
        return ("cr{0}.{1}" -f $crIndex, $fields[$crIndex][$fieldIndex])
    }
    if ($Offset -ge 2628 -and $Offset -lt 2632) { return "fpscr" }
    if ($Offset -eq 2632) { return "vscr_sat" }
    if ($Offset -eq 2636) { return "thread_id" }
    if ($Offset -ge 2640) { return "runtime_or_reservation" }
    return "unknown"
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
        [string]$PpcOp,
        [int]$Region,
        [string]$RegionName
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
        context_field = if ($null -ne $offset) { Get-ContextOffsetName $offset } else { "" }
        region = $Region
        region_name = $RegionName
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
    $region = 0
    $regionName = "<entry>"
    $pendingBoundary = ""

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
            ++$region
            $regionName = $Matches[1]
            $pendingBoundary = ""
            $rows.Add((New-HirRow $ordinal "label" $text $currentPpc $currentOp $region $regionName)) | Out-Null
            ++$ordinal
            continue
        }
        if ($text -match '^;\s*([0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+([A-Za-z0-9_\.]+)\b') {
            $currentPpc = $Matches[1].ToUpperInvariant()
            $currentOp = $Matches[2].ToLowerInvariant()
            $rows.Add((New-HirRow $ordinal "ppc" $text $currentPpc $currentOp $region $regionName)) | Out-Null
            ++$ordinal
            continue
        }
        if ($text -match '^;') {
            continue
        }

        if (![string]::IsNullOrWhiteSpace($pendingBoundary)) {
            ++$region
            $regionName = "{0}#after_{1}_{2}" -f $regionName, $pendingBoundary, $region
            $pendingBoundary = ""
        }

        $row = New-HirRow $ordinal "hir" $text $currentPpc $currentOp $region $regionName
        $rows.Add($row) | Out-Null
        ++$ordinal

        if ($row.op -eq "context_barrier") {
            $pendingBoundary = "barrier"
        } elseif ($row.op -like "call*") {
            $pendingBoundary = "call"
        } elseif ($row.op -like "branch*") {
            $pendingBoundary = "branch"
        } elseif ($row.op -eq "return") {
            $pendingBoundary = "return"
        }
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

$targetOffsets = @()
foreach ($raw in ($Offsets -split ',')) {
    if (![string]::IsNullOrWhiteSpace($raw)) {
        $targetOffsets += [int]$raw.Trim()
    }
}
if ($targetOffsets.Count -eq 0) {
    throw "No offsets provided."
}
$offsetSet = @{}
foreach ($offset in $targetOffsets) {
    $offsetSet[$offset] = $true
}

$hir = Read-HirFunction -Path $LogPath -Function $Function -Phase $Phase

$stats = @{}
$state = @{}
foreach ($offset in $targetOffsets) {
    $stats[$offset] = [ordered]@{
        field = Get-ContextOffsetName $offset
        loads = 0
        stores = 0
        regions = @{}
        load_pcs = @{}
        store_pcs = @{}
        strict_redundant_loads = 0
        strict_load_after_store = 0
        cross_boundary_reload_after_load = 0
        cross_boundary_reload_after_store = 0
        store_after_load = 0
        store_without_cached_value = 0
        boundary_reasons = @{}
        risky_call_crossings = 0
        first_load = $null
        first_store = $null
    }
    $state[$offset] = [ordered]@{
        has_cached_value = $false
        cached_from = ""
        dirty_boundary = $false
        boundary_reasons = @{}
    }
}

$globalOps = @{
    labels = 0
    context_barriers = 0
    calls = 0
    branches = 0
    returns = 0
}

foreach ($row in $hir.rows) {
    $boundary = ""
    if ($row.kind -eq "label") {
        ++$globalOps.labels
        $boundary = "label"
    } elseif ($row.op -eq "context_barrier") {
        ++$globalOps.context_barriers
        $boundary = "context_barrier"
    } elseif ($row.op -like "call*") {
        ++$globalOps.calls
        $boundary = "call"
    } elseif ($row.op -like "branch*") {
        ++$globalOps.branches
        $boundary = "branch"
    } elseif ($row.op -eq "return") {
        ++$globalOps.returns
        $boundary = "return"
    }

    if (![string]::IsNullOrWhiteSpace($boundary)) {
        foreach ($offset in $targetOffsets) {
            if ($state[$offset].has_cached_value) {
                $state[$offset].dirty_boundary = $true
                Add-Count $state[$offset].boundary_reasons $boundary 1
            }
        }
    }

    if ($row.op -ne "load_context" -and $row.op -ne "store_context") {
        continue
    }
    if ($null -eq $row.context_offset -or !$offsetSet.ContainsKey($row.context_offset)) {
        continue
    }

    $offset = [int]$row.context_offset
    $stat = $stats[$offset]
    $slot = $state[$offset]
    Add-Count $stat.regions $row.region_name 1

    if ($row.op -eq "load_context") {
        ++$stat.loads
        Add-Count $stat.load_pcs $row.ppc_address 1
        if ($null -eq $stat.first_load) {
            $stat.first_load = $row
        }

        if ($slot.has_cached_value) {
            if ($slot.dirty_boundary) {
                if ($slot.cached_from -eq "store") {
                    ++$stat.cross_boundary_reload_after_store
                } else {
                    ++$stat.cross_boundary_reload_after_load
                }
                foreach ($reason in $slot.boundary_reasons.Keys) {
                    Add-Count $stat.boundary_reasons $reason ([int64]$slot.boundary_reasons[$reason])
                }
                if ($slot.boundary_reasons.ContainsKey("call")) {
                    ++$stat.risky_call_crossings
                }
            } else {
                if ($slot.cached_from -eq "store") {
                    ++$stat.strict_load_after_store
                } else {
                    ++$stat.strict_redundant_loads
                }
            }
        }

        $slot.has_cached_value = $true
        $slot.cached_from = "load"
        $slot.dirty_boundary = $false
        $slot.boundary_reasons = @{}
    } else {
        ++$stat.stores
        Add-Count $stat.store_pcs $row.ppc_address 1
        if ($null -eq $stat.first_store) {
            $stat.first_store = $row
        }
        if ($slot.has_cached_value -and !$slot.dirty_boundary) {
            ++$stat.store_after_load
        } elseif (!$slot.has_cached_value) {
            ++$stat.store_without_cached_value
        }
        $slot.has_cached_value = $true
        $slot.cached_from = "store"
        $slot.dirty_boundary = $false
        $slot.boundary_reasons = @{}
    }
}

Write-Output "# HIR Callee Local Promotion Audit"
Write-Output ""
Write-Output ("log={0}" -f $hir.log)
Write-Output ("function={0} phase={1}" -f $hir.function, $Phase)
Write-Output ("offsets={0}" -f (($targetOffsets | ForEach-Object { "{0}:{1}" -f $_, (Get-ContextOffsetName $_) }) -join ","))
Write-Output ("parsed_rows={0} labels={1} context_barriers={2} calls={3} branches={4} returns={5}" -f $hir.rows.Count, $globalOps.labels, $globalOps.context_barriers, $globalOps.calls, $globalOps.branches, $globalOps.returns)
Write-Output "strict_region_policy=labels, context_barrier, calls, branches, and returns break local promotion windows"
Write-Output ""
Write-Output "## Target Summary"
foreach ($offset in $targetOffsets) {
    $stat = $stats[$offset]
    $firstLoadPc = if ($null -ne $stat.first_load) { $stat.first_load.ppc_address } else { "-" }
    $firstStorePc = if ($null -ne $stat.first_store) { $stat.first_store.ppc_address } else { "-" }
    Write-Output ("target field={0} offset={1} loads={2} stores={3} regions={4} first_load_pc={5} first_store_pc={6} strict_redundant_loads={7} strict_load_after_store={8} cross_boundary_reload_after_load={9} cross_boundary_reload_after_store={10} store_after_load={11} store_without_cached_value={12} risky_call_crossings={13} boundary_reasons={14} load_pcs={15} store_pcs={16}" -f
        $stat.field, $offset, $stat.loads, $stat.stores, $stat.regions.Count,
        $firstLoadPc, $firstStorePc, $stat.strict_redundant_loads,
        $stat.strict_load_after_store, $stat.cross_boundary_reload_after_load,
        $stat.cross_boundary_reload_after_store, $stat.store_after_load,
        $stat.store_without_cached_value, $stat.risky_call_crossings,
        (Get-TopPairs $stat.boundary_reasons $Top),
        (Get-TopPairs $stat.load_pcs $Top), (Get-TopPairs $stat.store_pcs $Top))
}

Write-Output ""
Write-Output "## Candidate Interpretation"
foreach ($offset in $targetOffsets) {
    $stat = $stats[$offset]
    $strictWins = [int64]$stat.strict_redundant_loads + [int64]$stat.strict_load_after_store
    $crossWins = [int64]$stat.cross_boundary_reload_after_load + [int64]$stat.cross_boundary_reload_after_store
    $risk = "low"
    $suggestion = "none"
    if ($stat.stores -gt 0) {
        $risk = "high"
        $suggestion = "mutable_context_state_needs_exact_dirty_flush_model"
    } elseif ($strictWins -gt 0) {
        $risk = "medium"
        $suggestion = "callee_local_cache_within_strict_regions"
    } elseif ($crossWins -gt 0) {
        $risk = "medium_high"
        $suggestion = "requires_cfg_or_function_pair_carrier_across_boundaries"
    }
    if ($stat.risky_call_crossings -gt 0) {
        $risk = "high"
    }
    Write-Output ("candidate field={0} strict_wins={1} cross_boundary_wins={2} risk={3} suggestion={4}" -f
        $stat.field, $strictWins, $crossWins, $risk, $suggestion)
}

Write-Output ""
Write-Output "## Decision"
Write-Output "decision=no_behavior_patch_yet"
Write-Output "reason=strict local windows are separated by labels/barriers/branches/calls; any cross-boundary carrier needs explicit PPC visibility and call-clobber rules."
Write-Output "next_experiment=prefer a default-off audit/probe that measures f[1] carrier safety across the exact 82282490->82287788 boundary, while treating fpscr as mutable/high-risk unless source review proves exact update forwarding."
