param(
    [Parameter(Mandatory = $true)]
    [string]$ParentLogPath,
    [Parameter(Mandatory = $true)]
    [string]$ParentFunction,
    [Parameter(Mandatory = $true)]
    [string]$ParentStartGuest,
    [string]$ParentEndGuest = "",
    [Parameter(Mandatory = $true)]
    [string]$CalleeLogPath,
    [Parameter(Mandatory = $true)]
    [string]$CalleeFunction,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [int]$Top = 24
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

function Get-ApproxStateBytes {
    param([int]$Offset)
    $class = Get-ContextOffsetClass $Offset
    switch ($class) {
        "vmx" { return 16 }
        "gpr" { return 8 }
        "fpr" { return 8 }
        "control" { return 8 }
        "cr" { return 1 }
        "fpscr" { return 4 }
        default { return 4 }
    }
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
        [string]$LogPath,
        [string]$Function,
        [string]$Phase
    )

    if (!(Test-Path -LiteralPath $LogPath)) {
        throw "LogPath not found: $LogPath"
    }
    $resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
    $functionUpper = $Function.ToUpperInvariant()
    $functionPattern = [Regex]::Escape($functionUpper)
    $phasePattern = [Regex]::Escape($Phase)
    $linePattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"

    $items = New-Object System.Collections.Generic.List[object]
    $ppcByAddress = @{}
    $currentPpcAddress = ""
    $currentPpcOp = ""
    $ordinal = 0

    Get-Content -LiteralPath $resolvedLog | ForEach-Object {
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
        throw "No $Phase filtered dump for function $functionUpper found in $resolvedLog."
    }

    return [pscustomobject][ordered]@{
        log = $resolvedLog
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
        if ($row.text -match "\bload_context\s+\+(?<offset>\d+)") {
            $kind = "load"
            $offset = [int]$Matches.offset
        } elseif ($row.text -match "\bstore_context\s+\+(?<offset>\d+)") {
            $kind = "store"
            $offset = [int]$Matches.offset
        } else {
            continue
        }
        $rows.Add([pscustomobject][ordered]@{
            kind = $kind
            ordinal = [int]$row.ordinal
            offset = $offset
            name = Get-ContextOffsetName $offset
            class = Get-ContextOffsetClass $offset
            approx_bytes = Get-ApproxStateBytes $offset
            ppc_address = $row.ppc_address
            ppc_op = $row.ppc_op
            text = $row.text
        }) | Out-Null
    }
    return $rows
}

function Get-FirstAccessByOffset {
    param([object[]]$Accesses)

    $table = @{}
    foreach ($access in ($Accesses | Sort-Object -Property ordinal)) {
        $key = [int]$access.offset
        if (!$table.ContainsKey($key)) {
            $table[$key] = $access
        }
    }
    return $table
}

$parent = Read-HirFunction -LogPath $ParentLogPath -Function $ParentFunction -Phase $Phase
$callee = Read-HirFunction -LogPath $CalleeLogPath -Function $CalleeFunction -Phase $Phase

$parentStartUpper = $ParentStartGuest.ToUpperInvariant()
$parentEndUpper = $ParentEndGuest.ToUpperInvariant()
$calleeFunctionUpper = $CalleeFunction.ToUpperInvariant()
if (!$parent.ppc_by_address.ContainsKey($parentStartUpper)) {
    throw "ParentStartGuest $parentStartUpper not found in $Phase dump for $($parent.function)."
}

$parentStartOrdinal = [int]$parent.ppc_by_address[$parentStartUpper].ordinal
$parentEndOrdinal = $parent.items.Count
if (![string]::IsNullOrWhiteSpace($parentEndUpper)) {
    if (!$parent.ppc_by_address.ContainsKey($parentEndUpper)) {
        throw "ParentEndGuest $parentEndUpper not found in $Phase dump for $($parent.function)."
    }
    $parentEndPpcOrdinal = [int]$parent.ppc_by_address[$parentEndUpper].ordinal
    $nextPpc = $parent.items |
        Where-Object { $_.kind -eq "ppc" -and [int]$_.ordinal -gt $parentEndPpcOrdinal } |
        Sort-Object -Property ordinal |
        Select-Object -First 1
    if ($null -ne $nextPpc) {
        $parentEndOrdinal = [int]$nextPpc.ordinal
    }
}

