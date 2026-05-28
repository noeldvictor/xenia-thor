param(
    [string]$PacketDir = "",
    [string]$LogPath = "",
    [string]$OutPath = "",
    [int]$Top = 16
)

$ErrorActionPreference = "Stop"

function Resolve-LogPath {
    param(
        [string]$ExplicitPath,
        [string]$PacketPath
    )

    if ($ExplicitPath) {
        if (!(Test-Path -LiteralPath $ExplicitPath)) {
            throw "LogPath not found: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    if ($PacketPath) {
        $candidate = Join-Path $PacketPath "logcat.txt"
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Pass -LogPath or -PacketDir containing logcat.txt."
}

function Convert-HexValue {
    param([string]$Text)
    return [UInt64]([Convert]::ToInt64($Text, 16))
}

function Format-HexValue {
    param(
        [UInt64]$Value,
        [int]$Width = 8
    )
    return ("{0:X$Width}" -f $Value)
}

function Bool-Bit {
    param([bool]$Value)
    if ($Value) {
        return 1
    }
    return 0
}

function First-Index {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )

    for ($i = 0; $i -lt $Lines.Count; ++$i) {
        if ($Lines[$i] -match $Pattern) {
            return $i
        }
    }
    return -1
}

function Count-Matches {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )
    return @($Lines | Select-String -Pattern $Pattern).Count
}

function Count-AfterIndex {
    param(
        [string[]]$Lines,
        [int]$Index,
        [string]$Pattern
    )

    if ($Index -lt 0 -or $Index -ge $Lines.Count) {
        return 0
    }

    $count = 0
    for ($i = $Index + 1; $i -lt $Lines.Count; ++$i) {
        if ($Lines[$i] -match $Pattern) {
            ++$count
        }
    }
    return $count
}

function Find-PhysicalReleaseAfter {
    param(
        [string[]]$Lines,
        [int]$StartIndex,
        [string]$Handle,
        [string]$ParentAddress,
        [int]$Lookahead = 5
    )

    $pattern = "!>\s+(?<handle>[0-9A-Fa-f]{8})\s+PhysicalHeap::Release failed due to parent heap failure \(physical_address=(?<physical>[0-9A-Fa-f]{8}), parent_address=(?<parent>[0-9A-Fa-f]{8}), heap_base=(?<heap_base>[0-9A-Fa-f]{8}), page_size=(?<page_size>[0-9A-Fa-f]+)\)"
    $endIndex = [Math]::Min($Lines.Count - 1, $StartIndex + $Lookahead)
    for ($i = $StartIndex + 1; $i -le $endIndex; ++$i) {
        if ($Lines[$i] -match $pattern -and
                $Matches["handle"].ToUpperInvariant() -eq $Handle.ToUpperInvariant() -and
                $Matches["parent"].ToUpperInvariant() -eq $ParentAddress.ToUpperInvariant()) {
            return [pscustomobject][ordered]@{
                LineIndex = $i
                PhysicalAddress = $Matches["physical"].ToUpperInvariant()
                ParentAddress = $Matches["parent"].ToUpperInvariant()
                HeapBase = $Matches["heap_base"].ToUpperInvariant()
                PageSize = $Matches["page_size"].ToUpperInvariant()
            }
        }
    }
    return $null
}

function Find-MmFreeAfter {
    param(
        [string[]]$Lines,
        [int]$StartIndex,
        [string]$Handle,
        [string]$PhysicalAddress,
        [int]$Lookahead = 5
    )

    $pattern = "!>\s+(?<handle>[0-9A-Fa-f]{8})\s+MmFreePhysicalMemory failed: type=(?<type>\d+) base_address=(?<base>[0-9A-Fa-f]{8}) heap_type=(?<heap_type>\d+) heap_base=(?<heap_base>[0-9A-Fa-f]{8}) page_size=(?<page_size>[0-9A-Fa-f]+)"
    $endIndex = [Math]::Min($Lines.Count - 1, $StartIndex + $Lookahead)
    for ($i = $StartIndex + 1; $i -le $endIndex; ++$i) {
        if ($Lines[$i] -match $pattern -and
                $Matches["handle"].ToUpperInvariant() -eq $Handle.ToUpperInvariant() -and
                $Matches["base"].ToUpperInvariant() -eq $PhysicalAddress.ToUpperInvariant()) {
            return [pscustomobject][ordered]@{
                LineIndex = $i
                Type = $Matches["type"]
                BaseAddress = $Matches["base"].ToUpperInvariant()
                HeapType = $Matches["heap_type"]
                HeapBase = $Matches["heap_base"].ToUpperInvariant()
                PageSize = $Matches["page_size"].ToUpperInvariant()
            }
        }
    }
    return $null
}

