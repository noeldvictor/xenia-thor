param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [string[]]$ExtraLogPath = @(),
    [string[]]$FastEntryAuditLogPath = @(),
    [string]$Function = "",
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

function Get-HashValue {
    param(
        [hashtable]$Table,
        [string]$Key,
        [string]$Default = "0"
    )
    if ($Table.ContainsKey($Key)) {
        return [string]$Table[$Key]
    }
    return $Default
}

function Get-ContextOffsetName {
    param([int]$Offset)

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
    if ($Offset -ge 2628 -and $Offset -lt 2632) { return "fpscr" }
    return ("ctx+{0}" -f $Offset)
}

function Get-ContextOffsetClass {
    param([int]$Offset)
    if ($Offset -eq 16 -or $Offset -eq 24) { return "control" }
    if ($Offset -ge 32 -and $Offset -lt 288) { return "gpr" }
    if ($Offset -ge 288 -and $Offset -lt 544) { return "fpr" }
    if ($Offset -ge 544 -and $Offset -lt 2592) { return "vmx" }
    if ($Offset -ge 2628 -and $Offset -lt 2632) { return "fpscr" }
    return "other"
}

function Get-ApproxStateBytes {
    param([int]$Offset)
    switch (Get-ContextOffsetClass $Offset) {
        "vmx" { return 16 }
        "gpr" { return 8 }
        "fpr" { return 8 }
        "control" { return 8 }
        "fpscr" { return 4 }
        default { return 4 }
    }
}

function Test-ArgumentOffset {
    param([int]$Offset)
    if ($Offset -eq 16) { return $true }
    return ($Offset -ge 56 -and $Offset -le 112 -and (($Offset - 32) % 8) -eq 0)
}

function New-ItemRow {
    param(
        [string]$Kind,
        [int]$Ordinal,
        [string]$Text,
        [string]$Op,
        [string]$PpcAddress,
        [string]$PpcOp,
        [string]$PpcTarget,
        [object]$Block
    )
    return [pscustomobject][ordered]@{
        kind = $Kind
        ordinal = $Ordinal
        text = $Text
        op = $Op
        ppc_address = $PpcAddress
        ppc_op = $PpcOp
        ppc_target = $PpcTarget
        block = $Block
    }
}

function New-BlockRow {
    param(
        [int]$Ordinal,
        [string]$FirstSource,
        [string]$LastSource,
        [string]$FirstGuest,
        [string]$LastGuest,
        [string]$Label,
        [int]$HirInstrs
    )
    return [pscustomobject][ordered]@{
        ordinal = $Ordinal
        first_source = $FirstSource
        last_source = $LastSource
        first_guest = $FirstGuest
        last_guest = $LastGuest
        label = $Label
        hir_instrs = $HirInstrs
    }
}