$parentSlice = $parent.items |
    Where-Object { [int]$_.ordinal -ge $parentStartOrdinal -and [int]$_.ordinal -lt $parentEndOrdinal }

$directCallPpc = $parentSlice |
    Where-Object {
        $_.kind -eq "ppc" -and $_.op -eq "bl" -and
        $_.text -match ("0x" + [Regex]::Escape($calleeFunctionUpper))
    } |
    Sort-Object -Property ordinal |
    Select-Object -First 1

if ($null -eq $directCallPpc) {
    throw "No direct parent bl to 0x$calleeFunctionUpper found in requested slice."
}

$callHir = $parentSlice |
    Where-Object { $_.kind -eq "hir" -and [int]$_.ordinal -gt [int]$directCallPpc.ordinal -and $_.op -like "call*" } |
    Sort-Object -Property ordinal |
    Select-Object -First 1
if ($null -eq $callHir) {
    throw "No HIR call after parent direct call $($directCallPpc.address)."
}

$parentPreCall = $parentSlice |
    Where-Object { [int]$_.ordinal -le [int]$callHir.ordinal }
$parentAfterCallFull = $parent.items |
    Where-Object { [int]$_.ordinal -gt [int]$callHir.ordinal }

$parentPreAccesses = @(Get-ContextAccesses $parentPreCall)
$parentPreStores = @($parentPreAccesses | Where-Object { $_.kind -eq "store" })
$calleeAccesses = @(Get-ContextAccesses $callee.items)
$parentAfterAccesses = @(Get-ContextAccesses $parentAfterCallFull)
$calleeFirst = Get-FirstAccessByOffset $calleeAccesses
$parentAfterFirst = Get-FirstAccessByOffset $parentAfterAccesses

$calleeLoadCounts = @{}
$calleeStoreCounts = @{}
foreach ($access in $calleeAccesses) {
    $key = "{0}:{1}" -f $access.name, $access.offset
    if ($access.kind -eq "load") {
        Add-Count $calleeLoadCounts $key
    } else {
        Add-Count $calleeStoreCounts $key
    }
}

$classificationRows = New-Object System.Collections.Generic.List[object]
$classCounts = @{}
$classBytes = @{}
$storeCounts = @{}

foreach ($store in $parentPreStores) {
    $offset = [int]$store.offset
    $calleeFirstAccess = $null
    if ($calleeFirst.ContainsKey($offset)) {
        $calleeFirstAccess = $calleeFirst[$offset]
    }
    $parentAfterFirstAccess = $null
    if ($parentAfterFirst.ContainsKey($offset)) {
        $parentAfterFirstAccess = $parentAfterFirst[$offset]
    }

    $calleeFirstKind = "absent"
    $calleeFirstPpc = "-"
    if ($null -ne $calleeFirstAccess) {
        $calleeFirstKind = $calleeFirstAccess.kind
        $calleeFirstPpc = "{0}:{1}" -f $calleeFirstAccess.ppc_address, $calleeFirstAccess.ppc_op
    }
    $parentAfterFirstKind = "absent"
    $parentAfterFirstPpc = "-"
    if ($null -ne $parentAfterFirstAccess) {
        $parentAfterFirstKind = $parentAfterFirstAccess.kind
        $parentAfterFirstPpc = "{0}:{1}" -f $parentAfterFirstAccess.ppc_address, $parentAfterFirstAccess.ppc_op
    }

    $classification = "callee_dead_parent_unknown"
    if ($calleeFirstKind -eq "load") {
        $classification = "callee_live_in"
    } elseif ($parentAfterFirstKind -eq "load") {
        $classification = "parent_reads_after_call"
    } elseif ($calleeFirstKind -eq "store" -or $calleeFirstKind -eq "absent") {
        if ($parentAfterFirstKind -eq "store" -or $parentAfterFirstKind -eq "absent") {
            $classification = "callee_dead_parent_dead_linear"
        }
    }

    Add-Count $classCounts $classification
    Add-Count $classBytes $classification ([int64]$store.approx_bytes)
    Add-Count $storeCounts ("{0}:{1}" -f $store.name, $store.ppc_address)

    $classificationRows.Add([pscustomobject][ordered]@{
        parent_store = ("{0}:{1}" -f $store.ppc_address, $store.ppc_op)
        name = $store.name
        offset = $offset
        state_class = $store.class
        approx_bytes = $store.approx_bytes
        callee_first = $calleeFirstKind
        callee_first_pc = $calleeFirstPpc
        parent_after_first = $parentAfterFirstKind
        parent_after_first_pc = $parentAfterFirstPpc
        classification = $classification
    }) | Out-Null
}

