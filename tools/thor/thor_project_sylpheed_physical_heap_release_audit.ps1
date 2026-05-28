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

function Parse-PhysicalMemoryAuditFreeRow {
    param(
        [string]$Line,
        [int]$LineIndex
    )

    $pattern = "Xboxkrnl physical memory audit free phase (?<phase>\S+) thid (?<thid>[0-9A-Fa-f]{8}) lr (?<lr>[0-9A-Fa-f]{8}) ctr (?<ctr>[0-9A-Fa-f]{8}) r1 (?<r1>[0-9A-Fa-f]{8}) type (?<type>\d+) base_address (?<base>[0-9A-Fa-f]{8}) heap_present (?<heap_present>\d+) heap_type (?<heap_type>\S+) heap_base (?<heap_base>[0-9A-Fa-f]{8}) heap_page_size (?<heap_page_size>[0-9A-Fa-f]+) parent_address (?<parent>[0-9A-Fa-f]{8}) physical_allocation_base (?<physical_allocation_base>[0-9A-Fa-f]{8}) physical_allocation_size (?<physical_allocation_size>[0-9A-Fa-f]{8}) physical_region_size (?<physical_region_size>[0-9A-Fa-f]{8}) physical_state (?<physical_state>\d+) physical_protect (?<physical_protect>\d+) physical_region_start (?<physical_region_start>\d+) physical_page_aligned (?<physical_page_aligned>\d+) parent_allocation_base (?<parent_allocation_base>[0-9A-Fa-f]{8}) parent_allocation_size (?<parent_allocation_size>[0-9A-Fa-f]{8}) parent_region_size (?<parent_region_size>[0-9A-Fa-f]{8}) parent_state (?<parent_state>\d+) parent_protect (?<parent_protect>\d+) parent_region_start (?<parent_region_start>\d+) parent_page_aligned (?<parent_page_aligned>\d+) result_known (?<result_known>\d+) result (?<result>\d+) returned_region_size (?<returned_region_size>[0-9A-Fa-f]{8}) behavior_changed (?<behavior_changed>\d+)"
    if (!($Line -match $pattern)) {
        return $null
    }

    return [pscustomobject][ordered]@{
        LineIndex = $LineIndex
        Phase = $Matches["phase"]
        ThreadId = $Matches["thid"].ToUpperInvariant()
        Lr = $Matches["lr"].ToUpperInvariant()
        Ctr = $Matches["ctr"].ToUpperInvariant()
        R1 = $Matches["r1"].ToUpperInvariant()
        Type = [int]$Matches["type"]
        BaseAddress = $Matches["base"].ToUpperInvariant()
        HeapPresent = [int]$Matches["heap_present"]
        HeapType = $Matches["heap_type"]
        HeapBase = $Matches["heap_base"].ToUpperInvariant()
        HeapPageSize = $Matches["heap_page_size"].ToUpperInvariant()
        ParentAddress = $Matches["parent"].ToUpperInvariant()
        PhysicalAllocationBase = $Matches["physical_allocation_base"].ToUpperInvariant()
        PhysicalAllocationSize = $Matches["physical_allocation_size"].ToUpperInvariant()
        PhysicalRegionSize = $Matches["physical_region_size"].ToUpperInvariant()
        PhysicalState = [int]$Matches["physical_state"]
        PhysicalProtect = [int]$Matches["physical_protect"]
        PhysicalRegionStart = [int]$Matches["physical_region_start"]
        PhysicalPageAligned = [int]$Matches["physical_page_aligned"]
        ParentAllocationBase = $Matches["parent_allocation_base"].ToUpperInvariant()
        ParentAllocationSize = $Matches["parent_allocation_size"].ToUpperInvariant()
        ParentRegionSize = $Matches["parent_region_size"].ToUpperInvariant()
        ParentState = [int]$Matches["parent_state"]
        ParentProtect = [int]$Matches["parent_protect"]
        ParentRegionStart = [int]$Matches["parent_region_start"]
        ParentPageAligned = [int]$Matches["parent_page_aligned"]
        ResultKnown = [int]$Matches["result_known"]
        Result = [int]$Matches["result"]
        ReturnedRegionSize = $Matches["returned_region_size"].ToUpperInvariant()
        BehaviorChanged = [int]$Matches["behavior_changed"]
    }
}

