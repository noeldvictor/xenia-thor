param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [string]$BlockProfileLog = "",
    [int]$Top = 20
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
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return
    }
    if (!$List.Contains($Value)) {
        $List.Add($Value) | Out-Null
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

function New-BlockRow {
    param(
        [int]$Index,
        [string]$Name
    )
    $guest = "00000000"
    if ($Name -match "^loc_([0-9A-Fa-f]+)") {
        $guest = $Matches[1].ToUpperInvariant()
    }
    return [pscustomobject][ordered]@{
        index = $Index
        name = $Name
        guest = $guest
        in_edges = ""
        out_edges = ""
        instructions = 0
        context_loads = 0
        context_stores = 0
        memory_loads = 0
        memory_stores = 0
        branches = 0
        calls = 0
        barriers = 0
        ppc = @{}
        context_load_slots = @{}
        context_store_slots = @{}
        call_targets = @{}
        profile_total = 0
        profile_peak_delta = 0
        profile_body_total = 0
        profile_body_peak_delta = 0
        profile_body_peak_tpe = 0
    }
}

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}

$resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
$functionPattern = [Regex]::Escape($Function.ToUpperInvariant())
$phasePattern = [Regex]::Escape($Phase)
$linePattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"
$blocks = New-Object System.Collections.Generic.List[object]
$current = $null
$callEdgeRows = @{}
$callEdgeAuditRows = New-Object System.Collections.Generic.List[object]

Get-Content -LiteralPath $resolvedLog | ForEach-Object {
    if ($_ -notmatch $linePattern) {
        return
    }

    $text = $Matches.text
    if ($text -match "^\s*<entry>:") {
        return
    }
    if ($text -match "^\s*(loc_[0-9A-Fa-f]+):") {
        $current = New-BlockRow $blocks.Count $Matches[1]
        $blocks.Add($current) | Out-Null
        return
    }
    if ($null -eq $current) {
        return
    }

    if ($text -match "^\s*;\s+in:\s*(?<edges>.*)$") {
        $current.in_edges = $Matches.edges.Trim()
        return
    }
    if ($text -match "^\s*;\s+out:\s*(?<edges>.*)$") {
        $current.out_edges = $Matches.edges.Trim()
        return
    }
    if ($text -match "^\s*;\s+([0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+([A-Za-z0-9_\.]+)\s*(.*)$") {
        $op = $Matches[2].ToLowerInvariant()
        Add-Count $current.ppc $op
        if ($op -eq "bl" -and $Matches[3] -match "0x([0-9A-Fa-f]{8})") {
            Add-Count $current.call_targets ("0x" + $Matches[1].ToUpperInvariant())
        }
        return
    }
    if ($text -match "^\s*(;|$)") {
        return
    }

    $current.instructions += 1
    if ($text -match "load_context \+([0-9]+)") {
        $current.context_loads += 1
        Add-Count $current.context_load_slots (Get-ContextOffsetName ([int]$Matches[1]))
    }
    if ($text -match "store_context \+([0-9]+)") {
        $current.context_stores += 1
        Add-Count $current.context_store_slots (Get-ContextOffsetName ([int]$Matches[1]))
    }
    if ($text -match "\b(load|load_offset|load_vector_sh[lr])(\.[0-9]+)?\b") {
        $current.memory_loads += 1
    }
    if ($text -match "\b(store|store_offset|store_vector_sh[lr])(\.[0-9]+)?\b") {
        $current.memory_stores += 1
    }
    if ($text -match "\bbranch(_true|_false)?\b") {
        $current.branches += 1
    }
    if ($text -match "\bcall(_true|_indirect|_indirect_true|_extern)?\b") {
        $current.calls += 1
    }
    if ($text -match "\bcontext_barrier\b") {
        $current.barriers += 1
    }
}

$hirDumpFound = $blocks.Count -gt 0

