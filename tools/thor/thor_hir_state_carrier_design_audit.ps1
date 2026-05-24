param(
    [Parameter(Mandatory = $true)]
    [string]$ParentLogPath,
    [Parameter(Mandatory = $true)]
    [string]$ParentFunction,
    [Parameter(Mandatory = $true)]
    [string]$ParentStartGuest,
    [Parameter(Mandatory = $true)]
    [string]$ParentEndGuest,
    [Parameter(Mandatory = $true)]
    [string]$ParentCallPc,
    [Parameter(Mandatory = $true)]
    [string]$CalleeLogPath,
    [Parameter(Mandatory = $true)]
    [string]$CalleeFunction,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [string]$Offsets = "56,296,2628,16",
    [string]$EdgeProfileLog = "",
    [string]$PreservedHelperPattern = "__savegprlr_|__restgprlr_",
    [string]$PreservedCallPcs = "",
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

function Add-Unique {
    param(
        [System.Collections.Generic.List[string]]$List,
        [string]$Value
    )
    if ([string]::IsNullOrWhiteSpace($Value) -or $Value -eq "-") {
        return
    }
    if (!$List.Contains($Value)) {
        $List.Add($Value) | Out-Null
    }
}

function Convert-HexToInt64 {
    param([string]$Hex)
    return [Convert]::ToInt64($Hex, 16)
}

function Format-List {
    param([object]$Values)
    if ($null -eq $Values) {
        return "-"
    }
    $items = @($Values | Where-Object { ![string]::IsNullOrWhiteSpace([string]$_) })
    if ($items.Count -eq 0) {
        return "-"
    }
    return ($items -join ",")
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
        context_field = if ($null -ne $offset) { Get-ContextOffsetName $offset } else { "" }
    }
}

function Read-HirFunction {
    param(
        [string]$LogPath,
        [string]$Function,
        [string]$Phase
    )

    if (!(Test-Path -LiteralPath $LogPath)) {
        throw "LogPath not found: $LogPath"
    }
    $resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
    $functionUpper = $Function.ToUpperInvariant()
    $needle = "Filtered function dump $functionUpper ${Phase}:"
    $rows = New-Object System.Collections.Generic.List[object]
    $currentPpc = ""
    $currentOp = ""
    $ordinal = 0

    foreach ($line in Get-Content -LiteralPath $resolvedLog) {
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
        throw "No $Phase filtered dump for $functionUpper in $resolvedLog."
    }

    [pscustomobject][ordered]@{
        log = $resolvedLog
        function = $functionUpper
        rows = $rows
    }
}

function Read-EdgeProfile {
    param(
        [string]$Path,
        [string]$ParentFunction,
        [string]$CalleeFunction
    )

    if ([string]::IsNullOrWhiteSpace($Path) -or !(Test-Path -LiteralPath $Path)) {
        return $null
    }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $parent = $ParentFunction.ToUpperInvariant()
    $callee = $CalleeFunction.ToUpperInvariant()
    $best = $null
    foreach ($line in Get-Content -LiteralPath $resolved) {
        if ($line -notmatch "A64 speed profile call edge top") {
            continue
        }
        if ($line -match "fn\s+$parent\b.*block=(?<block>[0-9A-Fa-f]{8}).*target=$callee\b.*calls_delta=(?<calls_delta>[0-9]+).*calls_total=(?<calls_total>[0-9]+).*body_ticks_delta=(?<body_delta>[0-9]+).*body_ticks_total=(?<body_total>[0-9]+).*ticks_per_call=(?<tpc>[0-9]+)") {
            $best = [pscustomobject][ordered]@{
                log = $resolved
                block = $Matches.block.ToUpperInvariant()
                calls_delta = [int64]$Matches.calls_delta
                calls_total = [int64]$Matches.calls_total
                body_ticks_delta = [int64]$Matches.body_delta
                body_ticks_total = [int64]$Matches.body_total
                ticks_per_call = [int64]$Matches.tpc
                line = $line.Trim()
            }
        }
    }
    return $best
}

function Get-CallKind {
    param(
        [object]$Row,
        [hashtable]$PreservedCallPcSet,
        [string]$HelperPattern
    )

    if ($Row.text -match $HelperPattern) {
        return "preserved_helper"
    }
    if ($PreservedCallPcSet.ContainsKey($Row.ppc_address)) {
        return "preserved_child_pc"
    }
    if ($Row.op -match 'call_indirect' -or $Row.op -eq "call.2") {
        return "indirect_or_exit"
    }
    return "unsafe_or_unknown"
}

