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

function Parse-AllocateRow {
    param(
        [string]$Line,
        [int]$LineIndex
    )

    $pattern = "Xboxkrnl physical memory audit allocate thid (?<thid>[0-9A-Fa-f]{8}) lr (?<lr>[0-9A-Fa-f]{8}) ctr (?<ctr>[0-9A-Fa-f]{8}) r1 (?<r1>[0-9A-Fa-f]{8}) flags (?<flags>[0-9A-Fa-f]{8}) requested_size (?<requested_size>[0-9A-Fa-f]{8}) protect_bits (?<protect_bits>[0-9A-Fa-f]{8}) min_addr (?<min_addr>[0-9A-Fa-f]{8}) max_addr (?<max_addr>[0-9A-Fa-f]{8}) alignment (?<alignment>[0-9A-Fa-f]{8}) page_size (?<page_size>[0-9A-Fa-f]+) adjusted_size (?<adjusted_size>[0-9A-Fa-f]{8}) adjusted_alignment (?<adjusted_alignment>[0-9A-Fa-f]{8}) heap_base (?<heap_base>[0-9A-Fa-f]{8}) heap_size (?<heap_size>[0-9A-Fa-f]{8}) heap_min (?<heap_min>[0-9A-Fa-f]{8}) heap_max (?<heap_max>[0-9A-Fa-f]{8}) result (?<result>[0-9A-Fa-f]{8}) parent_address (?<parent>[0-9A-Fa-f]{8}) physical_allocation_base (?<physical_allocation_base>[0-9A-Fa-f]{8}) physical_allocation_size (?<physical_allocation_size>[0-9A-Fa-f]{8}) physical_region_size (?<physical_region_size>[0-9A-Fa-f]{8}) physical_state (?<physical_state>\d+) physical_protect (?<physical_protect>\d+) parent_allocation_base (?<parent_allocation_base>[0-9A-Fa-f]{8}) parent_allocation_size (?<parent_allocation_size>[0-9A-Fa-f]{8}) parent_region_size (?<parent_region_size>[0-9A-Fa-f]{8}) parent_state (?<parent_state>\d+) parent_protect (?<parent_protect>\d+) behavior_changed (?<behavior_changed>\d+)"
    if (!($Line -match $pattern)) {
        return $null
    }

    return [pscustomobject][ordered]@{
        LineIndex = $LineIndex
        ThreadId = $Matches["thid"].ToUpperInvariant()
        Lr = $Matches["lr"].ToUpperInvariant()
        Ctr = $Matches["ctr"].ToUpperInvariant()
        RequestedSize = $Matches["requested_size"].ToUpperInvariant()
        ProtectBits = $Matches["protect_bits"].ToUpperInvariant()
        AdjustedSize = $Matches["adjusted_size"].ToUpperInvariant()
        Result = $Matches["result"].ToUpperInvariant()
        ParentAddress = $Matches["parent"].ToUpperInvariant()
        PhysicalAllocationBase = $Matches["physical_allocation_base"].ToUpperInvariant()
        PhysicalAllocationSize = $Matches["physical_allocation_size"].ToUpperInvariant()
        BehaviorChanged = [int]$Matches["behavior_changed"]
    }
}

