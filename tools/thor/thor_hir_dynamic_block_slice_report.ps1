param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [string]$BlockProfileLog = "",
    [string]$BlockGuests = "",
    [int]$Top = 12,
    [int]$ContextTop = 8
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

function Convert-HexToInt64 {
    param([string]$Hex)
    return [Convert]::ToInt64($Hex, 16)
}

function New-ProfileRow {
    param([string]$Guest)
    return [pscustomobject][ordered]@{
        guest = $Guest
        block = -1
        body_ticks_total = 0
        body_ticks_delta = 0
        entries_delta = 0
        ticks_per_entry = 0
    }
}

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}

$resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
$functionUpper = $Function.ToUpperInvariant()
$functionPattern = [Regex]::Escape($functionUpper)
$phasePattern = [Regex]::Escape($Phase)
$linePattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"

$items = New-Object System.Collections.Generic.List[object]
$ppcItems = New-Object System.Collections.Generic.List[object]
$ppcByAddress = @{}
$currentLabel = ""
$currentLabelGuest = "00000000"
$ordinal = 0

Get-Content -LiteralPath $resolvedLog | ForEach-Object {
    if ($_ -notmatch $linePattern) {
        return
    }

    $text = $Matches.text
    if ($text -match "^\s*<entry>:") {
        return
    }
    if ($text -match "^\s*(?<label>(loc_[0-9A-Fa-f]+|_label[0-9A-Fa-f]+)):\s*$") {
        $currentLabel = $Matches.label
        $currentLabelGuest = "00000000"
        if ($currentLabel -match "^loc_([0-9A-Fa-f]+)") {
            $currentLabelGuest = $Matches[1].ToUpperInvariant()
        }
        $items.Add([pscustomobject][ordered]@{
            kind = "label"
            ordinal = $ordinal
            label = $currentLabel
            label_guest = $currentLabelGuest
            address = $currentLabelGuest
            op = ""
            text = $text.Trim()
        }) | Out-Null
        $ordinal += 1
        return
    }
    if ($text -match "^\s*;\s+(?<addr>[0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+(?<op>[A-Za-z0-9_\.]+)\s*(?<args>.*)$") {
        $addr = $Matches.addr.ToUpperInvariant()
        $item = [pscustomobject][ordered]@{
            kind = "ppc"
            ordinal = $ordinal
            label = $currentLabel
            label_guest = $currentLabelGuest
            address = $addr
            op = $Matches.op.ToLowerInvariant()
            text = $text.Trim()
        }
        $items.Add($item) | Out-Null
        $ppcItems.Add($item) | Out-Null
        if (!$ppcByAddress.ContainsKey($addr)) {
            $ppcByAddress[$addr] = $item
        }
        $ordinal += 1
        return
    }
    if ($text -match "^\s*;\s*(in:|out:|$)") {
        return
    }
    if ([string]::IsNullOrWhiteSpace($text)) {
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
    $items.Add([pscustomobject][ordered]@{
        kind = "hir"
        ordinal = $ordinal
        label = $currentLabel
        label_guest = $currentLabelGuest
        address = ""
        op = $op
        text = $trimmed
    }) | Out-Null
    $ordinal += 1
}

if ($ppcItems.Count -eq 0) {
    throw "No $Phase filtered HIR/PPC lines for function $functionUpper found in $resolvedLog."
}

$profileRows = @{}
$edgeRows = @{}

if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    if (!(Test-Path -LiteralPath $BlockProfileLog)) {
        throw "BlockProfileLog not found: $BlockProfileLog"
    }
    $resolvedProfile = (Resolve-Path -LiteralPath $BlockProfileLog).Path
    $bodyPattern = "A64 speed profile block body top \d+: fn $functionPattern .* block=(?<block>\d+) guest=(?<guest>[0-9A-Fa-f]{8}) body_ticks_delta=(?<delta>\d+) body_ticks_total=(?<total>\d+) entries_delta=(?<entries>\d+) ticks_per_entry=(?<tpe>\d+)"
    $edgePattern = "A64 speed profile call edge top \d+: fn $functionPattern .* edge=(?<edge>\d+) block=(?<block>[0-9A-Fa-f]{8}) target=(?<target>[0-9A-Fa-f]{8}) calls_delta=(?<calls_delta>\d+) calls_total=(?<calls_total>\d+) body_ticks_delta=(?<body_delta>\d+) body_ticks_total=(?<body_total>\d+) ticks_per_call=(?<tpc>\d+)"
    Get-Content -LiteralPath $resolvedProfile | ForEach-Object {
        if ($_ -match $bodyPattern) {
            $guest = $Matches.guest.ToUpperInvariant()
            if (!$profileRows.ContainsKey($guest)) {
                $profileRows[$guest] = New-ProfileRow $guest
            }
            $row = $profileRows[$guest]
            $total = [int64]$Matches.total
            if ($total -ge $row.body_ticks_total) {
                $row.block = [int]$Matches.block
                $row.body_ticks_total = $total
                $row.body_ticks_delta = [int64]$Matches.delta
                $row.entries_delta = [int64]$Matches.entries
                $row.ticks_per_entry = [int64]$Matches.tpe
            }
            return
        }
        if ($_ -match $edgePattern) {
            $blockGuest = $Matches.block.ToUpperInvariant()
            if (!$edgeRows.ContainsKey($blockGuest)) {
                $edgeRows[$blockGuest] = New-Object System.Collections.Generic.List[object]
            }
            $list = $edgeRows[$blockGuest]
            $edge = $Matches.edge
            $existing = $list | Where-Object { $_.edge -eq $edge } | Select-Object -First 1
            $total = [int64]$Matches.body_total
            if ($null -eq $existing) {
                $list.Add([pscustomobject][ordered]@{
                    edge = $edge
                    target = $Matches.target.ToUpperInvariant()
                    calls_total = [int64]$Matches.calls_total
                    calls_delta = [int64]$Matches.calls_delta
                    body_ticks_total = $total
                    body_ticks_delta = [int64]$Matches.body_delta
                    ticks_per_call = [int64]$Matches.tpc
                }) | Out-Null
            } elseif ($total -ge $existing.body_ticks_total) {
                $existing.target = $Matches.target.ToUpperInvariant()
                $existing.calls_total = [int64]$Matches.calls_total
                $existing.calls_delta = [int64]$Matches.calls_delta
                $existing.body_ticks_total = $total
                $existing.body_ticks_delta = [int64]$Matches.body_delta
                $existing.ticks_per_call = [int64]$Matches.tpc
            }
        }
    }
}