if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    if (!(Test-Path -LiteralPath $BlockProfileLog)) {
        throw "BlockProfileLog not found: $BlockProfileLog"
    }
    $resolvedProfile = (Resolve-Path -LiteralPath $BlockProfileLog).Path
    $profilePattern = "A64 speed profile block top \d+: fn $functionPattern .* block=(?<block>\d+) guest=(?<guest>[0-9A-Fa-f]{8}) delta=(?<delta>\d+) total=(?<total>\d+)"
    $bodyProfilePattern = "A64 speed profile block body top \d+: fn $functionPattern .* block=(?<block>\d+) guest=(?<guest>[0-9A-Fa-f]{8}) body_ticks_delta=(?<delta>\d+) body_ticks_total=(?<total>\d+) entries_delta=(?<entries>\d+) ticks_per_entry=(?<tpe>\d+)"
    $callEdgePattern = "A64 speed profile call edge top \d+: fn $functionPattern .* edge=(?<edge>\d+) block=(?<block>[0-9A-Fa-f]{8}) target=(?<target>[0-9A-Fa-f]{8}) calls_delta=(?<calls_delta>\d+) calls_total=(?<calls_total>\d+) body_ticks_delta=(?<body_delta>\d+) body_ticks_total=(?<body_total>\d+) ticks_per_call=(?<tpc>\d+)"
    $callEdgeAuditPattern = "A64 call-edge compile audit: fn $functionPattern '(?<name>[^']*)' blocks=(?<blocks>\d+) direct_call_edges=(?<edges>\d+) instrumentation=(?<instrumentation>\d+)"
    Get-Content -LiteralPath $resolvedProfile | ForEach-Object {
        if ($_ -match $profilePattern) {
            $guest = $Matches.guest.ToUpperInvariant()
            $row = $null
            if ($guest -ne "00000000") {
                $row = $blocks | Where-Object { $_.guest -eq $guest } | Select-Object -First 1
            }
            if ($null -eq $row) {
                $index = [int]$Matches.block
                if ($index -lt 0 -or $index -ge $blocks.Count) {
                    return
                }
                $row = $blocks[$index]
            }
            $total = [int64]$Matches.total
            $delta = [int64]$Matches.delta
            if ($total -gt $row.profile_total) {
                $row.profile_total = $total
            }
            if ($delta -gt $row.profile_peak_delta) {
                $row.profile_peak_delta = $delta
            }
            return
        }

        if ($_ -match $bodyProfilePattern) {
            $guest = $Matches.guest.ToUpperInvariant()
            $row = $null
            if ($guest -ne "00000000") {
                $row = $blocks | Where-Object { $_.guest -eq $guest } | Select-Object -First 1
            }
            if ($null -eq $row) {
                $index = [int]$Matches.block
                if ($index -lt 0 -or $index -ge $blocks.Count) {
                    return
                }
                $row = $blocks[$index]
            }
            $total = [int64]$Matches.total
            $delta = [int64]$Matches.delta
            $tpe = [int64]$Matches.tpe
            if ($total -gt $row.profile_body_total) {
                $row.profile_body_total = $total
            }
            if ($delta -gt $row.profile_body_peak_delta) {
                $row.profile_body_peak_delta = $delta
            }
            if ($tpe -gt $row.profile_body_peak_tpe) {
                $row.profile_body_peak_tpe = $tpe
            }
            return
        }

        if ($_ -match $callEdgeAuditPattern) {
            $callEdgeAuditRows.Add([pscustomobject][ordered]@{
                function = $Function.ToUpperInvariant()
                name = $Matches.name
                blocks = [int]$Matches.blocks
                direct_call_edges = [int]$Matches.edges
                instrumentation = [int]$Matches.instrumentation
            }) | Out-Null
            return
        }

        if ($_ -match $callEdgePattern) {
            $edge = [int]$Matches.edge
            $blockGuest = $Matches.block.ToUpperInvariant()
            $target = $Matches.target.ToUpperInvariant()
            $key = "{0}|{1}|{2}" -f $edge, $blockGuest, $target
            if (!$callEdgeRows.ContainsKey($key)) {
                $callEdgeRows[$key] = [pscustomobject][ordered]@{
                    edge = $edge
                    block_guest = $blockGuest
                    target = ("0x" + $target)
                    calls_total = 0
                    calls_peak_delta = 0
                    body_ticks_total = 0
                    body_ticks_peak_delta = 0
                    ticks_per_call_peak = 0
                    samples = 0
                }
            }
            $row = $callEdgeRows[$key]
            $row.samples += 1
            $callsTotal = [int64]$Matches.calls_total
            $callsDelta = [int64]$Matches.calls_delta
            $bodyTotal = [int64]$Matches.body_total
            $bodyDelta = [int64]$Matches.body_delta
            $tpc = [int64]$Matches.tpc
            if ($callsTotal -gt $row.calls_total) {
                $row.calls_total = $callsTotal
            }
            if ($callsDelta -gt $row.calls_peak_delta) {
                $row.calls_peak_delta = $callsDelta
            }
            if ($bodyTotal -gt $row.body_ticks_total) {
                $row.body_ticks_total = $bodyTotal
            }
            if ($bodyDelta -gt $row.body_ticks_peak_delta) {
                $row.body_ticks_peak_delta = $bodyDelta
            }
            if ($tpc -gt $row.ticks_per_call_peak) {
                $row.ticks_per_call_peak = $tpc
            }
            return
        }
    }
}