function Parse-FreeRow {
    param(
        [string]$Line,
        [int]$LineIndex
    )

    $pattern = "Xboxkrnl physical memory audit free phase (?<phase>\S+) thid (?<thid>[0-9A-Fa-f]{8}) lr (?<lr>[0-9A-Fa-f]{8}) ctr (?<ctr>[0-9A-Fa-f]{8}) r1 (?<r1>[0-9A-Fa-f]{8}) type (?<type>\d+) base_address (?<base>[0-9A-Fa-f]{8}) heap_present (?<heap_present>\d+) heap_type (?<heap_type>\S+) heap_base (?<heap_base>[0-9A-Fa-f]{8}) heap_page_size (?<heap_page_size>[0-9A-Fa-f]+) parent_address (?<parent>[0-9A-Fa-f]{8}) physical_allocation_base (?<physical_allocation_base>[0-9A-Fa-f]{8}) physical_allocation_size (?<physical_allocation_size>[0-9A-Fa-f]{8}) physical_region_size (?<physical_region_size>[0-9A-Fa-f]{8}) physical_state (?<physical_state>\d+) physical_protect (?<physical_protect>\d+) physical_region_start (?<physical_region_start>\d+) physical_page_aligned (?<physical_page_aligned>\d+) parent_allocation_base (?<parent_allocation_base>[0-9A-Fa-f]{8}) parent_allocation_size (?<parent_allocation_size>[0-9A-Fa-f]{8}) parent_region_size (?<parent_region_size>[0-9A-Fa-f]{8}) parent_state (?<parent_state>\d+) parent_protect (?<parent_protect>\d+) parent_region_start (?<parent_region_start>\d+) parent_page_aligned (?<parent_page_aligned>\d+) result_known (?<result_known>\d+) result (?<result>\d+) returned_region_size (?<returned_region_size>[0-9A-Fa-f]{8}) behavior_changed (?<behavior_changed>\d+)"
    if (!($Line -match $pattern)) {
        return $null
    }

    $baseAddress = $Matches["base"].ToUpperInvariant()
    $ownerBase = $Matches["physical_allocation_base"].ToUpperInvariant()
    $offset = [UInt64]0
    if ($ownerBase -ne "00000000") {
        $baseValue = Convert-HexValue $baseAddress
        $ownerValue = Convert-HexValue $ownerBase
        if ($baseValue -ge $ownerValue) {
            $offset = $baseValue - $ownerValue
        }
    }

    return [pscustomobject][ordered]@{
        LineIndex = $LineIndex
        Phase = $Matches["phase"]
        ThreadId = $Matches["thid"].ToUpperInvariant()
        Lr = $Matches["lr"].ToUpperInvariant()
        Ctr = $Matches["ctr"].ToUpperInvariant()
        R1 = $Matches["r1"].ToUpperInvariant()
        Type = [int]$Matches["type"]
        BaseAddress = $baseAddress
        ParentAddress = $Matches["parent"].ToUpperInvariant()
        PhysicalAllocationBase = $ownerBase
        PhysicalAllocationSize = $Matches["physical_allocation_size"].ToUpperInvariant()
        PhysicalRegionSize = $Matches["physical_region_size"].ToUpperInvariant()
        PhysicalRegionStart = [int]$Matches["physical_region_start"]
        PhysicalPageAligned = [int]$Matches["physical_page_aligned"]
        ResultKnown = [int]$Matches["result_known"]
        Result = [int]$Matches["result"]
        ReturnedRegionSize = $Matches["returned_region_size"].ToUpperInvariant()
        BehaviorChanged = [int]$Matches["behavior_changed"]
        OwnerOffset = $offset
    }
}

$resolvedLogPath = Resolve-LogPath $LogPath $PacketDir
if ($PacketDir) {
    $PacketDir = (Resolve-Path -LiteralPath $PacketDir).Path
}

$lines = Get-Content -LiteralPath $resolvedLogPath
$allocateRows = @()
$freeRows = @()
for ($lineIndex = 0; $lineIndex -lt $lines.Count; ++$lineIndex) {
    $allocateRow = Parse-AllocateRow $lines[$lineIndex] $lineIndex
    if ($allocateRow -ne $null) {
        $allocateRows += $allocateRow
    }

    $freeRow = Parse-FreeRow $lines[$lineIndex] $lineIndex
    if ($freeRow -ne $null) {
        $freeRows += $freeRow
    }
}

$allocationResults = @{}
foreach ($allocationRow in $allocateRows) {
    if ($allocationRow.Result -and $allocationRow.Result -ne "00000000" -and
            !$allocationResults.ContainsKey($allocationRow.Result)) {
        $allocationResults[$allocationRow.Result] = $allocationRow
    }
}