$preStoreByClass = @{}
$preStoreBytesByClass = @{}
foreach ($store in $parentPreStores) {
    Add-Count $preStoreByClass $store.class
    Add-Count $preStoreBytesByClass $store.class ([int64]$store.approx_bytes)
}

Write-Output "# HIR Call Boundary State Audit"
Write-Output ""
Write-Output ("parent_log={0}" -f $parent.log)
Write-Output ("callee_log={0}" -f $callee.log)
Write-Output ("phase={0}" -f $Phase)
Write-Output ("parent_function={0}" -f $parent.function)
Write-Output ("parent_slice={0}-{1}" -f $parentStartUpper, $(if ([string]::IsNullOrWhiteSpace($parentEndUpper)) { "slice_end" } else { $parentEndUpper }))
Write-Output ("callee_function={0}" -f $callee.function)
Write-Output ("direct_call={0}" -f $directCallPpc.text)
Write-Output ("hir_call={0}" -f $callHir.text)
Write-Output ("parent_pre_call_store_context={0} approx_state_bytes={1} by_class={2} bytes_by_class={3}" -f $parentPreStores.Count, (($parentPreStores | Measure-Object -Property approx_bytes -Sum).Sum), (Get-TopPairs $preStoreByClass $Top), (Get-TopPairs $preStoreBytesByClass $Top))
Write-Output ("callee_context_loads={0} callee_context_stores={1} callee_loads={2} callee_stores={3}" -f (($calleeAccesses | Where-Object { $_.kind -eq "load" }).Count), (($calleeAccesses | Where-Object { $_.kind -eq "store" }).Count), (Get-TopPairs $calleeLoadCounts $Top), (Get-TopPairs $calleeStoreCounts $Top))
Write-Output ("classification_counts={0}" -f (Get-TopPairs $classCounts $Top))
Write-Output ("classification_approx_bytes={0}" -f (Get-TopPairs $classBytes $Top))
Write-Output ""
Write-Output "## Parent Pre-Call Stores"
foreach ($row in ($classificationRows | Sort-Object -Property @{ Expression = "classification"; Ascending = $true }, @{ Expression = "parent_store"; Ascending = $true } | Select-Object -First $Top)) {
    Write-Output ("store={0} field={1} offset={2} class={3} bytes={4} callee_first={5}@{6} parent_after_first={7}@{8} classification={9}" -f $row.parent_store, $row.name, $row.offset, $row.state_class, $row.approx_bytes, $row.callee_first, $row.callee_first_pc, $row.parent_after_first, $row.parent_after_first_pc, $row.classification)
}
Write-Output ""
Write-Output "## Candidate Read"
Write-Output "classification_note=This is a linear HIR/context dependency audit, not a proof that stores can be removed. Branches, exceptions, debug stops, and helper side effects still need an experiment gate."
Write-Output "callee_live_in=parent store likely feeds the direct child and is not a store-elision target without a new call ABI."
Write-Output "parent_reads_after_call=store is not live into the callee but later parent code reads the field before overwriting it; a local state cache or call-preserved carrier would be needed."
Write-Output "callee_dead_parent_dead_linear=callee does not load the field before writing it or never touches it, and the parent linear tail does not read it before overwrite/absence; these are the best candidates for a future default-off store-suppression audit."
Write-Output "next_lane=if callee_dead_parent_dead_linear is VMX/FPR-heavy, design a default-off function/span/call-gated store-suppression counter first; otherwise continue with broader call-boundary state handoff research, not another narrow vector peephole."
