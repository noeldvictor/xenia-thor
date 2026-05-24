param(
    [Parameter(Mandatory = $true)]
    [string]$ParentLogPath,
    [Parameter(Mandatory = $true)]
    [string]$ParentFunction,
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
    [int]$Top = 24
)

$ErrorActionPreference = "Stop"

function Convert-HexToInt64 {
    param([string]$Hex)
    return [Convert]::ToInt64($Hex, 16)
}

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

$parent = Read-HirFunction -LogPath $ParentLogPath -Function $ParentFunction -Phase $Phase
$callee = Read-HirFunction -LogPath $CalleeLogPath -Function $CalleeFunction -Phase $Phase
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

$parentStores = @{}
foreach ($row in $parent.rows) {
    if ($row.ordinal -ge $callRow.ordinal) {
        break
    }
    if ($row.op -ne "store_context" -or $null -eq $row.context_offset) {
        continue
    }
    if (!$offsetSet.ContainsKey($row.context_offset)) {
        continue
    }
    $parentStores[$row.context_offset] = $row
}

# Include the return-address store emitted at the call PC after the call marker.
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
    }
}

$firstBranchOrdinal = $null
foreach ($row in $callee.rows) {
    if ($row.kind -eq "ppc" -and $row.ppc_op -match '^b|^bc|^beq|^bne|^blt|^bgt|^ble|^bge') {
        $firstBranchOrdinal = $row.ordinal
        break
    }
}

foreach ($row in $callee.rows) {
    if ($row.op -ne "load_context" -and $row.op -ne "store_context") {
        continue
    }
    if ($null -eq $row.context_offset -or !$offsetSet.ContainsKey($row.context_offset)) {
        continue
    }
    $stat = $calleeStats[$row.context_offset]
    if ($row.op -eq "load_context") {
        ++$stat.loads
        Add-Count $stat.load_pcs $row.ppc_address 1
        if ($null -eq $stat.first_load) {
            $stat.first_load = $row
        }
    } else {
        ++$stat.stores
        Add-Count $stat.store_pcs $row.ppc_address 1
        if ($null -eq $stat.first_store) {
            $stat.first_store = $row
        }
    }
}

$edge = Read-EdgeProfile -Path $EdgeProfileLog -ParentFunction $ParentFunction -CalleeFunction $CalleeFunction

Write-Output "# HIR Interprocedural State Roundtrip Audit"
Write-Output ""
Write-Output ("parent_log={0}" -f $parent.log)
Write-Output ("callee_log={0}" -f $callee.log)
Write-Output ("parent_function={0} call_pc={1} callee_function={2} phase={3}" -f $parent.function, $callPcUpper, $callee.function, $Phase)
Write-Output ("offsets={0}" -f (($targetOffsets | ForEach-Object { "{0}:{1}" -f $_, (Get-ContextOffsetName $_) }) -join ","))
if ($null -ne $edge) {
    Write-Output ("edge_profile_log={0}" -f $edge.log)
    Write-Output ("edge block={0} calls_total={1} calls_delta={2} body_ticks_total={3} body_ticks_delta={4} ticks_per_call={5}" -f $edge.block, $edge.calls_total, $edge.calls_delta, $edge.body_ticks_total, $edge.body_ticks_delta, $edge.ticks_per_call)
} else {
    Write-Output "edge_profile_log=-"
}
Write-Output ""
Write-Output "## Parent Stores Before/At Call"
foreach ($offset in $targetOffsets) {
    $field = Get-ContextOffsetName $offset
    if ($parentStores.ContainsKey($offset)) {
        $row = $parentStores[$offset]
        Write-Output ("parent_store field={0} offset={1} pc={2} distance_to_call={3} text={4}" -f $field, $offset, $row.ppc_address, ($callRow.ordinal - $row.ordinal), $row.text)
    } else {
        Write-Output ("parent_store field={0} offset={1} pc=- distance_to_call=- text=-" -f $field, $offset)
    }
}
Write-Output ""
Write-Output "## Callee Context Access"
foreach ($offset in $targetOffsets) {
    $stat = $calleeStats[$offset]
    $firstLoadPc = if ($null -ne $stat.first_load) { $stat.first_load.ppc_address } else { "-" }
    $firstLoadOrdinal = if ($null -ne $stat.first_load) { $stat.first_load.ordinal } else { "-" }
    $beforeFirstBranch = "n/a"
    if ($null -ne $firstBranchOrdinal -and $null -ne $stat.first_load) {
        $beforeFirstBranch = [string]($stat.first_load.ordinal -lt $firstBranchOrdinal)
    }
    $firstStorePc = if ($null -ne $stat.first_store) { $stat.first_store.ppc_address } else { "-" }
    Write-Output ("callee_access field={0} offset={1} loads={2} stores={3} first_load_pc={4} first_load_ordinal={5} first_load_before_first_branch={6} first_store_pc={7} load_pcs={8} store_pcs={9}" -f $stat.field, $offset, $stat.loads, $stat.stores, $firstLoadPc, $firstLoadOrdinal, $beforeFirstBranch, $firstStorePc, (Get-TopPairs $stat.load_pcs $Top), (Get-TopPairs $stat.store_pcs $Top))
}
Write-Output ""
Write-Output "## Roundtrip Candidates"
foreach ($offset in $targetOffsets) {
    $stat = $calleeStats[$offset]
    $hasParentStore = $parentStores.ContainsKey($offset)
    $hasCalleeLoad = $stat.loads -gt 0
    $candidate = $hasParentStore -and $hasCalleeLoad
    $kind = "none"
    if ($candidate -and $offset -eq 296) {
        $kind = "direct_fpr_argument_or_callee_local_promotion"
    } elseif ($candidate -and $offset -eq 2628) {
        $kind = "fpscr_state_carrier_or_callee_local_promotion"
    } elseif ($candidate) {
        $kind = "state_roundtrip"
    }
    Write-Output ("candidate field={0} parent_store={1} callee_load={2} kind={3}" -f $stat.field, $hasParentStore, $hasCalleeLoad, $kind)
}
Write-Output ""
Write-Output "## Decision"
Write-Output "decision=no_behavior_patch_yet"
Write-Output "reason=offline evidence proves live parent-to-callee state roundtrips, but changing PPC context visibility across a direct call needs a guarded design and route proof."
Write-Output "next_experiment=design a default-off 82282490->82287788 function-pair audit/probe for f[1]/fpscr carrier or callee-local promotion; do not skip the live stores."