$freeRequestRows = @($freeRows | Where-Object { $_.Phase -eq "request" })
$freeResultRows = @($freeRows | Where-Object { $_.Phase -eq "result" })
$regionStartFreeRequestRows = @(
    $freeRequestRows | Where-Object {
        $_.PhysicalRegionStart -eq 1
    }
)
$exactAllocationFreeRequestRows = @(
    $freeRequestRows | Where-Object {
        $allocationResults.ContainsKey($_.BaseAddress)
    }
)
$failedInteriorRows = @(
    $freeResultRows | Where-Object {
        $_.ResultKnown -eq 1 -and $_.Result -eq 0 -and
        $_.PhysicalRegionStart -eq 0 -and
        $_.PhysicalAllocationBase -ne "00000000"
    }
)
$successfulFreeResultRows = @(
    $freeResultRows | Where-Object {
        $_.ResultKnown -eq 1 -and $_.Result -eq 1
    }
)
$failedExactAllocationRows = @(
    $failedInteriorRows | Where-Object {
        $allocationResults.ContainsKey($_.BaseAddress)
    }
)

$siteGroups = @{}
foreach ($row in $failedInteriorRows) {
    $siteKey = "{0}:{1}" -f $row.Lr, $row.Ctr
    if (!$siteGroups.ContainsKey($siteKey)) {
        $siteGroups[$siteKey] = @()
    }
    $siteGroups[$siteKey] = @($siteGroups[$siteKey]) + $row
}

$ownerGroups = @{}
foreach ($row in $failedInteriorRows) {
    $ownerKey = "{0}:{1}" -f $row.PhysicalAllocationBase, $row.PhysicalAllocationSize
    if (!$ownerGroups.ContainsKey($ownerKey)) {
        $ownerGroups[$ownerKey] = @()
    }
    $ownerGroups[$ownerKey] = @($ownerGroups[$ownerKey]) + $row
}

$topOwnerKey = ""
$topOwnerCount = 0
foreach ($ownerKey in $ownerGroups.Keys) {
    $count = @($ownerGroups[$ownerKey]).Count
    if ($count -gt $topOwnerCount) {
        $topOwnerKey = $ownerKey
        $topOwnerCount = $count
    }
}

$topOwnerAllocation = $null
if ($topOwnerKey) {
    $topOwnerBase = $topOwnerKey.Split(":")[0]
    if ($allocationResults.ContainsKey($topOwnerBase)) {
        $topOwnerAllocation = $allocationResults[$topOwnerBase]
    }
}

$classification = "no_physical_memory_audit_free_rows"
$reason = "no physical memory audit free rows were parsed"
$decision = "rerun_with_xboxkrnl_physical_memory_audit"
if ($freeRows.Count -gt 0) {
    $classification = "physical_memory_free_site_rows_present"
    $reason = "physical memory audit free rows were parsed"
    $decision = "inspect_free_site_rows"
}
if ($failedInteriorRows.Count -gt 0 -and $failedExactAllocationRows.Count -eq 0) {
    $classification = "project_sylpheed_failed_interior_frees_not_allocation_results"
    $reason = "failed interior frees are not exact MmAllocatePhysicalMemoryEx results"
    $decision = "map_guest_lr_ctr_for_suballocation_size_lifetime_before_behavior_patch"
}
if ($classification -eq "project_sylpheed_failed_interior_frees_not_allocation_results" -and
        $topOwnerCount -eq $failedInteriorRows.Count -and $topOwnerAllocation -ne $null) {
    $classification = "project_sylpheed_single_owner_guest_suballocator_free_sites"
    $reason = "all failed interior frees share one real owner allocation and zero failed frees match allocation results"
    $decision = "map_guest_free_site_821507bc_and_ctr_targets; no_heap_behavior_patch_without_size_lifetime"
}