function Read-HirLogs {
    param([string[]]$Paths, [string]$Phase)

    $functions = @{}
    $phasePattern = [Regex]::Escape($Phase)
    $linePattern = "Filtered function dump (?<function>[0-9A-Fa-f]{8}) $phasePattern`:\s+(?<text>.*)$"
    foreach ($path in $Paths) {
        if (!(Test-Path -LiteralPath $path)) {
            throw "LogPath not found: $path"
        }
        $resolved = (Resolve-Path -LiteralPath $path).Path
        $currentPpcAddress = ""
        $currentPpcOp = ""
        $currentPpcTarget = ""
        Get-Content -LiteralPath $resolved | ForEach-Object {
            if ($_ -notmatch $linePattern) {
                return
            }
            $function = $Matches.function.ToUpperInvariant()
            $text = $Matches.text
            if (!$functions.ContainsKey($function)) {
                $functions[$function] = [pscustomobject][ordered]@{
                    function = $function
                    log = $resolved
                    items = New-Object System.Collections.Generic.List[object]
                    ppc_by_address = @{}
                    blocks = New-Object System.Collections.Generic.List[object]
                    block_profile = @{}
                }
            }
            $fn = $functions[$function]
            $ordinal = $fn.items.Count
            $currentBlock = if ($fn.blocks.Count -gt 0) { $fn.blocks[$fn.blocks.Count - 1] } else { $null }

            if ($text -match "^\s*<entry>:") {
                return
            }
            if ($text -match "^\s*;\s+block_profile\s+ordinal=(?<block>\d+)\s+first_source=(?<first_source>[0-9A-Fa-f]{8})\s+last_source=(?<last_source>[0-9A-Fa-f]{8})\s+first_guest=(?<first_guest>[0-9A-Fa-f]{8})\s+last_guest=(?<last_guest>[0-9A-Fa-f]{8})\s+first_comment=(?<first_comment>[0-9A-Fa-f]{8})\s+last_comment=(?<last_comment>[0-9A-Fa-f]{8})\s+label=(?<label>[0-9A-Fa-f]{8})\s+hir_instrs=(?<hir_instrs>\d+)") {
                $block = New-BlockRow `
                    -Ordinal ([int]$Matches.block) `
                    -FirstSource $Matches.first_source.ToUpperInvariant() `
                    -LastSource $Matches.last_source.ToUpperInvariant() `
                    -FirstGuest $Matches.first_guest.ToUpperInvariant() `
                    -LastGuest $Matches.last_guest.ToUpperInvariant() `
                    -Label $Matches.label.ToUpperInvariant() `
                    -HirInstrs ([int]$Matches.hir_instrs)
                $fn.blocks.Add($block) | Out-Null
                return
            }
            if ($text -match "^\s*(loc_[0-9A-Fa-f]+|_label[0-9A-Fa-f]+):") {
                $fn.items.Add((New-ItemRow "label" $ordinal $text.Trim() "" $currentPpcAddress $currentPpcOp $currentPpcTarget $currentBlock)) | Out-Null
                return
            }
            if ($text -match "^\s*;\s+(?<addr>[0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+(?<op>[A-Za-z0-9_\.]+)\s*(?<args>.*)$") {
                $currentPpcAddress = $Matches.addr.ToUpperInvariant()
                $currentPpcOp = $Matches.op.ToLowerInvariant()
                $currentPpcTarget = ""
                if ($currentPpcOp -eq "bl" -and $Matches.args -match "0x(?<target>[0-9A-Fa-f]{8})") {
                    $currentPpcTarget = $Matches.target.ToUpperInvariant()
                }
                $item = New-ItemRow "ppc" $ordinal $text.Trim() $currentPpcOp $currentPpcAddress $currentPpcOp $currentPpcTarget $currentBlock
                $fn.items.Add($item) | Out-Null
                if (!$fn.ppc_by_address.ContainsKey($currentPpcAddress)) {
                    $fn.ppc_by_address[$currentPpcAddress] = $item
                }
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
            $fn.items.Add((New-ItemRow "hir" $ordinal $trimmed $op $currentPpcAddress $currentPpcOp $currentPpcTarget $currentBlock)) | Out-Null
        }
    }
    return $functions
}

function Read-BlockProfileRows {
    param([string[]]$Paths)
    $table = @{}
    $pattern = "A64 speed profile block body top\s+\d+:\s+fn\s+(?<function>[0-9A-Fa-f]{8})\s+'[^']*'\s+block=(?<block>\d+)\s+guest=(?<guest>[0-9A-Fa-f]{8})\s+body_ticks_delta=(?<body_delta>\d+)\s+body_ticks_total=(?<body_total>\d+)\s+entries_delta=(?<entries_delta>\d+)\s+ticks_per_entry=(?<tpe>\d+)"
    foreach ($path in $Paths) {
        if (!(Test-Path -LiteralPath $path)) {
            continue
        }
        $resolved = (Resolve-Path -LiteralPath $path).Path
        Get-Content -LiteralPath $resolved | ForEach-Object {
            if ($_ -notmatch $pattern) {
                return
            }
            $function = $Matches.function.ToUpperInvariant()
            $block = [int]$Matches.block
            $key = "{0}:{1}" -f $function, $block
            $table[$key] = [pscustomobject][ordered]@{
                function = $function
                block = $block
                guest = $Matches.guest.ToUpperInvariant()
                body_ticks_delta = [int64]$Matches.body_delta
                body_ticks_total = [int64]$Matches.body_total
                entries_delta = [int64]$Matches.entries_delta
                ticks_per_entry = [int64]$Matches.tpe
                log = $resolved
            }
        }
    }
    return $table
}

function Read-FastEntryAuditRows {
    param([string[]]$Paths)
    $rows = New-Object System.Collections.Generic.List[object]
    $pattern = "A64 guest-call fast-entry audit\s+(?<index>\d+):\s+fn\s+(?<function>[0-9A-Fa-f]{8})\s+(?<rest>.*)$"
    foreach ($path in $Paths) {
        if (!(Test-Path -LiteralPath $path)) {
            throw "FastEntryAuditLogPath not found: $path"
        }
        $resolved = (Resolve-Path -LiteralPath $path).Path
        foreach ($line in (Get-Content -LiteralPath $resolved)) {
            if ($line -notmatch $pattern) {
                continue
            }
            $rowIndex = [int]$Matches.index
            $rowFunction = $Matches.function.ToUpperInvariant()
            $rowRest = $Matches.rest
            $values = @{}
            foreach ($token in ($rowRest -split "\s+")) {
                if ($token -match "^(?<key>[A-Za-z0-9_]+)=(?<value>.*)$") {
                    $values[$Matches.key] = $Matches.value
                }
            }
            $rows.Add([pscustomobject][ordered]@{
                function = $rowFunction
                index = $rowIndex
                values = $values
                log = $resolved
                text = $line
            }) | Out-Null
        }
    }
    return $rows
}

function Read-FastEntryAuditTargetRows {
    param([string[]]$Paths)
    $rows = New-Object System.Collections.Generic.List[object]
    $pattern = "A64 guest-call fast-entry target\s+(?<index>\d+)\.(?<row>\d+):\s+fn\s+(?<function>[0-9A-Fa-f]{8})\s+target=(?<target>[0-9A-Fa-f]{8})\s+(?<rest>.*)$"
    foreach ($path in $Paths) {
        if (!(Test-Path -LiteralPath $path)) {
            throw "FastEntryAuditLogPath not found: $path"
        }
        $resolved = (Resolve-Path -LiteralPath $path).Path
        foreach ($line in (Get-Content -LiteralPath $resolved)) {
            if ($line -notmatch $pattern) {
                continue
            }
            $rowIndex = [int]$Matches.index
            $rowNumber = [int]$Matches.row
            $rowFunction = $Matches.function.ToUpperInvariant()
            $rowTarget = $Matches.target.ToUpperInvariant()
            $rowRest = $Matches.rest
            $values = @{}
            foreach ($token in ($rowRest -split "\s+")) {
                if ($token -match "^(?<key>[A-Za-z0-9_]+)=(?<value>.*)$") {
                    $values[$Matches.key] = $Matches.value
                }
            }
            $rows.Add([pscustomobject][ordered]@{
                function = $rowFunction
                target = $rowTarget
                index = $rowIndex
                row = $rowNumber
                values = $values
                log = $resolved
                text = $line
            }) | Out-Null
        }
    }
    return $rows
}

function New-TargetSummary {
    param([string]$Target)
    return [pscustomobject][ordered]@{
        target = $Target
        calls = 0
        argument_stores = 0
        live_in_stores = 0
        first_store_stores = 0
        absent_stores = 0
        missing_callee_stores = 0
        live_in_bytes = 0
        body_ticks_total_sum = 0L
        body_weighted_argument_fields = 0L
        body_weighted_live_in_fields = 0L
        body_weighted_missing_fields = 0L
        top_call = ""
        top_call_body_ticks = 0L
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

function Get-LastArgumentStoresBeforeCall {
    param([object]$Function, [object]$CallHir, [object]$Block)

    $startOrdinal = 0
    if ($null -ne $Block) {
        $blockItems = @($Function.items | Where-Object { $_.block -eq $Block })
        if ($blockItems.Count -gt 0) {
            $startOrdinal = [int]($blockItems | Sort-Object -Property ordinal | Select-Object -First 1).ordinal
        }
    }
    $stores = @{}
    foreach ($row in ($Function.items | Where-Object { [int]$_.ordinal -ge $startOrdinal -and [int]$_.ordinal -lt [int]$CallHir.ordinal })) {
        if ($row.kind -eq "hir" -and $row.text -match "\bstore_context\s+\+(?<offset>\d+)") {
            $offset = [int]$Matches.offset
            if (Test-ArgumentOffset $offset) {
                $stores[$offset] = [pscustomobject][ordered]@{
                    offset = $offset
                    name = Get-ContextOffsetName $offset
                    class = Get-ContextOffsetClass $offset
                    approx_bytes = Get-ApproxStateBytes $offset
                    ppc_address = $row.ppc_address
                    ppc_op = $row.ppc_op
                    text = $row.text
                    ordinal = [int]$row.ordinal
                }
            }
        }
    }
    return @($stores.Values | Sort-Object -Property offset)
}

$paths = @($LogPath) + $ExtraLogPath
$callerFunctions = Read-HirLogs -Paths @($LogPath) -Phase $Phase
$functions = Read-HirLogs -Paths $paths -Phase $Phase
$blockProfiles = Read-BlockProfileRows -Paths @($LogPath)
$fastEntryAuditRows = @()
$fastEntryAuditTargetRows = @()
if ($FastEntryAuditLogPath.Count -gt 0) {
    $fastEntryAuditRows = @(Read-FastEntryAuditRows -Paths $FastEntryAuditLogPath)
    $fastEntryAuditTargetRows = @(Read-FastEntryAuditTargetRows -Paths $FastEntryAuditLogPath)
}

$functionFilter = $Function.ToUpperInvariant()
$selectedFunctions = @($callerFunctions.Values | Where-Object {
    [string]::IsNullOrWhiteSpace($functionFilter) -or $_.function -eq $functionFilter
} | Sort-Object -Property function)

if ($selectedFunctions.Count -eq 0) {
    throw "No $Phase filtered dump matched Function='$Function'."
}

$calleeFirstAccess = @{}
foreach ($fn in $functions.Values) {
    $calleeFirstAccess[$fn.function] = Get-FirstAccessByOffset @(Get-ContextAccesses $fn.items)
}

$rows = New-Object System.Collections.Generic.List[object]
$classCounts = @{}
$fieldCounts = @{}
$missingTargetCounts = @{}
$calleeTargetCounts = @{}
$targetSummaries = @{}
$totalCalls = 0
$callsWithArgumentStores = 0
$totalArgumentStores = 0
$totalArgumentBytes = 0
$liveInStores = 0
$liveInBytes = 0
$missingCalleeStores = 0
$firstStoreStores = 0
$absentStores = 0
$bodyWeightedArgumentFields = 0L
$bodyWeightedLiveInFields = 0L
$bodyWeightedMissingFields = 0L

foreach ($fn in $selectedFunctions) {
    $directPpcCalls = @($fn.items | Where-Object {
        $_.kind -eq "ppc" -and $_.ppc_op -eq "bl" -and
        -not [string]::IsNullOrWhiteSpace($_.ppc_target)
    })
    foreach ($ppcCall in $directPpcCalls) {
        $callHir = $fn.items |
            Where-Object {
                $_.kind -eq "hir" -and [int]$_.ordinal -gt [int]$ppcCall.ordinal -and
                $_.op -like "call*"
            } |
            Sort-Object -Property ordinal |
            Select-Object -First 1
        if ($null -eq $callHir) {
            continue
        }
        $totalCalls += 1
        $target = $ppcCall.ppc_target
        Add-Count $calleeTargetCounts $target
        $stores = @(Get-LastArgumentStoresBeforeCall -Function $fn -CallHir $callHir -Block $ppcCall.block)
        if ($stores.Count -gt 0) {
            $callsWithArgumentStores += 1
        }
        if (!$targetSummaries.ContainsKey($target)) {
            $targetSummaries[$target] = New-TargetSummary -Target $target
        }
        $targetSummary = $targetSummaries[$target]
        $targetSummary.calls += 1

        $blockKey = ""
        $blockBodyTotal = 0
        $blockEntriesDelta = 0
        $blockTicksPerEntry = 0
        if ($null -ne $ppcCall.block) {
            $blockKey = "{0}:{1}" -f $fn.function, $ppcCall.block.ordinal
            if ($blockProfiles.ContainsKey($blockKey)) {
                $blockBodyTotal = $blockProfiles[$blockKey].body_ticks_total
                $blockEntriesDelta = $blockProfiles[$blockKey].entries_delta
                $blockTicksPerEntry = $blockProfiles[$blockKey].ticks_per_entry
            }
        }

        $targetFirst = $null
        if ($calleeFirstAccess.ContainsKey($target)) {
            $targetFirst = $calleeFirstAccess[$target]
        } else {
            Add-Count $missingTargetCounts $target
        }

        $classifications = @()
        $fieldNames = @()
        $callLiveInStores = 0
        $callMissingStores = 0
        $callLiveInBytes = 0
        $callFirstStoreStores = 0
        $callAbsentStores = 0
        foreach ($store in $stores) {
            $totalArgumentStores += 1
            $totalArgumentBytes += [int64]$store.approx_bytes
            Add-Count $fieldCounts $store.name
            $fieldNames += $store.name
            $classification = "callee_hir_missing"
            $calleeFirst = "-"
            if ($null -ne $targetFirst) {
                if ($targetFirst.ContainsKey([int]$store.offset)) {
                    $first = $targetFirst[[int]$store.offset]
                    $calleeFirst = "{0}@{1}:{2}" -f $first.kind, $first.ppc_address, $first.ppc_op
                    if ($first.kind -eq "load") {
                        $classification = "callee_first_load"
                        $callLiveInStores += 1
                        $callLiveInBytes += [int64]$store.approx_bytes
                        $liveInStores += 1
                        $liveInBytes += [int64]$store.approx_bytes
                    } elseif ($first.kind -eq "store") {
                        $classification = "callee_first_store"
                        $callFirstStoreStores += 1
                        $firstStoreStores += 1
                    } else {
                        $classification = "callee_first_other"
                    }
                } else {
                    $classification = "callee_absent"
                    $callAbsentStores += 1
                    $absentStores += 1
                }
            } else {
                $callMissingStores += 1
                $missingCalleeStores += 1
            }
            Add-Count $classCounts $classification
            $classifications += ("{0}:{1}:{2}" -f $store.name, $classification, $calleeFirst)
        }
        $bodyWeightedArgumentFields += ([int64]$stores.Count * [int64]$blockBodyTotal)
        $bodyWeightedLiveInFields += ([int64]$callLiveInStores * [int64]$blockBodyTotal)
        $bodyWeightedMissingFields += ([int64]$callMissingStores * [int64]$blockBodyTotal)
        $targetSummary.argument_stores += $stores.Count
        $targetSummary.live_in_stores += $callLiveInStores
        $targetSummary.first_store_stores += $callFirstStoreStores
        $targetSummary.absent_stores += $callAbsentStores
        $targetSummary.missing_callee_stores += $callMissingStores
        $targetSummary.live_in_bytes += $callLiveInBytes
        $targetSummary.body_ticks_total_sum += [int64]$blockBodyTotal
        $targetSummary.body_weighted_argument_fields += ([int64]$stores.Count * [int64]$blockBodyTotal)
        $targetSummary.body_weighted_live_in_fields += ([int64]$callLiveInStores * [int64]$blockBodyTotal)
        $targetSummary.body_weighted_missing_fields += ([int64]$callMissingStores * [int64]$blockBodyTotal)
        if ([int64]$blockBodyTotal -gt [int64]$targetSummary.top_call_body_ticks) {
            $targetSummary.top_call_body_ticks = [int64]$blockBodyTotal
            $targetSummary.top_call = "{0}->0x{1}" -f $ppcCall.ppc_address, $target
        }

        $rows.Add([pscustomobject][ordered]@{
            function = $fn.function
            block = if ($null -ne $ppcCall.block) { $ppcCall.block.ordinal } else { -1 }
            block_guest = if ($null -ne $ppcCall.block) { $ppcCall.block.first_guest } else { "00000000" }
            block_body_ticks_total = $blockBodyTotal
            block_entries_delta = $blockEntriesDelta
            block_ticks_per_entry = $blockTicksPerEntry
            call_pc = $ppcCall.ppc_address
            target = $target
            hir_call = $callHir.text
            argument_stores = $stores.Count
            live_in_stores = $callLiveInStores
            first_store_stores = $callFirstStoreStores
            absent_stores = $callAbsentStores
            missing_callee_stores = $callMissingStores
            live_in_bytes = $callLiveInBytes
            fields = (($fieldNames | Sort-Object -Unique) -join ",")
            classification = ($classifications -join ";")
        }) | Out-Null
    }
}

Write-Output "# HIR Guest-Call Argument Handoff Audit"
Write-Output ""
Write-Output ("primary_log={0}" -f (Resolve-Path -LiteralPath $LogPath).Path)
if ($ExtraLogPath.Count -gt 0) {
    Write-Output ("extra_logs={0}" -f (($ExtraLogPath | ForEach-Object { (Resolve-Path -LiteralPath $_).Path }) -join ";"))
}
Write-Output ("phase={0}" -f $Phase)
Write-Output ("function_filter={0}" -f $(if ([string]::IsNullOrWhiteSpace($Function)) { "<all>" } else { $functionFilter }))
Write-Output ("parsed_functions={0}" -f $functions.Count)
Write-Output ("selected_functions={0}" -f $selectedFunctions.Count)
Write-Output ("direct_ppc_bl_calls={0}" -f $totalCalls)
Write-Output ("calls_with_argument_stores={0}" -f $callsWithArgumentStores)
Write-Output ("argument_store_fields={0}" -f $totalArgumentStores)
Write-Output ("argument_store_bytes={0}" -f $totalArgumentBytes)
Write-Output ("callee_first_load_stores={0}" -f $liveInStores)
Write-Output ("callee_first_load_bytes={0}" -f $liveInBytes)
Write-Output ("callee_first_store_stores={0}" -f $firstStoreStores)
Write-Output ("callee_absent_stores={0}" -f $absentStores)
Write-Output ("callee_hir_missing_stores={0}" -f $missingCalleeStores)
Write-Output ("normal_entry_fallback_required={0}" -f $callsWithArgumentStores)
Write-Output ("body_weighted_argument_fields={0}" -f $bodyWeightedArgumentFields)
Write-Output ("body_weighted_live_in_fields={0}" -f $bodyWeightedLiveInFields)
Write-Output ("body_weighted_missing_fields={0}" -f $bodyWeightedMissingFields)
Write-Output "payload_materializations_allowed=0"
Write-Output "behavior_changed=0"
Write-Output ("classification_counts={0}" -f (Get-TopPairs $classCounts $Top))
Write-Output ("field_counts={0}" -f (Get-TopPairs $fieldCounts $Top))
Write-Output ("callee_targets={0}" -f (Get-TopPairs $calleeTargetCounts $Top))
Write-Output ("missing_callee_targets={0}" -f (Get-TopPairs $missingTargetCounts $Top))
Write-Output ""
if ($fastEntryAuditRows.Count -gt 0) {
    Write-Output "## Fast-Entry Compile Audit Rows"
    $selectedFastRows = @($fastEntryAuditRows | Where-Object {
        [string]::IsNullOrWhiteSpace($functionFilter) -or $_.function -eq $functionFilter
    })
    foreach ($fastRow in $selectedFastRows) {
        $values = $fastRow.values
        Write-Output ("fn={0} direct_calls={1} eligible_regular={2} tail_blockers={3} indirect_blockers={4} extern_host_blockers={5} unresolved_direct_targets={6} helper_blockers={7} normal_entry_fallback={8} stackpoint_sensitive={9} arg_store_calls={10} arg_store_fields={11} arg_store_bytes={12} callee_first_use_known={13} callee_first_use_missing={14} dirty_flush_points={15} flush_context_barrier={16} flush_return={17} debug_trap_blockers={18} already_compiled_targets={19} payload_materializations_allowed={20} behavior_changed={21} alternate_codegen={22} normal_entry={23} global_indirection={24}" -f
            $fastRow.function,
            (Get-HashValue $values "direct_calls"),
            (Get-HashValue $values "eligible_regular"),
            (Get-HashValue $values "tail_blockers"),
            (Get-HashValue $values "indirect_blockers"),
            (Get-HashValue $values "extern_host_blockers"),
            (Get-HashValue $values "unresolved_direct_targets"),
            (Get-HashValue $values "helper_blockers"),
            (Get-HashValue $values "normal_entry_fallback"),
            (Get-HashValue $values "stackpoint_sensitive"),
            (Get-HashValue $values "arg_store_calls"),
            (Get-HashValue $values "arg_store_fields"),
            (Get-HashValue $values "arg_store_bytes"),
            (Get-HashValue $values "callee_first_use_known"),
            (Get-HashValue $values "callee_first_use_missing"),
            (Get-HashValue $values "dirty_flush_points"),
            (Get-HashValue $values "flush_context_barrier"),
            (Get-HashValue $values "flush_return"),
            (Get-HashValue $values "debug_trap_blockers"),
            (Get-HashValue $values "already_compiled_targets"),
            (Get-HashValue $values "payload_materializations_allowed"),
            (Get-HashValue $values "behavior_changed"),
            (Get-HashValue $values "alternate_codegen"),
            (Get-HashValue $values "normal_entry" "unknown"),
            (Get-HashValue $values "global_indirection" "unknown"))
    }
    if ($selectedFastRows.Count -eq 0) {
        Write-Output "none_for_function_filter=true"
    }
    Write-Output ""
    $selectedTargetRows = @($fastEntryAuditTargetRows | Where-Object {
        [string]::IsNullOrWhiteSpace($functionFilter) -or $_.function -eq $functionFilter
    })
    if ($selectedTargetRows.Count -gt 0) {
        Write-Output "## Fast-Entry Runtime Target Rows"
        foreach ($targetRow in ($selectedTargetRows |
                Sort-Object -Property @{ Expression = { [int64](Get-HashValue $_.values "arg_store_fields") }; Descending = $true },
                                      @{ Expression = { [int64](Get-HashValue $_.values "calls") }; Descending = $true },
                                      @{ Expression = "target"; Ascending = $true } |
                Select-Object -First $Top)) {
            $values = $targetRow.values
            Write-Output ("target=0x{0} calls={1} regular={2} conditional={3} tail={4} eligible_regular={5} already_compiled={6} unresolved={7} helper_blockers={8} normal_entry_fallback={9} stackpoint_sensitive={10} arg_store_calls={11} arg_store_fields={12} arg_store_bytes={13} parent_pre_call_flush_points={14} first_call={15} first_block={16} payload_materializations_allowed={17} behavior_changed={18} alternate_codegen={19} normal_entry={20} global_indirection={21} arg_store_top={22}" -f
                $targetRow.target,
                (Get-HashValue $values "calls"),
                (Get-HashValue $values "regular"),
                (Get-HashValue $values "conditional"),
                (Get-HashValue $values "tail"),
                (Get-HashValue $values "eligible_regular"),
                (Get-HashValue $values "already_compiled"),
                (Get-HashValue $values "unresolved"),
                (Get-HashValue $values "helper_blockers"),
                (Get-HashValue $values "normal_entry_fallback"),
                (Get-HashValue $values "stackpoint_sensitive"),
                (Get-HashValue $values "arg_store_calls"),
                (Get-HashValue $values "arg_store_fields"),
                (Get-HashValue $values "arg_store_bytes"),
                (Get-HashValue $values "parent_pre_call_flush_points"),
                (Get-HashValue $values "first_call" "00000000"),
                (Get-HashValue $values "first_block" "00000000"),
                (Get-HashValue $values "payload_materializations_allowed"),
                (Get-HashValue $values "behavior_changed"),
                (Get-HashValue $values "alternate_codegen"),
                (Get-HashValue $values "normal_entry" "unknown"),
                (Get-HashValue $values "global_indirection" "unknown"),
                (Get-HashValue $values "arg_store_top" "-"))
        }
        Write-Output ""
    }
}
Write-Output "## Target Summary"
foreach ($summary in ($targetSummaries.Values |
        Sort-Object -Property @{ Expression = "body_weighted_argument_fields"; Descending = $true },
                              @{ Expression = "argument_stores"; Descending = $true },
                              @{ Expression = "target"; Ascending = $true } |
        Select-Object -First $Top)) {
    $knownStores = [int64]$summary.live_in_stores + [int64]$summary.first_store_stores + [int64]$summary.absent_stores
    $knownShare = 0.0
    if ([int64]$summary.argument_stores -gt 0) {
        $knownShare = [Math]::Round((100.0 * [double]$knownStores / [double]$summary.argument_stores), 2)
    }
    Write-Output ("target=0x{0} calls={1} stores={2} live_in={3} first_store={4} absent={5} missing={6} known_share_pct={7} live_bytes={8} body_ticks_sum={9} body_weighted_fields={10} body_weighted_live_in={11} body_weighted_missing={12} top_call={13} top_call_body_ticks={14}" -f
        $summary.target, $summary.calls, $summary.argument_stores,
        $summary.live_in_stores, $summary.first_store_stores,
        $summary.absent_stores, $summary.missing_callee_stores,
        $knownShare, $summary.live_in_bytes, $summary.body_ticks_total_sum,
        $summary.body_weighted_argument_fields,
        $summary.body_weighted_live_in_fields,
        $summary.body_weighted_missing_fields,
        $summary.top_call, $summary.top_call_body_ticks)
}
Write-Output ""
Write-Output "## Top Calls"
foreach ($row in ($rows | Sort-Object -Property @{ Expression = "block_body_ticks_total"; Descending = $true },
                                      @{ Expression = "argument_stores"; Descending = $true },
                                      @{ Expression = "call_pc"; Ascending = $true } |
                       Select-Object -First $Top)) {
    Write-Output ("fn={0} block={1} guest={2} body_total={3} entries_delta={4} tpe={5} call={6}->0x{7} stores={8} live_in={9} first_store={10} absent={11} missing={12} live_bytes={13} fields={14} hir='{15}' class={16}" -f
        $row.function, $row.block, $row.block_guest, $row.block_body_ticks_total,
        $row.block_entries_delta, $row.block_ticks_per_entry, $row.call_pc,
        $row.target, $row.argument_stores, $row.live_in_stores,
        $row.first_store_stores, $row.absent_stores,
        $row.missing_callee_stores, $row.live_in_bytes, $row.fields,
        $row.hir_call, $row.classification)
}
Write-Output ""
Write-Output "## Decision Notes"
Write-Output "callee_first_load_stores counts parent r3-r10/lr stores where the parsed callee first touches the same PPCContext field with a load. Those are real ABI traffic and not local store-elision candidates."
Write-Output "callee_first_store_stores and callee_absent_stores are known-callee cases that are less useful for argument handoff than callee_first_load_stores."
Write-Output "callee_hir_missing_stores means the caller side is a candidate but the target function was not in the provided HIR logs; capture or provide that callee before any fast-entry design."
Write-Output "normal_entry_fallback_required is the number of direct guest-call sites with argument stores that would need a normal-entry-compatible fallback if a future fast-entry variant passed arguments out-of-band."
Write-Output "body_weighted_* values multiply field counts by the block body-time total when block profile data is available; they are ranking hints, not speed proof."