$targetOffsets = @()
foreach ($raw in ($Offsets -split ',')) {
    if (![string]::IsNullOrWhiteSpace($raw)) {
        $targetOffsets += [int]$raw.Trim()
    }
}
$offsetSet = @{}
foreach ($offset in $targetOffsets) {
    $offsetSet[$offset] = $true
}

$preservedCallPcSet = @{}
foreach ($raw in ($PreservedCallPcs -split ',')) {
    $pc = $raw.Trim().ToUpperInvariant()
    if (![string]::IsNullOrWhiteSpace($pc)) {
        $preservedCallPcSet[$pc] = $true
    }
}

$parent = Read-HirFunction -LogPath $ParentLogPath -Function $ParentFunction -Phase $Phase
$callee = Read-HirFunction -LogPath $CalleeLogPath -Function $CalleeFunction -Phase $Phase
$edge = Read-EdgeProfile -Path $EdgeProfileLog -ParentFunction $ParentFunction -CalleeFunction $CalleeFunction

$startInt = Convert-HexToInt64 $ParentStartGuest
$endInt = Convert-HexToInt64 $ParentEndGuest
$callPcUpper = $ParentCallPc.ToUpperInvariant()

$callRow = $null
foreach ($row in $parent.rows) {
    if ($row.kind -eq "ppc" -and $row.ppc_address -eq $callPcUpper) {
        $callRow = $row
        break
    }
}
if ($null -eq $callRow) {
    throw "Parent call PC $callPcUpper not found in $($parent.log)."
}

$spanRows = @($parent.rows | Where-Object {
    $_.ppc_address -match '^[0-9A-F]{8}$' -and
    (Convert-HexToInt64 $_.ppc_address) -ge $startInt -and
    (Convert-HexToInt64 $_.ppc_address) -le $endInt
})

$spanLoads = @{}
$spanStores = @{}
$spanOps = @{}
foreach ($row in $spanRows) {
    if ($row.kind -ne "hir") {
        continue
    }
    Add-Count $spanOps $row.op
    if ($row.op -eq "load_context" -and $null -ne $row.context_offset) {
        Add-Count $spanLoads $row.context_field
    }
    if ($row.op -eq "store_context" -and $null -ne $row.context_offset) {
        Add-Count $spanStores $row.context_field
    }
}

$parentStores = @{}
foreach ($row in $parent.rows) {
    if ($row.ordinal -ge $callRow.ordinal) {
        break
    }
    if ($row.op -ne "store_context" -or $null -eq $row.context_offset) {
        continue
    }
    if ($offsetSet.ContainsKey($row.context_offset)) {
        $parentStores[$row.context_offset] = $row
    }
}

foreach ($row in $parent.rows) {
    if ($row.ordinal -le $callRow.ordinal) {
        continue
    }
    if ($row.ppc_address -ne $callPcUpper) {
        break
    }
    if ($row.op -eq "store_context" -and $null -ne $row.context_offset -and
        $offsetSet.ContainsKey($row.context_offset)) {
        $parentStores[$row.context_offset] = $row
    }
}

$calleeStats = @{}
foreach ($offset in $targetOffsets) {
    $calleeStats[$offset] = [pscustomobject][ordered]@{
        offset = $offset
        field = Get-ContextOffsetName $offset
        loads = 0
        stores = 0
        first_load = $null
        first_store = $null
        load_pcs = @{}
        store_pcs = @{}
        writeback_call_pcs = New-Object System.Collections.Generic.List[string]
    }
}

$calleeCalls = New-Object System.Collections.Generic.List[object]
$calleeBoundaryCounts = @{
    labels = 0
    branches = 0
    context_barriers = 0
}

