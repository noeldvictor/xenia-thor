param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [Parameter(Mandatory = $true)]
    [string]$StartGuest,
    [Parameter(Mandatory = $true)]
    [string]$EndGuest,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [string]$BlockProfileLog = "",
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
        [int]$Limit = 8
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
        $xerNames = @("xer_ca", "xer_ov", "xer_so", "xer_pad")
        return $xerNames[$Offset - 2592]
    }
    if ($Offset -ge 2596 -and $Offset -lt 2628) {
        $crIndex = [Math]::Floor(($Offset - 2596) / 4)
        $fieldIndex = ($Offset - 2596) % 4
        $fieldNames = @(
            @("lt", "gt", "eq", "so"),
            @("fx", "fex", "vx", "ox"),
            @("0", "1", "2", "3"),
            @("0", "1", "2", "3"),
            @("0", "1", "2", "3"),
            @("0", "1", "2", "3"),
            @("all_equal", "1", "none_equal", "3"),
            @("0", "1", "2", "3")
        )
        return ("cr{0}.{1}" -f $crIndex, $fieldNames[$crIndex][$fieldIndex])
    }
    if ($Offset -ge 2628 -and $Offset -lt 2632) { return "fpscr" }
    if ($Offset -eq 2632) { return "vscr_sat" }
    if ($Offset -eq 2636) { return "thread_id" }
    if ($Offset -ge 2640) { return "runtime_or_reservation" }
    return "unknown"
}

function Get-ContextOffsetClass {
    param([int]$Offset)
    if ($Offset -eq 16 -or $Offset -eq 24) { return "control" }
    if ($Offset -ge 32 -and $Offset -lt 288) { return "gpr" }
    if ($Offset -ge 288 -and $Offset -lt 544) { return "fpr" }
    if ($Offset -ge 544 -and $Offset -lt 2592) { return "vmx" }
    if ($Offset -ge 2596 -and $Offset -lt 2628) { return "cr" }
    if ($Offset -ge 2628 -and $Offset -lt 2632) { return "fpscr" }
    return "other"
}

function New-ItemRow {
    param(
        [string]$Kind,
        [int]$Ordinal,
        [string]$Address,
        [string]$Op,
        [string]$Text,
        [string]$PpcAddress,
        [string]$PpcOp
    )
    return [pscustomobject][ordered]@{
        kind = $Kind
        ordinal = $Ordinal
        address = $Address
        op = $Op
        text = $Text
        ppc_address = $PpcAddress
        ppc_op = $PpcOp
    }
}

function Read-HirFunction {
    param(
        [string]$Path,
        [string]$FunctionName,
        [string]$DumpPhase
    )

    if (!(Test-Path -LiteralPath $Path)) {
        throw "LogPath not found: $Path"
    }

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $functionUpper = $FunctionName.ToUpperInvariant()
    $linePattern = "Filtered function dump $([Regex]::Escape($functionUpper)) $([Regex]::Escape($DumpPhase))`:\s+(?<text>.*)$"
    $items = New-Object System.Collections.Generic.List[object]
    $ppcByAddress = @{}
    $currentPpcAddress = ""
    $currentPpcOp = ""
    $ordinal = 0

    Get-Content -LiteralPath $resolved | ForEach-Object {
        if ($_ -notmatch $linePattern) {
            return
        }
        $text = $Matches.text
        if ($text -match "^\s*<entry>:") {
            return
        }
        if ($text -match "^\s*(loc_[0-9A-Fa-f]+|_label[0-9A-Fa-f]+):") {
            $items.Add((New-ItemRow "label" $ordinal "" "" $text.Trim() $currentPpcAddress $currentPpcOp)) | Out-Null
            $ordinal += 1
            return
        }
        if ($text -match "^\s*;\s+(?<addr>[0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+(?<op>[A-Za-z0-9_\.]+)\s*(?<args>.*)$") {
            $currentPpcAddress = $Matches.addr.ToUpperInvariant()
            $currentPpcOp = $Matches.op.ToLowerInvariant()
            $item = New-ItemRow "ppc" $ordinal $currentPpcAddress $currentPpcOp $text.Trim() $currentPpcAddress $currentPpcOp
            $items.Add($item) | Out-Null
            if (!$ppcByAddress.ContainsKey($currentPpcAddress)) {
                $ppcByAddress[$currentPpcAddress] = $item
            }
            $ordinal += 1
            return
        }
        if ($text -match "^\s*;\s*(in:|out:|default|$)" -or [string]::IsNullOrWhiteSpace($text)) {
            return
        }

        $trimmed = $text.Trim()
        $op = ""
        if ($trimmed -match "^\S+\s*=\s*(?<op>[A-Za-z0-9_\.]+)\b") {
            $op = $Matches.op.ToLowerInvariant()
        } elseif ($trimmed -match "^(?<op>[A-Za-z0-9_\.]+)\b") {
            $op = $Matches.op.ToLowerInvariant()
        }
        if ([string]::IsNullOrWhiteSpace($op)) {
            return
        }
        $items.Add((New-ItemRow "hir" $ordinal "" $op $trimmed $currentPpcAddress $currentPpcOp)) | Out-Null
        $ordinal += 1
    }

    if ($items.Count -eq 0) {
        throw "No $DumpPhase filtered dump for function $functionUpper found in $resolved."
    }

    return [pscustomobject][ordered]@{
        path = $resolved
        function = $functionUpper
        items = $items
        ppc_by_address = $ppcByAddress
    }
}