if (!$hirDumpFound -and $callEdgeRows.Count -eq 0 -and $callEdgeAuditRows.Count -eq 0) {
    throw "No $Phase dump or dynamic call-edge rows for function $($Function.ToUpperInvariant()) found in $resolvedLog."
}

$callBlocks = @($blocks | Where-Object { $_.calls -gt 0 -or $_.call_targets.Count -gt 0 })
$targetRows = @{}
foreach ($block in $callBlocks) {
    foreach ($target in $block.call_targets.Keys) {
        if (!$targetRows.ContainsKey($target)) {
            $targetRows[$target] = [pscustomobject][ordered]@{
                target = $target
                blocks = New-Object System.Collections.Generic.List[string]
                charged_body_total = 0
                charged_body_peak_delta = 0
                entry_total = 0
                entry_peak_delta = 0
                static_call_sites = 0
            }
        }
        $row = $targetRows[$target]
        Add-Unique $row.blocks $block.guest
        $row.charged_body_total += [int64]$block.profile_body_total
        if ($block.profile_body_peak_delta -gt $row.charged_body_peak_delta) {
            $row.charged_body_peak_delta = [int64]$block.profile_body_peak_delta
        }
        $row.entry_total += [int64]$block.profile_total
        if ($block.profile_peak_delta -gt $row.entry_peak_delta) {
            $row.entry_peak_delta = [int64]$block.profile_peak_delta
        }
        $row.static_call_sites += [int64]$block.call_targets[$target]
    }
}

Write-Output "# HIR Call Path Report"
Write-Output ""
Write-Output "log=$resolvedLog"
if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    Write-Output "block_profile_log=$((Resolve-Path -LiteralPath $BlockProfileLog).Path)"
}
Write-Output "function=$($Function.ToUpperInvariant())"
Write-Output "phase=$Phase"
Write-Output "hir_dump_found=$($hirDumpFound.ToString().ToLowerInvariant())"
Write-Output "blocks=$($blocks.Count)"
Write-Output "call_blocks=$($callBlocks.Count)"
Write-Output ""
Write-Output "Note: charged_body_total is the inclusive body time charged to the caller block until the next HIR label/function exit. A call-heavy block can mostly represent callee work."
if (!$hirDumpFound) {
    Write-Output "Note: no filtered HIR dump was found, so static block context is unavailable; dynamic call-edge rows are still summarized below."
}