$resolvedLogPath = Resolve-LogPath $LogPath $PacketDir
if ($PacketDir) {
    $PacketDir = (Resolve-Path -LiteralPath $PacketDir).Path
}

$lines = Get-Content -LiteralPath $resolvedLogPath
$firstLongjmpIndex = First-Index $lines "longjmp_reenter\s+1"
$vdSwapCount = Count-Matches $lines "\bVdSwap\b"
$vdSwapAfterLongjmp = Count-AfterIndex $lines $firstLongjmpIndex "\bVdSwap\b"
$nativeAbortMarkerCount = Count-Matches $lines "Abort message:|Fatal signal|SIGABRT|AndroidRuntime|FATAL EXCEPTION"
$throwsExceptionCount = Count-Matches $lines "throws_exception\s+1"
$longjmpReenterCount = Count-Matches $lines "longjmp_reenter\s+1"
$baseReleaseLogCount = Count-Matches $lines "BaseHeap::Release failed"
$physicalReleaseLogCount = Count-Matches $lines "PhysicalHeap::Release failed"
$mmFreeLogCount = Count-Matches $lines "MmFreePhysicalMemory failed"
$baseReleaseAfterLongjmpLines = Count-AfterIndex $lines $firstLongjmpIndex "BaseHeap::Release failed"
$physicalReleaseAfterLongjmpLines = Count-AfterIndex $lines $firstLongjmpIndex "PhysicalHeap::Release failed"
$mmFreeAfterLongjmpLines = Count-AfterIndex $lines $firstLongjmpIndex "MmFreePhysicalMemory failed"
$physicalMemoryAuditAllocateCount =
    Count-Matches $lines "Xboxkrnl physical memory audit allocate"
$physicalMemoryAuditFreeRequestCount =
    Count-Matches $lines "Xboxkrnl physical memory audit free phase request"
$physicalMemoryAuditFreeResultCount =
    Count-Matches $lines "Xboxkrnl physical memory audit free phase result"
$physicalMemoryAuditInteriorFreeRequestCount =
    Count-Matches $lines "Xboxkrnl physical memory audit free phase request .*parent_region_start 0"

$basePattern = "^(?<date>\S+)\s+(?<time>\S+)\s+(?<pid>\d+)\s+(?<tid>\d+)\s+\S\s+xenia\s+:\s+!>\s+(?<handle>[0-9A-Fa-f]{8})\s+BaseHeap::Release failed because address (?<address>[0-9A-Fa-f]{8}) is not a region start \(heap_base=(?<heap_base>[0-9A-Fa-f]{8}), page_size=(?<page_size>[0-9A-Fa-f]+), page=(?<page>[0-9A-Fa-f]+), region_base=(?<region_base>[0-9A-Fa-f]{8}), region_pages=(?<region_pages>\d+), state=(?<state>\d+), raw=(?<raw>[0-9A-Fa-f]{16})\)"
$rows = @()