function Get-ContextAccesses {
    param([object[]]$Items)

    $rows = New-Object System.Collections.Generic.List[object]
    foreach ($row in $Items) {
        if ($row.kind -ne "hir") {
            continue
        }
        $kind = ""
        $offset = -1
        $value = ""
        if ($row.text -match "\bload_context\s+\+(?<offset>\d+)") {
            $kind = "load"
            $offset = [int]$Matches.offset
            if ($row.text -match "^(?<value>v[0-9]+)\.") {
                $value = $Matches.value
            }
        } elseif ($row.text -match "\bstore_context\s+\+(?<offset>\d+),\s*(?<value>\S+)") {
            $kind = "store"
            $offset = [int]$Matches.offset
            $value = $Matches.value -replace "\..*$", ""
        } else {
            continue
        }
        $rows.Add([pscustomobject][ordered]@{
            kind = $kind
            ordinal = [int]$row.ordinal
            ppc_address = $row.ppc_address
            ppc_op = $row.ppc_op
            offset = $offset
            name = Get-ContextOffsetName $offset
            class = Get-ContextOffsetClass $offset
            value = $value
            text = $row.text
        }) | Out-Null
    }
    return $rows
}

function Test-HasControlBoundary {
    param(
        [object[]]$Items,
        [int]$StartOrdinal,
        [int]$EndOrdinal
    )
    foreach ($item in $Items) {
        $ordinal = [int]$item.ordinal
        if ($ordinal -le $StartOrdinal -or $ordinal -ge $EndOrdinal) {
            continue
        }
        if ($item.op -eq "context_barrier" -or $item.op -like "branch*" -or $item.op -like "call*") {
            return $true
        }
    }
    return $false
}

$dump = Read-HirFunction -Path $LogPath -FunctionName $Function -DumpPhase $Phase
$functionUpper = $Function.ToUpperInvariant()
$startUpper = $StartGuest.ToUpperInvariant()
$endUpper = $EndGuest.ToUpperInvariant()

if (!$dump.ppc_by_address.ContainsKey($startUpper)) {
    throw "StartGuest $startUpper not found in $Phase dump for $functionUpper."
}
if (!$dump.ppc_by_address.ContainsKey($endUpper)) {
    throw "EndGuest $endUpper not found in $Phase dump for $functionUpper."
}

$startOrdinal = [int]$dump.ppc_by_address[$startUpper].ordinal
$endOrdinal = [int]$dump.ppc_by_address[$endUpper].ordinal
$slice = @($dump.items | Where-Object { [int]$_.ordinal -ge $startOrdinal -and [int]$_.ordinal -lt $endOrdinal })
$ppc = @($slice | Where-Object { $_.kind -eq "ppc" })
$hir = @($slice | Where-Object { $_.kind -eq "hir" })
$accesses = @(Get-ContextAccesses $slice)