Write-Output ""
Write-Output "## Call Target Charge Summary"
if ($targetRows.Count -eq 0) {
    Write-Output "(no static call targets found)"
} else {
    $targetRows.Values |
        Sort-Object -Property @{ Expression = "charged_body_total"; Descending = $true },
                              @{ Expression = "entry_total"; Descending = $true },
                              @{ Expression = "target"; Ascending = $true } |
        Select-Object -First $Top |
        ForEach-Object {
            Write-Output ("target={0} blocks={1} block_guests={2} charged_body_total={3} charged_body_peak_delta={4} entry_total={5} entry_peak_delta={6} static_call_sites={7}" -f
                $_.target, $_.blocks.Count, ($_.blocks -join ","),
                $_.charged_body_total, $_.charged_body_peak_delta,
                $_.entry_total, $_.entry_peak_delta, $_.static_call_sites)
        }
}

Write-Output ""
Write-Output "## Dynamic Call Edge Target Summary"
if ($callEdgeRows.Count -eq 0) {
    if ([string]::IsNullOrWhiteSpace($BlockProfileLog)) {
        Write-Output "(no block/call-edge profile log supplied)"
    } else {
        Write-Output "(no dynamic call-edge rows found)"
    }
} else {
    $dynamicTargetRows = @{}
    foreach ($edgeRow in $callEdgeRows.Values) {
        $target = $edgeRow.target
        if (!$dynamicTargetRows.ContainsKey($target)) {
            $dynamicTargetRows[$target] = [pscustomobject][ordered]@{
                target = $target
                edges = New-Object System.Collections.Generic.List[string]
                blocks = New-Object System.Collections.Generic.List[string]
                calls_total = 0
                calls_peak_delta = 0
                body_ticks_total = 0
                body_ticks_peak_delta = 0
                ticks_per_call_peak = 0
            }
        }
        $row = $dynamicTargetRows[$target]
        Add-Unique $row.edges ([string]$edgeRow.edge)
        Add-Unique $row.blocks $edgeRow.block_guest
        $row.calls_total += [int64]$edgeRow.calls_total
        if ($edgeRow.calls_peak_delta -gt $row.calls_peak_delta) {
            $row.calls_peak_delta = [int64]$edgeRow.calls_peak_delta
        }
        $row.body_ticks_total += [int64]$edgeRow.body_ticks_total
        if ($edgeRow.body_ticks_peak_delta -gt $row.body_ticks_peak_delta) {
            $row.body_ticks_peak_delta = [int64]$edgeRow.body_ticks_peak_delta
        }
        if ($edgeRow.ticks_per_call_peak -gt $row.ticks_per_call_peak) {
            $row.ticks_per_call_peak = [int64]$edgeRow.ticks_per_call_peak
        }
    }

    $dynamicTargetRows.Values |
        Sort-Object -Property @{ Expression = "body_ticks_total"; Descending = $true },
                              @{ Expression = "calls_total"; Descending = $true },
                              @{ Expression = "target"; Ascending = $true } |
        Select-Object -First $Top |
        ForEach-Object {
            Write-Output ("target={0} edges={1} blocks={2} block_guests={3} calls_total={4} calls_peak_delta={5} body_ticks_total={6} body_ticks_peak_delta={7} ticks_per_call_peak={8}" -f
                $_.target, ($_.edges -join ","), $_.blocks.Count, ($_.blocks -join ","),
                $_.calls_total, $_.calls_peak_delta,
                $_.body_ticks_total, $_.body_ticks_peak_delta,
                $_.ticks_per_call_peak)
        }
}