function Find-AuditFreeRowNear {
    param(
        [object[]]$Rows,
        [int]$BaseLineIndex,
        [string]$BaseAddress,
        [string]$Phase,
        [int]$Before = 8,
        [int]$After = 8
    )

    if (!$BaseAddress) {
        return $null
    }

    $upperBase = $BaseAddress.ToUpperInvariant()
    $startIndex = [Math]::Max(0, $BaseLineIndex - $Before)
    $endIndex = $BaseLineIndex + $After
    $matches = @(
        $Rows | Where-Object {
            $_.Phase -eq $Phase -and
            $_.BaseAddress -eq $upperBase -and
            $_.LineIndex -ge $startIndex -and
            $_.LineIndex -le $endIndex
        } | Sort-Object @{Expression = {[Math]::Abs($_.LineIndex - $BaseLineIndex)}} |
            Select-Object -First 1
    )
    if ($matches.Count -eq 0) {
        return $null
    }
    return $matches[0]
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

$physicalMemoryAuditFreeRows = @()
for ($i = 0; $i -lt $lines.Count; ++$i) {
    $auditFreeRow = Parse-PhysicalMemoryAuditFreeRow $lines[$i] $i
    if ($auditFreeRow -ne $null) {
        $physicalMemoryAuditFreeRows += $auditFreeRow
    }
}
$physicalMemoryAuditFreeRequestRows =
    @($physicalMemoryAuditFreeRows | Where-Object { $_.Phase -eq "request" })
$physicalMemoryAuditFreeResultRows =
    @($physicalMemoryAuditFreeRows | Where-Object { $_.Phase -eq "result" })
$physicalMemoryAuditParsedRequestCount = $physicalMemoryAuditFreeRequestRows.Count
$physicalMemoryAuditParsedResultCount = $physicalMemoryAuditFreeResultRows.Count
$physicalMemoryAuditRequestRegionStartCount =
    @($physicalMemoryAuditFreeRequestRows | Where-Object { $_.PhysicalRegionStart -eq 1 }).Count
$physicalMemoryAuditRequestInteriorCount =
    @($physicalMemoryAuditFreeRequestRows | Where-Object { $_.PhysicalRegionStart -eq 0 }).Count
$physicalMemoryAuditRequestPhysicalPageAlignedCount =
    @($physicalMemoryAuditFreeRequestRows | Where-Object { $_.PhysicalPageAligned -eq 1 }).Count
$physicalMemoryAuditResultSuccessCount =
    @($physicalMemoryAuditFreeResultRows | Where-Object { $_.ResultKnown -eq 1 -and $_.Result -eq 1 }).Count
$physicalMemoryAuditResultFailureCount =
    @($physicalMemoryAuditFreeResultRows | Where-Object { $_.ResultKnown -eq 1 -and $_.Result -eq 0 }).Count
$physicalMemoryAuditBehaviorChangedCount =
    @($physicalMemoryAuditFreeRows | Where-Object { $_.BehaviorChanged -ne 0 }).Count
$physicalMemoryAuditFailedInteriorRows = @(
    $physicalMemoryAuditFreeResultRows | Where-Object {
        $_.ResultKnown -eq 1 -and $_.Result -eq 0 -and
        $_.PhysicalRegionStart -eq 0 -and
        $_.PhysicalAllocationBase -ne "00000000"
    }
)

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
    $auditRequest = Find-AuditFreeRowNear $physicalMemoryAuditFreeRows $i $physicalAddressText "request" 8 1
    $auditResult = Find-AuditFreeRowNear $physicalMemoryAuditFreeRows $i $physicalAddressText "result" 1 8

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
        AuditThreadId = if ($auditRequest) { $auditRequest.ThreadId } else { "" }
        AuditLr = if ($auditRequest) { $auditRequest.Lr } else { "" }
        AuditCtr = if ($auditRequest) { $auditRequest.Ctr } else { "" }
        AuditPhysicalAllocationBase = if ($auditRequest) { $auditRequest.PhysicalAllocationBase } else { "" }
        AuditPhysicalAllocationSize = if ($auditRequest) { $auditRequest.PhysicalAllocationSize } else { "" }
        AuditPhysicalRegionSize = if ($auditRequest) { $auditRequest.PhysicalRegionSize } else { "" }
        AuditPhysicalRegionStart = if ($auditRequest) { $auditRequest.PhysicalRegionStart } else { -1 }
        AuditPhysicalPageAligned = if ($auditRequest) { $auditRequest.PhysicalPageAligned } else { -1 }
        AuditResult = if ($auditResult) { $auditResult.Result } else { -1 }
        AuditReturnedRegionSize = if ($auditResult) { $auditResult.ReturnedRegionSize } else { "" }
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
$failedInteriorOwnerGroups = @{}
$topFailedInteriorOwnerKey = ""
$topFailedInteriorOwnerCount = 0

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

    if ($row.AuditPhysicalAllocationBase -and $row.AuditPhysicalRegionStart -eq 0) {
        $ownerKey = "{0}:{1}" -f $row.AuditPhysicalAllocationBase, $row.AuditPhysicalAllocationSize
        if ($failedInteriorOwnerGroups.ContainsKey($ownerKey)) {
            $failedInteriorOwnerGroups[$ownerKey] = $failedInteriorOwnerGroups[$ownerKey] + 1
        } else {
            $failedInteriorOwnerGroups[$ownerKey] = 1
        }
    }
}