$explicitGuests = New-Object System.Collections.Generic.List[string]
if (![string]::IsNullOrWhiteSpace($BlockGuests)) {
    $BlockGuests -split "[,\s]+" |
        Where-Object { ![string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { $explicitGuests.Add($_.Trim().ToUpperInvariant()) | Out-Null }
}

$targetGuests = New-Object System.Collections.Generic.List[string]
if ($explicitGuests.Count -gt 0) {
    foreach ($guest in $explicitGuests) {
        if (!$targetGuests.Contains($guest)) {
            $targetGuests.Add($guest) | Out-Null
        }
    }
} elseif ($profileRows.Count -gt 0) {
    $profileRows.Values |
        Where-Object { $_.guest -ne "00000000" } |
        Sort-Object -Property @{ Expression = "body_ticks_total"; Descending = $true },
                              @{ Expression = "guest"; Ascending = $true } |
        Select-Object -First $Top |
        ForEach-Object { $targetGuests.Add($_.guest) | Out-Null }
}

if ($targetGuests.Count -eq 0) {
    throw "No block guests supplied or found in profile rows."
}

$allBoundaryGuests = New-Object System.Collections.Generic.List[string]
foreach ($guest in $profileRows.Keys) {
    if ($guest -ne "00000000" -and $ppcByAddress.ContainsKey($guest)) {
        $allBoundaryGuests.Add($guest) | Out-Null
    }
}
foreach ($guest in $explicitGuests) {
    if ($guest -ne "00000000" -and $ppcByAddress.ContainsKey($guest) -and !$allBoundaryGuests.Contains($guest)) {
        $allBoundaryGuests.Add($guest) | Out-Null
    }
}

function Get-EdgeSummary {
    param([string]$Guest)
    if (!$edgeRows.ContainsKey($Guest)) {
        return [pscustomobject][ordered]@{
            targets = "-"
            body_total = 0
            calls_total = 0
        }
    }
    $bodyTotal = 0
    $callsTotal = 0
    $targets = New-Object System.Collections.Generic.List[string]
    foreach ($edge in $edgeRows[$Guest]) {
        $bodyTotal += [int64]$edge.body_ticks_total
        $callsTotal += [int64]$edge.calls_total
        $targets.Add(("{0}:{1}" -f $edge.target, $edge.body_ticks_total)) | Out-Null
    }
    return [pscustomobject][ordered]@{
        targets = ($targets -join ",")
        body_total = $bodyTotal
        calls_total = $callsTotal
    }
}

function Get-SliceEndOrdinal {
    param(
        [object]$StartItem,
        [System.Collections.Generic.List[string]]$BoundaryGuests
    )
    $startOrdinal = [int]$StartItem.ordinal
    $next = $null
    foreach ($guest in $BoundaryGuests) {
        if (!$ppcByAddress.ContainsKey($guest)) {
            continue
        }
        $candidate = $ppcByAddress[$guest]
        if ([int]$candidate.ordinal -le $startOrdinal) {
            continue
        }
        if ($null -eq $next -or [int]$candidate.ordinal -lt [int]$next.ordinal) {
            $next = $candidate
        }
    }
    if ($null -ne $next) {
        return [int]$next.ordinal
    }
    $nextLabel = $items |
        Where-Object { $_.kind -eq "label" -and [int]$_.ordinal -gt $startOrdinal } |
        Sort-Object -Property ordinal |
        Select-Object -First 1
    if ($null -ne $nextLabel) {
        return [int]$nextLabel.ordinal
    }
    return [int]($items[$items.Count - 1].ordinal + 1)
}

Write-Output "# HIR Dynamic Block Slice Report"
Write-Output ""
Write-Output "log=$resolvedLog"
if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    Write-Output "block_profile_log=$((Resolve-Path -LiteralPath $BlockProfileLog).Path)"
}
Write-Output "function=$functionUpper"
Write-Output "phase=$Phase"
Write-Output "ppc_comments=$($ppcItems.Count)"
Write-Output "hir_items=$($items.Count)"
Write-Output ""
Write-Output "Note: dynamic block guests come from generated-code block body rows. A guest can point inside a larger HIR label, so this report slices from that PPC address to the next profiled dynamic guest or HIR label."
Write-Output "Note: approximate exclusive ticks subtract same-run direct call-edge body ticks from the dynamic block body total."
Write-Output ""
Write-Output "## Dynamic Block Slices"

foreach ($guest in $targetGuests) {
    $guestUpper = $guest.ToUpperInvariant()
    if (!$ppcByAddress.ContainsKey($guestUpper)) {
        Write-Output ("guest={0} mapped=false reason=no_exact_ppc_comment" -f $guestUpper)
        continue
    }

    $startItem = $ppcByAddress[$guestUpper]
    $endOrdinal = Get-SliceEndOrdinal $startItem $allBoundaryGuests
    $sliceItems = @($items | Where-Object {
        [int]$_.ordinal -ge [int]$startItem.ordinal -and [int]$_.ordinal -lt $endOrdinal
    })
    $slicePpc = @($sliceItems | Where-Object { $_.kind -eq "ppc" })
    $sliceHir = @($sliceItems | Where-Object { $_.kind -eq "hir" })
    $lastPpc = $slicePpc | Select-Object -Last 1
    $profile = $null
    if ($profileRows.ContainsKey($guestUpper)) {
        $profile = $profileRows[$guestUpper]
    } else {
        $profile = New-ProfileRow $guestUpper
    }
    $edgeSummary = Get-EdgeSummary $guestUpper
    $exclusive = [int64]$profile.body_ticks_total - [int64]$edgeSummary.body_total
    $exclusivePct = 0
    if ($profile.body_ticks_total -gt 0) {
        $exclusivePct = [Math]::Round(100.0 * $exclusive / [double]$profile.body_ticks_total, 2)
    }

    $ppcOps = @{}
    $hirOps = @{}
    $loadSlots = @{}
    $storeSlots = @{}
    $memoryLoads = 0
    $memoryStores = 0
    $branches = 0
    $calls = 0
    $barriers = 0
    foreach ($item in $slicePpc) {
        Add-Count $ppcOps $item.op
    }
    foreach ($item in $sliceHir) {
        Add-Count $hirOps $item.op
        if ($item.text -match "load_context \+([0-9]+)") {
            Add-Count $loadSlots (Get-ContextOffsetName ([int]$Matches[1]))
        }
        if ($item.text -match "store_context \+([0-9]+)") {
            Add-Count $storeSlots (Get-ContextOffsetName ([int]$Matches[1]))
        }
        if ($item.text -match "\b(load|load_offset|load_vector_sh[lr])(\.[0-9]+)?\b") {
            $memoryLoads += 1
        }
        if ($item.text -match "\b(store|store_offset|store_vector_sh[lr])(\.[0-9]+)?\b") {
            $memoryStores += 1
        }
        if ($item.text -match "\bbranch(_true|_false)?\b") {
            $branches += 1
        }
        if ($item.text -match "\bcall(_true|_indirect|_indirect_true|_extern)?\b") {
            $calls += 1
        }
        if ($item.text -match "\bcontext_barrier\b") {
            $barriers += 1
        }
    }

    $spanEnd = $guestUpper
    if ($null -ne $lastPpc) {
        $spanEnd = $lastPpc.address
    }
    Write-Output ("guest={0} mapped=true label={1} span={2}-{3} body_total={4} body_delta={5} entries_delta={6} ticks_per_entry={7} edge_targets={8} edge_body_total={9} edge_calls_total={10} approx_exclusive={11} exclusive_pct={12} ppc_count={13} hir_count={14} mem={15}/{16} branches={17} calls={18} barriers={19} ppc_ops={20} hir_ops={21} loads={22} stores={23}" -f
        $guestUpper, $startItem.label, $guestUpper, $spanEnd,
        $profile.body_ticks_total, $profile.body_ticks_delta,
        $profile.entries_delta, $profile.ticks_per_entry,
        $edgeSummary.targets, $edgeSummary.body_total,
        $edgeSummary.calls_total, $exclusive, $exclusivePct,
        $slicePpc.Count, $sliceHir.Count, $memoryLoads, $memoryStores,
        $branches, $calls, $barriers,
        (Get-TopPairs $ppcOps $ContextTop),
        (Get-TopPairs $hirOps $ContextTop),
        (Get-TopPairs $loadSlots $ContextTop),
        (Get-TopPairs $storeSlots $ContextTop))

    $ppcPreview = $slicePpc |
        Select-Object -First 12 |
        ForEach-Object { $_.text -replace "\s+", " " }
    Write-Output ("  ppc_preview={0}" -f (($ppcPreview) -join " | "))
}