Write-Output ""
Write-Output "## Dynamic Call Edge Body-Time Rows"
if ($callEdgeRows.Count -eq 0) {
    if ([string]::IsNullOrWhiteSpace($BlockProfileLog)) {
        Write-Output "(no block/call-edge profile log supplied)"
    } else {
        Write-Output "(no dynamic call-edge rows found)"
    }
} else {
    $callEdgeRows.Values |
        Sort-Object -Property @{ Expression = "body_ticks_total"; Descending = $true },
                              @{ Expression = "calls_total"; Descending = $true },
                              @{ Expression = "edge"; Ascending = $true } |
        Select-Object -First $Top |
        ForEach-Object {
            $edgeRow = $_
            $block = $blocks | Where-Object { $_.guest -eq $edgeRow.block_guest } | Select-Object -First 1
            $staticSummary = "-"
            if ($null -ne $block) {
                $staticSummary = "instr={0} ctx={1}/{2} mem={3}/{4} branches={5} calls={6} barriers={7}" -f
                    $block.instructions, $block.context_loads, $block.context_stores,
                    $block.memory_loads, $block.memory_stores, $block.branches,
                    $block.calls, $block.barriers
            }
            Write-Output ("edge={0} block_guest={1} target={2} calls_total={3} calls_peak_delta={4} body_ticks_total={5} body_ticks_peak_delta={6} ticks_per_call_peak={7} samples={8} static={9}" -f
                $edgeRow.edge, $edgeRow.block_guest, $edgeRow.target,
                $edgeRow.calls_total, $edgeRow.calls_peak_delta,
                $edgeRow.body_ticks_total, $edgeRow.body_ticks_peak_delta,
                $edgeRow.ticks_per_call_peak, $edgeRow.samples,
                $staticSummary)
        }
}

Write-Output ""
Write-Output "## Call-Edge Compile Audit Rows"
if ($callEdgeAuditRows.Count -eq 0) {
    if ([string]::IsNullOrWhiteSpace($BlockProfileLog)) {
        Write-Output "(no block/call-edge profile log supplied)"
    } else {
        Write-Output "(no call-edge compile audit rows found)"
    }
} else {
    $callEdgeAuditRows |
        Select-Object -First $Top |
        ForEach-Object {
            Write-Output ("fn={0} name='{1}' blocks={2} direct_call_edges={3} instrumentation={4}" -f
                $_.function, $_.name, $_.blocks, $_.direct_call_edges,
                $_.instrumentation)
        }
}

Write-Output ""
Write-Output "## Dynamic Body-Time Call Blocks"
$dynamicCallRows = $callBlocks |
    Sort-Object -Property @{ Expression = "profile_body_total"; Descending = $true },
                          @{ Expression = "profile_total"; Descending = $true },
                          @{ Expression = "index"; Ascending = $true } |
    Select-Object -First $Top
if (!$dynamicCallRows) {
    Write-Output "(no call blocks found)"
} else {
    $dynamicCallRows | ForEach-Object {
        Write-Output ("block={0} guest={1} body_total={2} body_peak_delta={3} body_peak_tpe={4} entry_total={5} entry_peak_delta={6} instr={7} ctx={8}/{9} mem={10}/{11} branches={12} calls={13} barriers={14} call_targets={15} ppc={16} loads={17} stores={18} in={19} out={20}" -f
            $_.index, $_.guest, $_.profile_body_total,
            $_.profile_body_peak_delta, $_.profile_body_peak_tpe,
            $_.profile_total, $_.profile_peak_delta,
            $_.instructions, $_.context_loads, $_.context_stores,
            $_.memory_loads, $_.memory_stores, $_.branches, $_.calls,
            $_.barriers, (Get-TopPairs $_.call_targets 6),
            (Get-TopPairs $_.ppc 6), (Get-TopPairs $_.context_load_slots 6),
            (Get-TopPairs $_.context_store_slots 6), $_.in_edges, $_.out_edges)
    }
}

Write-Output ""
Write-Output "## Static Call Blocks"
$callBlocks |
    Sort-Object -Property @{ Expression = "calls"; Descending = $true },
                          @{ Expression = "instructions"; Descending = $true },
                          @{ Expression = "index"; Ascending = $true } |
    Select-Object -First $Top |
    ForEach-Object {
        Write-Output ("block={0} guest={1} instr={2} ctx={3}/{4} mem={5}/{6} branches={7} calls={8} barriers={9} call_targets={10} ppc={11} in={12} out={13}" -f
            $_.index, $_.guest, $_.instructions, $_.context_loads,
            $_.context_stores, $_.memory_loads, $_.memory_stores,
            $_.branches, $_.calls, $_.barriers,
            (Get-TopPairs $_.call_targets 6), (Get-TopPairs $_.ppc 6),
            $_.in_edges, $_.out_edges)
    }