$sameParentRegionCount = 0
foreach ($value in $regionGroups.Values) {
    if ($value -gt $sameParentRegionCount) {
        $sameParentRegionCount = $value
    }
}
foreach ($key in $failedInteriorOwnerGroups.Keys) {
    $value = $failedInteriorOwnerGroups[$key]
    if ($value -gt $topFailedInteriorOwnerCount) {
        $topFailedInteriorOwnerCount = $value
        $topFailedInteriorOwnerKey = $key
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
        $failedInteriorOwnerGroups.Count -eq 1 -and
        $topFailedInteriorOwnerCount -eq $rows.Count) {
    $classification = "project_sylpheed_single_owner_interior_physical_heap_frees"
    $reason = "all parsed failed interior frees belong to one owning physical allocation; nearby region-start frees succeed"
    $decision = "design_explicit_physical_suballocation_free_contract_or_owner_refcount; do_not_round_down_parent_region"
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
    "physical_memory_audit_parsed_free_request_count=$physicalMemoryAuditParsedRequestCount",
    "physical_memory_audit_parsed_free_result_count=$physicalMemoryAuditParsedResultCount",
    "physical_memory_audit_request_region_start_count=$physicalMemoryAuditRequestRegionStartCount",
    "physical_memory_audit_request_interior_count=$physicalMemoryAuditRequestInteriorCount",
    "physical_memory_audit_request_physical_page_aligned_count=$physicalMemoryAuditRequestPhysicalPageAlignedCount",
    "physical_memory_audit_result_success_count=$physicalMemoryAuditResultSuccessCount",
    "physical_memory_audit_result_failure_count=$physicalMemoryAuditResultFailureCount",
    "physical_memory_audit_failed_interior_result_count=$($physicalMemoryAuditFailedInteriorRows.Count)",
    "physical_memory_audit_behavior_changed_count=$physicalMemoryAuditBehaviorChangedCount",
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
    "failed_interior_owner_group_count=$($failedInteriorOwnerGroups.Count)",
    "top_failed_interior_owner_group=$topFailedInteriorOwnerKey",
    "top_failed_interior_owner_group_count=$topFailedInteriorOwnerCount",
    "top_row_count=$($topRows.Count)"
)

$rowIndex = 0
foreach ($row in $topRows) {
    $report += ("row[{0}]=line_index={1} time={2} handle={3} parent={4} physical={5} region_base={6} region_end={7} region_pages={8} offset={9} offset_pages={10} inside_region={11} region_start={12} parent_page_aligned={13} physical_page_aligned={14} audit_thread={15} audit_lr={16} audit_ctr={17} audit_physical_allocation_base={18} audit_physical_allocation_size={19} audit_physical_region_size={20} audit_physical_region_start={21} audit_physical_page_aligned={22} audit_result={23} audit_returned_region_size={24} after_longjmp={25}" -f
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
        $row.AuditThreadId,
        $row.AuditLr,
        $row.AuditCtr,
        $row.AuditPhysicalAllocationBase,
        $row.AuditPhysicalAllocationSize,
        $row.AuditPhysicalRegionSize,
        $row.AuditPhysicalRegionStart,
        $row.AuditPhysicalPageAligned,
        $row.AuditResult,
        $row.AuditReturnedRegionSize,
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