$report = @(
    "classification=$classification",
    "reason=$reason",
    "decision=$decision",
    "packet_dir=$PacketDir",
    "log_path=$resolvedLogPath",
    "line_count=$($lines.Count)",
    "allocate_row_count=$($allocateRows.Count)",
    "allocation_result_count=$($allocationResults.Count)",
    "free_row_count=$($freeRows.Count)",
    "free_request_row_count=$($freeRequestRows.Count)",
    "free_result_row_count=$($freeResultRows.Count)",
    "region_start_free_request_count=$($regionStartFreeRequestRows.Count)",
    "exact_allocation_free_request_count=$($exactAllocationFreeRequestRows.Count)",
    "successful_free_result_count=$($successfulFreeResultRows.Count)",
    "failed_interior_free_count=$($failedInteriorRows.Count)",
    "failed_interior_exact_allocation_result_count=$($failedExactAllocationRows.Count)",
    "failed_free_site_count=$($siteGroups.Count)",
    "failed_owner_group_count=$($ownerGroups.Count)",
    "top_owner_group=$topOwnerKey",
    "top_owner_group_count=$topOwnerCount"
)

if ($topOwnerAllocation -ne $null) {
    $report += ("top_owner_allocation=line_index={0} thread={1} lr={2} ctr={3} requested_size={4} adjusted_size={5} result={6} parent={7} protect_bits={8} behavior_changed={9}" -f
        $topOwnerAllocation.LineIndex,
        $topOwnerAllocation.ThreadId,
        $topOwnerAllocation.Lr,
        $topOwnerAllocation.Ctr,
        $topOwnerAllocation.RequestedSize,
        $topOwnerAllocation.AdjustedSize,
        $topOwnerAllocation.Result,
        $topOwnerAllocation.ParentAddress,
        $topOwnerAllocation.ProtectBits,
        $topOwnerAllocation.BehaviorChanged)
}

$topSiteCount = [Math]::Max(1, $Top)
$siteSummaries = @()
foreach ($siteKey in $siteGroups.Keys) {
    $groupRows = @($siteGroups[$siteKey])
    $offsets = @($groupRows | ForEach-Object { $_.OwnerOffset } | Sort-Object)
    $baseAddresses = @($groupRows | ForEach-Object { $_.BaseAddress } | Sort-Object -Unique)
    $ownerKeys = @($groupRows | ForEach-Object { "{0}:{1}" -f $_.PhysicalAllocationBase, $_.PhysicalAllocationSize } | Sort-Object -Unique)
    $siteParts = $siteKey.Split(":")
    $siteSummaries += [pscustomobject][ordered]@{
        Key = $siteKey
        Lr = $siteParts[0]
        Ctr = $siteParts[1]
        Count = $groupRows.Count
        OwnerCount = $ownerKeys.Count
        OwnerKeys = ($ownerKeys -join ",")
        OffsetMin = if ($offsets.Count) { $offsets[0] } else { [UInt64]0 }
        OffsetMax = if ($offsets.Count) { $offsets[$offsets.Count - 1] } else { [UInt64]0 }
        FirstBaseAddresses = (@($baseAddresses | Select-Object -First 6) -join ",")
        FirstLine = @($groupRows | Sort-Object LineIndex | Select-Object -First 1)[0].LineIndex
    }
}

$rankedSites = @($siteSummaries | Sort-Object @{Expression = "Count"; Descending = $true}, @{Expression = "Lr"; Descending = $false}, @{Expression = "Ctr"; Descending = $false} | Select-Object -First $topSiteCount)
$siteIndex = 0
foreach ($site in $rankedSites) {
    $report += ("free_site[{0}]=lr={1} ctr={2} count={3} owner_count={4} owners={5} offset_min={6} offset_max={7} first_bases={8} first_line={9}" -f
        $siteIndex,
        $site.Lr,
        $site.Ctr,
        $site.Count,
        $site.OwnerCount,
        $site.OwnerKeys,
        (Format-HexValue $site.OffsetMin),
        (Format-HexValue $site.OffsetMax),
        $site.FirstBaseAddresses,
        $site.FirstLine)
    ++$siteIndex
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