$ppcOps = @{}
$hirOps = @{}
$loadNames = @{}
$storeNames = @{}
$classLoads = @{}
$classStores = @{}
foreach ($row in $ppc) { Add-Count $ppcOps $row.op }
foreach ($row in $hir) { Add-Count $hirOps $row.op }
foreach ($access in $accesses) {
    if ($access.kind -eq "load") {
        Add-Count $loadNames $access.name
        Add-Count $classLoads $access.class
    } else {
        Add-Count $storeNames $access.name
        Add-Count $classStores $access.class
    }
}

$branchRows = New-Object System.Collections.Generic.List[object]
$directPredicateStores = 0
foreach ($branch in @($hir | Where-Object { $_.op -like "branch*" })) {
    $predicate = ""
    $target = ""
    if ($branch.text -match "^(?<op>branch_true|branch_false)\s+(?<pred>v[0-9]+)\.[^,]+,\s*(?<target>\S+)") {
        $predicate = $Matches.pred
        $target = $Matches.target
    } elseif ($branch.text -match "^(?<op>branch)\s+(?<target>\S+)") {
        $target = $Matches.target
    }
    $producer = $null
    if (![string]::IsNullOrWhiteSpace($predicate)) {
        $producer = $accesses |
            Where-Object {
                $_.kind -eq "store" -and
                $_.value -eq $predicate -and
                [int]$_.ordinal -lt [int]$branch.ordinal
            } |
            Sort-Object -Property ordinal -Descending |
            Select-Object -First 1
    }
    if ($null -ne $producer) {
        $directPredicateStores += 1
    }
    $branchRows.Add([pscustomobject][ordered]@{
        ppc = $branch.ppc_address
        ppc_op = $branch.ppc_op
        op = $branch.op
        predicate = $predicate
        target = $target
        predicate_store = if ($null -eq $producer) { "-" } else { ("{0}@{1}" -f $producer.name, $producer.ppc_address) }
        note = if ($null -eq $producer) { "no_context_predicate_store" } else { "predicate_also_stored_to_context_before_branch" }
    }) | Out-Null
}

$reloadRows = New-Object System.Collections.Generic.List[object]
foreach ($load in @($accesses | Where-Object { $_.kind -eq "load" })) {
    $previousSameStore = $accesses |
        Where-Object {
            $_.kind -eq "store" -and
            $_.offset -eq $load.offset -and
            [int]$_.ordinal -lt [int]$load.ordinal
        } |
        Sort-Object -Property ordinal -Descending |
        Select-Object -First 1
    $previousSameLoad = $accesses |
        Where-Object {
            $_.kind -eq "load" -and
            $_.offset -eq $load.offset -and
            [int]$_.ordinal -lt [int]$load.ordinal
        } |
        Sort-Object -Property ordinal -Descending |
        Select-Object -First 1
    if ($null -eq $previousSameStore -and $null -eq $previousSameLoad) {
        continue
    }
    $source = if ($null -ne $previousSameStore) { $previousSameStore } else { $previousSameLoad }
    $crossesBoundary = Test-HasControlBoundary -Items $slice -StartOrdinal ([int]$source.ordinal) -EndOrdinal ([int]$load.ordinal)
    $reloadRows.Add([pscustomobject][ordered]@{
        name = $load.name
        class = $load.class
        load_ppc = $load.ppc_address
        source_kind = $source.kind
        source_ppc = $source.ppc_address
        crosses_barrier_or_branch = $crossesBoundary
        decision = if ($crossesBoundary) { "fallthrough_carrier_only" } else { "local_redundant_load" }
    }) | Out-Null
}

$profile = $null
if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    if (!(Test-Path -LiteralPath $BlockProfileLog)) {
        throw "BlockProfileLog not found: $BlockProfileLog"
    }
    $profilePattern = "A64 speed profile block body top \d+: fn $([Regex]::Escape($functionUpper)) .* block=(?<block>\d+) guest=$([Regex]::Escape($startUpper)) body_ticks_delta=(?<delta>\d+) body_ticks_total=(?<total>\d+) entries_delta=(?<entries>\d+) ticks_per_entry=(?<tpe>\d+)"
    foreach ($line in Get-Content -LiteralPath (Resolve-Path -LiteralPath $BlockProfileLog).Path) {
        if ($line -match $profilePattern) {
            $total = [int64]$Matches.total
            if ($null -eq $profile -or $total -ge [int64]$profile.body_ticks_total) {
                $profile = [pscustomobject][ordered]@{
                    block = [int]$Matches.block
                    body_ticks_total = $total
                    body_ticks_delta = [int64]$Matches.delta
                    entries_delta = [int64]$Matches.entries
                    ticks_per_entry = [int64]$Matches.tpe
                }
            }
        }
    }
}