for ($i = 0; $i -lt $lines.Count; ++$i) {
    $line = $lines[$i]
    if (!($line -match $basePattern)) {
        continue
    }

    $addressText = $Matches["address"].ToUpperInvariant()
    $heapBaseText = $Matches["heap_base"].ToUpperInvariant()
    $pageSizeText = $Matches["page_size"].ToUpperInvariant()
    $regionBaseText = $Matches["region_base"].ToUpperInvariant()
    $regionPagesText = $Matches["region_pages"]
    $handleText = $Matches["handle"].ToUpperInvariant()

    $address = Convert-HexValue $addressText
    $heapBase = Convert-HexValue $heapBaseText
    $pageSize = Convert-HexValue $pageSizeText
    $regionBase = Convert-HexValue $regionBaseText
    $regionPages = [UInt64]$regionPagesText
    $regionSize = $regionPages * $pageSize
    $regionEnd = $regionBase + $regionSize
    $offset = 0
    if ($address -ge $regionBase) {
        $offset = $address - $regionBase
    }

    $insideRegion = ($address -ge $regionBase -and $address -lt $regionEnd)
    $isRegionStart = ($address -eq $regionBase)
    $parentPageAligned = (($address % $pageSize) -eq 0)
    $offsetPages = 0
    if ($pageSize -ne 0) {
        $offsetPages = [UInt64][Math]::Floor([double]$offset / [double]$pageSize)
    }

    $physical = Find-PhysicalReleaseAfter $lines $i $handleText $addressText
    $physicalAddressText = ""
    $physicalHeapBaseText = ""
    $physicalPageSizeText = ""
    $physicalPageAligned = $false
    if ($physical -ne $null) {
        $physicalAddressText = $physical.PhysicalAddress
        $physicalHeapBaseText = $physical.HeapBase
        $physicalPageSizeText = $physical.PageSize
        $physicalPageSize = Convert-HexValue $physicalPageSizeText
        $physicalAddress = Convert-HexValue $physicalAddressText
        $physicalPageAligned = (($physicalAddress % $physicalPageSize) -eq 0)
    }

    $mmFree = $null
    if ($physicalAddressText) {
        $mmFree = Find-MmFreeAfter $lines $i $handleText $physicalAddressText
    }
    $mmFreePresent = ($mmFree -ne $null)

    $rows += [pscustomobject][ordered]@{
        LineIndex = $i
        Date = $Matches["date"]
        Time = $Matches["time"]
        Pid = $Matches["pid"]
        Tid = $Matches["tid"]
        Handle = $handleText
        ParentAddress = $addressText
        PhysicalAddress = $physicalAddressText
        ParentHeapBase = $heapBaseText
        ParentPageSize = $pageSizeText
        ParentPage = $Matches["page"].ToUpperInvariant()
        RegionBase = $regionBaseText
        RegionPages = $regionPages
        RegionSize = $regionSize
        RegionEnd = $regionEnd
        Offset = $offset
        OffsetPages = $offsetPages
        State = $Matches["state"]
        Raw = $Matches["raw"].ToUpperInvariant()
        InsideRegion = $insideRegion
        IsRegionStart = $isRegionStart
        ParentPageAligned = $parentPageAligned
        PhysicalHeapBase = $physicalHeapBaseText
        PhysicalPageSize = $physicalPageSizeText
        PhysicalPageAligned = $physicalPageAligned
        MmFreePresent = $mmFreePresent
        AfterLongjmp = ($firstLongjmpIndex -ge 0 -and $i -gt $firstLongjmpIndex)
    }
}

$distinctParentAddresses = @{}
$distinctPhysicalAddresses = @{}
$regionGroups = @{}
$repeatedParentAddressOccurrences = 0
$insideRegionCount = 0
$regionStartCount = 0
$parentPageAlignedCount = 0
$physicalPageAlignedCount = 0
$afterLongjmpTripletCount = 0
$maxRegionSize = [UInt64]0
$maxRegionPages = [UInt64]0
$maxRegionKey = ""

foreach ($row in $rows) {
    if ($distinctParentAddresses.ContainsKey($row.ParentAddress)) {
        ++$repeatedParentAddressOccurrences
        $distinctParentAddresses[$row.ParentAddress] = $distinctParentAddresses[$row.ParentAddress] + 1
    } else {
        $distinctParentAddresses[$row.ParentAddress] = 1
    }

    if ($row.PhysicalAddress) {
        if ($distinctPhysicalAddresses.ContainsKey($row.PhysicalAddress)) {
            $distinctPhysicalAddresses[$row.PhysicalAddress] = $distinctPhysicalAddresses[$row.PhysicalAddress] + 1
        } else {
            $distinctPhysicalAddresses[$row.PhysicalAddress] = 1
        }
    }

    $regionKey = "{0}:{1}:{2}:{3}" -f $row.ParentHeapBase, $row.ParentPageSize, $row.RegionBase, $row.RegionPages
    if ($regionGroups.ContainsKey($regionKey)) {
        $regionGroups[$regionKey] = $regionGroups[$regionKey] + 1
    } else {
        $regionGroups[$regionKey] = 1
    }

    if ($row.InsideRegion) { ++$insideRegionCount }
    if ($row.IsRegionStart) { ++$regionStartCount }
    if ($row.ParentPageAligned) { ++$parentPageAlignedCount }
    if ($row.PhysicalPageAligned) { ++$physicalPageAlignedCount }
    if ($row.AfterLongjmp) { ++$afterLongjmpTripletCount }

    if ($row.RegionSize -gt $maxRegionSize) {
        $maxRegionSize = $row.RegionSize
        $maxRegionPages = $row.RegionPages
        $maxRegionKey = $regionKey
    }
}

$sameParentRegionCount = 0
foreach ($value in $regionGroups.Values) {
    if ($value -gt $sameParentRegionCount) {
        $sameParentRegionCount = $value
    }
}