foreach ($row in $callee.rows) {
    if ($row.kind -eq "label") {
        ++$calleeBoundaryCounts.labels
    }
    if ($row.op -match '^branch') {
        ++$calleeBoundaryCounts.branches
    }
    if ($row.op -eq "context_barrier") {
        ++$calleeBoundaryCounts.context_barriers
    }
    if ($row.op -match '^call') {
        $kind = Get-CallKind -Row $row -PreservedCallPcSet $preservedCallPcSet -HelperPattern $PreservedHelperPattern
        $calleeCalls.Add([pscustomobject][ordered]@{
            pc = $row.ppc_address
            ordinal = $row.ordinal
            kind = $kind
            op = $row.op
            text = $row.text
        }) | Out-Null
    }
    if (($row.op -eq "load_context" -or $row.op -eq "store_context") -and
        $null -ne $row.context_offset -and $offsetSet.ContainsKey($row.context_offset)) {
        $stat = $calleeStats[$row.context_offset]
        if ($row.op -eq "load_context") {
            ++$stat.loads
            Add-Count $stat.load_pcs $row.ppc_address
            if ($null -eq $stat.first_load) {
                $stat.first_load = $row
            }
        } else {
            ++$stat.stores
            Add-Count $stat.store_pcs $row.ppc_address
            if ($null -eq $stat.first_store) {
                $stat.first_store = $row
            }
        }
    }
}

foreach ($offset in $targetOffsets) {
    $stat = $calleeStats[$offset]
    $storeRows = @($callee.rows | Where-Object { $_.op -eq "store_context" -and $_.context_offset -eq $offset })
    $loadRows = @($callee.rows | Where-Object { $_.op -eq "load_context" -and $_.context_offset -eq $offset })
    foreach ($store in $storeRows) {
        $nextLoad = $null
        foreach ($load in $loadRows) {
            if ($load.ordinal -gt $store.ordinal) {
                $nextLoad = $load
                break
            }
        }
        foreach ($call in $calleeCalls) {
            if ($call.ordinal -le $store.ordinal) {
                continue
            }
            if ($null -ne $nextLoad -and $call.ordinal -ge $nextLoad.ordinal) {
                continue
            }
            Add-Unique $stat.writeback_call_pcs $call.pc
        }
    }
}

$edgeCallsTotal = 0
if ($null -ne $edge) {
    $edgeCallsTotal = $edge.calls_total
}

Write-Output "# HIR State-Carrier Design Audit"
Write-Output ""
Write-Output ("parent_log={0}" -f $parent.log)
Write-Output ("callee_log={0}" -f $callee.log)
Write-Output ("parent_function={0} parent_span={1}-{2} parent_call_pc={3} callee_function={4} phase={5}" -f $parent.function, $ParentStartGuest.ToUpperInvariant(), $ParentEndGuest.ToUpperInvariant(), $callPcUpper, $callee.function, $Phase)
Write-Output ("offsets={0}" -f (($targetOffsets | ForEach-Object { "{0}:{1}" -f $_, (Get-ContextOffsetName $_) }) -join ","))
if ($null -ne $edge) {
    Write-Output ("edge block={0} calls_total={1} calls_delta={2} body_ticks_total={3} body_ticks_delta={4} ticks_per_call={5}" -f $edge.block, $edge.calls_total, $edge.calls_delta, $edge.body_ticks_total, $edge.body_ticks_delta, $edge.ticks_per_call)
} else {
    Write-Output "edge=-"
}
Write-Output ""

Write-Output "## Parent Span Summary"
Write-Output ("span_hir_rows={0} span_ops={1}" -f (@($spanRows | Where-Object { $_.kind -eq "hir" }).Count), (Get-TopPairs $spanOps $Top))
Write-Output ("span_context_loads={0}" -f (Get-TopPairs $spanLoads $Top))
Write-Output ("span_context_stores={0}" -f (Get-TopPairs $spanStores $Top))
Write-Output ""

Write-Output "## Callee Boundary Summary"
$callKindCounts = @{}
foreach ($call in $calleeCalls) {
    Add-Count $callKindCounts $call.kind
}
Write-Output ("calls={0} call_kinds={1} labels={2} branches={3} context_barriers={4}" -f $calleeCalls.Count, (Get-TopPairs $callKindCounts 8), $calleeBoundaryCounts.labels, $calleeBoundaryCounts.branches, $calleeBoundaryCounts.context_barriers)
foreach ($call in $calleeCalls) {
    Write-Output ("call pc={0} ordinal={1} kind={2} op={3} text={4}" -f $call.pc, $call.ordinal, $call.kind, $call.op, $call.text)
}
Write-Output ""