$crStores = @($accesses | Where-Object { $_.kind -eq "store" -and $_.class -eq "cr" })
$gprLoads = @($accesses | Where-Object { $_.kind -eq "load" -and $_.class -eq "gpr" })
$gprReloadsAcrossBoundary = @($reloadRows | Where-Object { $_.class -eq "gpr" -and $_.crosses_barrier_or_branch })
$crPredicateStoreRows = @($branchRows | Where-Object { $_.predicate_store -like "cr*" })

$decision = "no_codegen_patch_from_this_audit"
if ($gprReloadsAcrossBoundary.Count -gt 0 -and $crPredicateStoreRows.Count -eq 0) {
    $decision = "possible_local_gpr_carrier_probe"
} elseif ($gprReloadsAcrossBoundary.Count -gt 0) {
    $decision = "gpr_carrier_tiny_cr_store_lane_blocked"
}

Write-Output "# HIR Branch State Audit"
Write-Output ""
Write-Output ("log={0}" -f $dump.path)
if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    Write-Output ("block_profile_log={0}" -f (Resolve-Path -LiteralPath $BlockProfileLog).Path)
}
Write-Output ("function={0}" -f $functionUpper)
Write-Output ("phase={0}" -f $Phase)
Write-Output ("slice={0}-{1}" -f $startUpper, $endUpper)
if ($null -ne $profile) {
    Write-Output ("body_ticks_total={0} body_ticks_delta={1} entries_delta={2} ticks_per_entry={3}" -f $profile.body_ticks_total, $profile.body_ticks_delta, $profile.entries_delta, $profile.ticks_per_entry)
}
Write-Output ("ppc_count={0} hir_count={1}" -f $ppc.Count, $hir.Count)
Write-Output ("ppc_ops={0}" -f (Get-TopPairs $ppcOps $Top))
Write-Output ("hir_ops={0}" -f (Get-TopPairs $hirOps $Top))
Write-Output ("context_loads={0}" -f (Get-TopPairs $loadNames $Top))
Write-Output ("context_stores={0}" -f (Get-TopPairs $storeNames $Top))
Write-Output ("class_loads={0}" -f (Get-TopPairs $classLoads $Top))
Write-Output ("class_stores={0}" -f (Get-TopPairs $classStores $Top))
Write-Output ""
Write-Output "## Branch Predicate Stores"
Write-Output ("branches={0} direct_predicate_context_stores={1} cr_predicate_context_stores={2}" -f $branchRows.Count, $directPredicateStores, $crPredicateStoreRows.Count)
foreach ($row in $branchRows) {
    Write-Output ("branch ppc={0} op={1} predicate={2} target={3} predicate_store={4} note={5}" -f $row.ppc, $row.op, $row.predicate, $row.target, $row.predicate_store, $row.note)
}
Write-Output ""
Write-Output "## Reload Opportunities"
Write-Output ("reloads_after_same_context_access={0} gpr_reload_cross_boundary={1}" -f $reloadRows.Count, $gprReloadsAcrossBoundary.Count)
foreach ($row in ($reloadRows | Select-Object -First $Top)) {
    Write-Output ("reload name={0} class={1} load_ppc={2} source={3}@{4} crosses_boundary={5} decision={6}" -f $row.name, $row.class, $row.load_ppc, $row.source_kind, $row.source_ppc, $row.crosses_barrier_or_branch, $row.decision)
}
Write-Output ""
Write-Output "## Decision"
Write-Output ("decision={0}" -f $decision)
Write-Output "notes=CR predicate stores are directly consumed by local branches, but they also cross context_barrier/branch visibility and broad CR-store/compare fusion is a known negative lane for this title. GPR reloads are fallthrough-carrier-only and small in this span."