$classification = "no_project_sylpheed_heap_release_rows"
$reason = "no BaseHeap interior release rows were parsed"
$decision = "capture_a_fresh_project_sylpheed_packet"
if ($rows.Count -gt 0) {
    $classification = "physical_heap_release_rows_present"
    $reason = "parsed physical heap release failure rows"
    $decision = "inspect_physical_heap_release_rows"
}
if ($rows.Count -gt 0 -and $regionStartCount -eq 0 -and $insideRegionCount -eq $rows.Count) {
    $classification = "project_sylpheed_interior_physical_heap_frees"
    $reason = "all parsed frees are interior addresses inside an allocated parent heap region, not region starts"
    $decision = "do_not_round_down_parent_region; instrument_allocation_owner_or_guest_free_contract_next"
}
if ($classification -eq "project_sylpheed_interior_physical_heap_frees" -and
        $longjmpReenterCount -gt 0 -and $afterLongjmpTripletCount -gt 0 -and
        $vdSwapAfterLongjmp -eq 0 -and $nativeAbortMarkerCount -eq 0) {
    $classification = "project_sylpheed_interior_physical_heap_frees_after_reenter_no_present_swaps"
    $reason = "interior physical frees continue after Android reenter longjmp, with no native abort marker and no VdSwap rows after the first longjmp"
    $decision = "add_source_or_runtime_owner_trace_before_any_physical_free_behavior_patch"
}

$topCount = [Math]::Max(1, $Top)
$topRows = @($rows | Select-Object -First $topCount)

$report = @(
    "classification=$classification",
    "reason=$reason",
    "decision=$decision",
    "packet_dir=$PacketDir",
    "log_path=$resolvedLogPath",
    "line_count=$($lines.Count)",
    "first_longjmp_index=$firstLongjmpIndex",
    "longjmp_reenter_count=$longjmpReenterCount",
    "throws_exception_count=$throwsExceptionCount",
    "native_abort_marker_count=$nativeAbortMarkerCount",
    "vd_swap_count=$vdSwapCount",
    "vd_swap_after_longjmp=$vdSwapAfterLongjmp",
    "base_release_log_count=$baseReleaseLogCount",
    "physical_release_log_count=$physicalReleaseLogCount",
    "mmfree_log_count=$mmFreeLogCount",
    "base_release_after_longjmp_lines=$baseReleaseAfterLongjmpLines",
    "physical_release_after_longjmp_lines=$physicalReleaseAfterLongjmpLines",
    "mmfree_after_longjmp_lines=$mmFreeAfterLongjmpLines",
    "physical_memory_audit_allocate_count=$physicalMemoryAuditAllocateCount",
    "physical_memory_audit_free_request_count=$physicalMemoryAuditFreeRequestCount",
    "physical_memory_audit_free_result_count=$physicalMemoryAuditFreeResultCount",
    "physical_memory_audit_interior_free_request_count=$physicalMemoryAuditInteriorFreeRequestCount",
    "parsed_triplet_count=$($rows.Count)",
    "parsed_triplets_after_longjmp=$afterLongjmpTripletCount",
    "distinct_parent_address_count=$($distinctParentAddresses.Count)",
    "distinct_physical_address_count=$($distinctPhysicalAddresses.Count)",
    "repeated_parent_address_occurrences=$repeatedParentAddressOccurrences",
    "distinct_parent_region_count=$($regionGroups.Count)",
    "same_parent_region_count=$sameParentRegionCount",
    "inside_region_count=$insideRegionCount",
    "region_start_count=$regionStartCount",
    "parent_page_aligned_count=$parentPageAlignedCount",
    "physical_page_aligned_count=$physicalPageAlignedCount",
    "round_down_region_key=$maxRegionKey",
    "round_down_region_pages_max=$maxRegionPages",
    "round_down_region_size_max_hex=$(Format-HexValue $maxRegionSize)",
    "round_down_region_size_max_bytes=$maxRegionSize",
    "top_row_count=$($topRows.Count)"
)

$rowIndex = 0
foreach ($row in $topRows) {
    $report += ("row[{0}]=line_index={1} time={2} handle={3} parent={4} physical={5} region_base={6} region_end={7} region_pages={8} offset={9} offset_pages={10} inside_region={11} region_start={12} parent_page_aligned={13} physical_page_aligned={14} after_longjmp={15}" -f
        $rowIndex,
        $row.LineIndex,
        $row.Time,
        $row.Handle,
        $row.ParentAddress,
        $row.PhysicalAddress,
        $row.RegionBase,
        (Format-HexValue $row.RegionEnd),
        $row.RegionPages,
        (Format-HexValue $row.Offset),
        $row.OffsetPages,
        (Bool-Bit $row.InsideRegion),
        (Bool-Bit $row.IsRegionStart),
        (Bool-Bit $row.ParentPageAligned),
        (Bool-Bit $row.PhysicalPageAligned),
        (Bool-Bit $row.AfterLongjmp))
    ++$rowIndex
}

$output = $report -join "`r`n"
if ($OutPath) {
    $parent = Split-Path -Parent $OutPath
    if ($parent -and !(Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    Set-Content -LiteralPath $OutPath -Value $output
} else {
    Write-Output $output
}