Write-Output "## Candidate Requirements"
foreach ($offset in $targetOffsets) {
    $field = Get-ContextOffsetName $offset
    $stat = $calleeStats[$offset]
    $parentSeedPc = "-"
    $parentSeedDistance = "-"
    $parentSeedText = "-"
    if ($parentStores.ContainsKey($offset)) {
        $seedRow = $parentStores[$offset]
        $parentSeedPc = $seedRow.ppc_address
        $parentSeedDistance = $callRow.ordinal - $seedRow.ordinal
        $parentSeedText = $seedRow.text
    }

    $loadUpper = if ($edgeCallsTotal -gt 0) { [int64]$stat.loads * [int64]$edgeCallsTotal } else { 0 }
    $storeUpper = if ($edgeCallsTotal -gt 0) { [int64]$stat.stores * [int64]$edgeCallsTotal } else { 0 }

    $candidateClass = "blocked_no_callee_load"
    $risk = "none"
    $seedRequirement = "none"
    $killRequirement = "none"
    $writebackRequirement = "none"
    $recommendation = "not a carrier candidate"

    $unsafeCalls = New-Object System.Collections.Generic.List[string]
    foreach ($call in $calleeCalls) {
        if ($call.kind -eq "unsafe_or_unknown" -or $call.kind -eq "indirect_or_exit") {
            Add-Unique $unsafeCalls $call.pc
        }
    }

    if ($field -eq "lr") {
        $candidateClass = "blocked_call_link_state"
        $risk = "high"
        $seedRequirement = "do_not_seed_as_cache"
        $killRequirement = "call_return_semantics"
        $writebackRequirement = "normal_lr_store_path"
        $recommendation = "do not forward LR; it is PPC call/return state"
    } elseif ($stat.loads -gt 0 -and $stat.stores -eq 0) {
        $candidateClass = "read_only_entry_seed"
        $risk = "medium"
        $seedRequirement = "pair_entry_or_caller_seeded_callee_slot"
        $killRequirement = "unknown_calls_or_target_store_or_exit"
        $writebackRequirement = "none_read_only"
        $recommendation = "possible only as function-pair design; keep parent context store visible until proven movable"
    } elseif ($field -eq "fpscr") {
        $candidateClass = "dirty_cfg_carrier"
        $risk = "very_high"
        $seedRequirement = "load_or_parent_seed_plus_dirty_bit"
        $killRequirement = "branches_labels_barriers_calls_exits_readers"
        $writebackRequirement = "writeback_before_call_visible_points_and_exits"
        $recommendation = "audit/design only; do not patch without exact dirty writebacks"
    } elseif ($stat.loads -gt 0 -and $stat.stores -gt 0) {
        $candidateClass = "mutable_state_cache"
        $risk = "high"
        $seedRequirement = "load_or_parent_seed_plus_dirty_bit"
        $killRequirement = "aliases_calls_barriers_exits_overlapping_writes"
        $writebackRequirement = "writeback_before_external_visibility"
        $recommendation = "needs full dirty/alias visibility model; not a first behavior patch"
    }

    $firstLoadPc = if ($null -ne $stat.first_load) { $stat.first_load.ppc_address } else { "-" }
    $firstStorePc = if ($null -ne $stat.first_store) { $stat.first_store.ppc_address } else { "-" }
    $writebackPcs = Format-List $stat.writeback_call_pcs
    $unsafeCallText = Format-List $unsafeCalls

    Write-Output ("candidate field={0} offset={1} class={2} risk={3} parent_seed_pc={4} parent_seed_distance={5} first_load_pc={6} first_store_pc={7} callee_loads={8} callee_stores={9} dynamic_load_upper={10} dynamic_store_upper={11} seed={12} kill={13} unsafe_call_pcs={14} writeback={15} writeback_pcs={16} load_pcs={17} store_pcs={18} recommendation={19}" -f
        $field, $offset, $candidateClass, $risk, $parentSeedPc, $parentSeedDistance,
        $firstLoadPc, $firstStorePc, $stat.loads, $stat.stores, $loadUpper,
        $storeUpper, $seedRequirement, $killRequirement, $unsafeCallText,
        $writebackRequirement, $writebackPcs, (Get-TopPairs $stat.load_pcs $Top),
        (Get-TopPairs $stat.store_pcs $Top), $recommendation)
    Write-Output ("  parent_seed_text={0}" -f $parentSeedText)
}
Write-Output ""

Write-Output "## Decision"
Write-Output "decision=no_behavior_patch_yet"
Write-Output "reason=the top local span is still live across a hot direct-call edge; every useful state carrier needs explicit seed, kill, and writeback machinery before changing generated behavior."
Write-Output "next_experiment=use this candidate table to design a default-off audit/probe for CFG/interprocedural carriers; start with compile-time/dynamic counters, not a speed A/B."
