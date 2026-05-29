param(
    [string]$PacketDir = "",
    [string]$LogPath = "",
    [string]$OutPath = "",
    [int]$TopWaitGroups = 8
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

function Try-ParseLogcatTimestamp {
    param([string]$Line)

    if ($Line -notmatch '^(?<month>\d{2})-(?<day>\d{2})\s+(?<clock>\d{2}:\d{2}:\d{2}\.\d{3})') {
        return $null
    }

    $year = (Get-Date).Year
    $text = "{0:D4}-{1}-{2} {3}" -f $year, $Matches.month, $Matches.day, $Matches.clock
    $parsed = [DateTime]::MinValue
    if ([DateTime]::TryParseExact(
            $text,
            "yyyy-MM-dd HH:mm:ss.fff",
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::AssumeLocal,
            [ref]$parsed)) {
        return $parsed
    }
    return $null
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

function Convert-SignedImmediate {
    param([string]$Text)

    $valueText = $Text.Trim()
    $sign = [Int64]1
    if ($valueText.StartsWith("-")) {
        $sign = -1
        $valueText = $valueText.Substring(1)
    }

    if ($valueText.StartsWith("0x", [StringComparison]::OrdinalIgnoreCase)) {
        return $sign * [Int64]([Convert]::ToInt64($valueText.Substring(2), 16))
    }
    return $sign * [Int64]::Parse(
        $valueText,
        [Globalization.CultureInfo]::InvariantCulture)
}

function Add-HexOffset {
    param(
        [string]$HexText,
        [UInt64]$Offset
    )

    return Format-HexValue ((Convert-HexValue $HexText) + $Offset)
}

function Parse-FreeRow {
    param(
        [string]$Line,
        [int]$LineIndex
    )

    $pattern = "Xboxkrnl physical memory audit free phase (?<phase>\S+) thid (?<thid>[0-9A-Fa-f]{8}) lr (?<lr>[0-9A-Fa-f]{8}) ctr (?<ctr>[0-9A-Fa-f]{8}) r1 (?<r1>[0-9A-Fa-f]{8}) type (?<type>\d+) base_address (?<base>[0-9A-Fa-f]{8}) heap_present (?<heap_present>\d+) heap_type (?<heap_type>\S+) heap_base (?<heap_base>[0-9A-Fa-f]{8}) heap_page_size (?<heap_page_size>[0-9A-Fa-f]+) parent_address (?<parent>[0-9A-Fa-f]{8}) physical_allocation_base (?<physical_allocation_base>[0-9A-Fa-f]{8}) physical_allocation_size (?<physical_allocation_size>[0-9A-Fa-f]{8}) physical_region_size (?<physical_region_size>[0-9A-Fa-f]{8}) physical_state (?<physical_state>\d+) physical_protect (?<physical_protect>\d+) physical_region_start (?<physical_region_start>\d+) physical_page_aligned (?<physical_page_aligned>\d+) parent_allocation_base (?<parent_allocation_base>[0-9A-Fa-f]{8}) parent_allocation_size (?<parent_allocation_size>[0-9A-Fa-f]{8}) parent_region_size (?<parent_region_size>[0-9A-Fa-f]{8}) parent_state (?<parent_state>\d+) parent_protect (?<parent_protect>\d+) parent_region_start (?<parent_region_start>\d+) parent_page_aligned (?<parent_page_aligned>\d+) result_known (?<result_known>\d+) result (?<result>\d+) returned_region_size (?<returned_region_size>[0-9A-Fa-f]{8}) behavior_changed (?<behavior_changed>\d+)"
    if ($Line -notmatch $pattern) {
        return $null
    }

    $baseAddress = $Matches.base.ToUpperInvariant()
    $ownerBase = $Matches.physical_allocation_base.ToUpperInvariant()
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
        Timestamp = Try-ParseLogcatTimestamp $Line
        Phase = $Matches.phase
        ThreadId = $Matches.thid.ToUpperInvariant()
        Lr = $Matches.lr.ToUpperInvariant()
        Ctr = $Matches.ctr.ToUpperInvariant()
        R1 = $Matches.r1.ToUpperInvariant()
        BaseAddress = $baseAddress
        ParentAddress = $Matches.parent.ToUpperInvariant()
        PhysicalAllocationBase = $ownerBase
        PhysicalAllocationSize = $Matches.physical_allocation_size.ToUpperInvariant()
        PhysicalRegionSize = $Matches.physical_region_size.ToUpperInvariant()
        PhysicalRegionStart = [int]$Matches.physical_region_start
        ResultKnown = [int]$Matches.result_known
        Result = [int]$Matches.result
        ReturnedRegionSize = $Matches.returned_region_size.ToUpperInvariant()
        BehaviorChanged = [int]$Matches.behavior_changed
        OwnerOffset = $offset
    }
}

function Parse-WaitRow {
    param(
        [string]$Line,
        [int]$LineIndex
    )

    $pattern = "Xboxkrnl wait trace (?<api>\S+) (?<phase>\S+) thid (?<thid>[0-9A-Fa-f]{8}) handle (?<handle>[0-9A-Fa-f]{8}) guest_object (?<guest_object>[0-9A-Fa-f]{8}) type (?<type>\S+) count (?<count>\d+) wait_type (?<wait_type>\d+) alertable (?<alertable>\d+) timeout (?<timeout>[0-9A-Fa-f]{16}) status (?<status>[0-9A-Fa-f]{8}) lr (?<lr>[0-9A-Fa-f]{8}) ctr (?<ctr>[0-9A-Fa-f]{8}) r1 (?<r1>[0-9A-Fa-f]{8}) name '(?<name>[^']*)'"
    if ($Line -notmatch $pattern) {
        return $null
    }

    return [pscustomobject][ordered]@{
        LineIndex = $LineIndex
        Timestamp = Try-ParseLogcatTimestamp $Line
        Api = $Matches.api
        Phase = $Matches.phase
        ThreadId = $Matches.thid.ToUpperInvariant()
        Handle = $Matches.handle.ToUpperInvariant()
        GuestObject = $Matches.guest_object.ToUpperInvariant()
        ObjectType = $Matches.type
        Count = [int]$Matches.count
        WaitType = [int]$Matches.wait_type
        Timeout = $Matches.timeout.ToUpperInvariant()
        Status = $Matches.status.ToUpperInvariant()
        Lr = $Matches.lr.ToUpperInvariant()
        Ctr = $Matches.ctr.ToUpperInvariant()
        R1 = $Matches.r1.ToUpperInvariant()
        Name = $Matches.name
    }
}

function Parse-A64SnapshotRow {
    param(
        [string]$Line,
        [int]$LineIndex
    )

    $pattern = "A64 thread snapshot tid=(?<tid>[0-9A-Fa-f]+) handle=(?<handle>[0-9A-Fa-f]+) native=(?<native>[0-9A-Fa-f]+) state=(?<state>\w+) last_fn=(?<last_fn>[0-9A-Fa-f]+) last_ret=(?<last_ret>[0-9A-Fa-f]+) lr=(?<lr>[0-9A-Fa-f]+) ctr=(?<ctr>[0-9A-Fa-f]+) r1=(?<r1>[0-9A-Fa-f]+) r3=(?<r3>[0-9A-Fa-f]+) r4=(?<r4>[0-9A-Fa-f]+)"
    if ($Line -notmatch $pattern) {
        return $null
    }

    return [pscustomobject][ordered]@{
        LineIndex = $LineIndex
        Timestamp = Try-ParseLogcatTimestamp $Line
        ThreadId = $Matches.tid.ToUpperInvariant().PadLeft(8, "0")
        Handle = $Matches.handle.ToUpperInvariant()
        NativeThread = $Matches.native.ToUpperInvariant()
        State = $Matches.state
        LastFn = $Matches.last_fn.ToUpperInvariant()
        LastRet = $Matches.last_ret.ToUpperInvariant()
        Lr = $Matches.lr.ToUpperInvariant()
        Ctr = $Matches.ctr.ToUpperInvariant()
        R1 = $Matches.r1.ToUpperInvariant()
        R3 = $Matches.r3.ToUpperInvariant()
        R4 = $Matches.r4.ToUpperInvariant()
    }
}

function Parse-PpcDumpRow {
    param([string]$Line)

    if ($Line -notmatch "Filtered function dump 821511D0 PPC:\s+(?<text>.*)$") {
        return $null
    }

    $text = $Matches.text.TrimEnd()
    if ($text -notmatch "^(?<addr>[0-9A-Fa-f]{8})\s+(?:(?<opcode>[0-9A-Fa-f]{8})\s+)?(?<asm>.*)$") {
        return $null
    }

    return [pscustomobject][ordered]@{
        Address = $Matches.addr.ToUpperInvariant()
        Opcode = if ($Matches.opcode) { $Matches.opcode.ToUpperInvariant() } else { "" }
        Asm = $Matches.asm.Trim()
    }
}

function Get-PpcRow {
    param(
        [hashtable]$RowsByAddress,
        [string]$Address
    )

    if ($RowsByAddress.ContainsKey($Address)) {
        return $RowsByAddress[$Address]
    }
    return $null
}

function Parse-PpcTargetFlow {
    param(
        [string]$Target,
        [hashtable]$RowsByAddress
    )

    $addisRow = Get-PpcRow $RowsByAddress $Target
    $addiRow = Get-PpcRow $RowsByAddress (Add-HexOffset $Target 8)
    $callOneRow = Get-PpcRow $RowsByAddress (Add-HexOffset $Target 0x10)
    $branchRow = Get-PpcRow $RowsByAddress (Add-HexOffset $Target 0x1C)
    $callTwoRow = Get-PpcRow $RowsByAddress (Add-HexOffset $Target 0x28)
    $exitRow = Get-PpcRow $RowsByAddress (Add-HexOffset $Target 0x2C)

    $addisImmediate = $null
    $addiImmediate = $null
    $computedOffset = [UInt64]0
    $computedKnown = $false
    if ($addisRow -and $addisRow.Asm -match "addis\s+r30,\s*r30,\s*(?<imm>0x[0-9A-Fa-f]+)") {
        $addisImmediate = Convert-SignedImmediate $Matches.imm
    }
    if ($addiRow -and $addiRow.Asm -match "addi\s+r30,\s*r30,\s*(?<imm>-?0x[0-9A-Fa-f]+|-?\d+)") {
        $addiImmediate = Convert-SignedImmediate $Matches.imm
    }
    if ($addisImmediate -ne $null -and $addiImmediate -ne $null) {
        $computedValue = ([Int64]$addisImmediate * 0x10000) + [Int64]$addiImmediate
        if ($computedValue -ge 0) {
            $computedOffset = [UInt64]$computedValue
            $computedKnown = $true
        }
    }

    $callOne = ""
    if ($callOneRow -and $callOneRow.Asm -match "bl\s+0x(?<target>[0-9A-Fa-f]+)") {
        $callOne = $Matches.target.ToUpperInvariant()
    }
    $callTwo = ""
    if ($callTwoRow -and $callTwoRow.Asm -match "bl\s+0x(?<target>[0-9A-Fa-f]+)") {
        $callTwo = $Matches.target.ToUpperInvariant()
    }
    $branchTarget = ""
    if ($branchRow -and $branchRow.Asm -match "bne\s+crf6,\s*0x(?<target>[0-9A-Fa-f]+)") {
        $branchTarget = $Matches.target.ToUpperInvariant()
    }
    $exitTarget = ""
    if ($exitRow -and $exitRow.Asm -match "b\s+0x(?<target>[0-9A-Fa-f]+)") {
        $exitTarget = $Matches.target.ToUpperInvariant()
    }

    return [pscustomobject][ordered]@{
        Target = $Target
        PpcMatched = [bool]($addisRow -and $addiRow -and $callOneRow -and $branchRow -and $callTwoRow -and $exitRow)
        ComputedOffsetKnown = $computedKnown
        ComputedOffset = $computedOffset
        CallOne = $callOne
        CallTwo = $callTwo
        BranchOnNonZeroTarget = $branchTarget
        ExitTarget = $exitTarget
    }
}

function Find-OffsetSourceFlow {
    param(
        [string]$Target,
        [UInt64]$OwnerOffset,
        [hashtable]$RowsByAddress
    )

    $candidateOffsets = @(0, 0x30, -0x30)
    foreach ($candidateOffset in $candidateOffsets) {
        $candidateValue = [Int64](Convert-HexValue $Target) + [Int64]$candidateOffset
        if ($candidateValue -lt 0) {
            continue
        }
        $candidate = Format-HexValue ([UInt64]$candidateValue)
        $candidateFlow = Parse-PpcTargetFlow $candidate $RowsByAddress
        if ($candidateFlow.ComputedOffsetKnown -and
                $candidateFlow.ComputedOffset -eq $OwnerOffset) {
            return [pscustomobject][ordered]@{
                Target = $candidate
                Flow = $candidateFlow
            }
        }
    }

    return [pscustomobject][ordered]@{
        Target = ""
        Flow = $null
    }
}

function Parse-HirTargetStats {
    param(
        [string[]]$Lines,
        [string[]]$Targets
    )

    $targetSet = @{}
    $stats = @{}
    foreach ($target in $Targets) {
        $targetSet[$target] = $true
        $stats[$target] = [pscustomobject][ordered]@{
            Blocks = 0
            HirInstrs = 0
            ContextBarriers = 0
            Calls = 0
            ExitBranches = 0
        }
    }

    $currentTarget = ""
    foreach ($line in $Lines) {
        if ($line -match "Filtered function dump 821511D0 RawHIR:\s+;\s+block_profile .*first_source=(?<first>[0-9A-Fa-f]{8}) .*last_source=(?<last>[0-9A-Fa-f]{8}) .*hir_instrs=(?<hir>\d+)") {
            $currentTarget = ""
            $firstSource = $Matches.first.ToUpperInvariant()
            $firstValue = Convert-HexValue $firstSource
            foreach ($target in $Targets) {
                $targetValue = Convert-HexValue $target
                if ($firstValue -ge $targetValue -and $firstValue -le ($targetValue + 0x2C)) {
                    $currentTarget = $target
                    ++$stats[$target].Blocks
                    $stats[$target].HirInstrs += [int]$Matches.hir
                    break
                }
            }
            continue
        }

        if (!$currentTarget) {
            continue
        }
        if ($line -match "Filtered function dump 821511D0 RawHIR:\s+.*context_barrier") {
            ++$stats[$currentTarget].ContextBarriers
        }
        if ($line -match "Filtered function dump 821511D0 RawHIR:\s+.*\bcall") {
            ++$stats[$currentTarget].Calls
        }
        if ($line -match "Filtered function dump 821511D0 RawHIR:\s+.*branch loc_82151900") {
            ++$stats[$currentTarget].ExitBranches
        }
    }

    return $stats
}

function Format-TimeValue {
    param([object]$Value)
    if (!$Value) {
        return ""
    }
    return $Value.ToString("HH:mm:ss.fff", [Globalization.CultureInfo]::InvariantCulture)
}

if ($PacketDir) {
    $PacketDir = (Resolve-Path -LiteralPath $PacketDir).Path
}
$resolvedLogPath = Resolve-LogPath $LogPath $PacketDir
$lines = Get-Content -LiteralPath $resolvedLogPath

$freeRows = @()
$waitRows = @()
$snapshotRows = @()
$ppcRowsByAddress = @{}
$vdSwapRows = @()
for ($lineIndex = 0; $lineIndex -lt $lines.Count; ++$lineIndex) {
    $line = $lines[$lineIndex]
    $freeRow = Parse-FreeRow $line $lineIndex
    if ($freeRow -ne $null) {
        $freeRows += $freeRow
    }

    $waitRow = Parse-WaitRow $line $lineIndex
    if ($waitRow -ne $null) {
        $waitRows += $waitRow
    }

    $snapshotRow = Parse-A64SnapshotRow $line $lineIndex
    if ($snapshotRow -ne $null) {
        $snapshotRows += $snapshotRow
    }

    $ppcRow = Parse-PpcDumpRow $line
    if ($ppcRow -ne $null) {
        if (!$ppcRowsByAddress.ContainsKey($ppcRow.Address)) {
            $ppcRowsByAddress[$ppcRow.Address] = $ppcRow
        } elseif (!$ppcRowsByAddress[$ppcRow.Address].Opcode -and $ppcRow.Opcode) {
            $ppcRowsByAddress[$ppcRow.Address] = $ppcRow
        }
    }

    if ($line -match '\bVdSwap\b') {
        $vdSwapRows += [pscustomobject][ordered]@{
            LineIndex = $lineIndex
            Timestamp = Try-ParseLogcatTimestamp $line
        }
    }
}

$failedFreeRows = @(
    $freeRows | Where-Object {
        $_.Phase -eq "result" -and $_.ResultKnown -eq 1 -and $_.Result -eq 0 -and
        $_.PhysicalRegionStart -eq 0 -and $_.PhysicalAllocationBase -ne "00000000"
    } | Sort-Object LineIndex
)
$failedTargets = @($failedFreeRows | ForEach-Object { $_.Ctr } | Sort-Object -Unique)
$hirStats = Parse-HirTargetStats $lines $failedTargets

$firstFailed = $null
$lastFailed = $null
if ($failedFreeRows.Count -gt 0) {
    $firstFailed = $failedFreeRows[0].Timestamp
    $lastFailed = $failedFreeRows[$failedFreeRows.Count - 1].Timestamp
}

$waitAfterLast = @()
$snapshotAfterLast = @()
$vdSwapAfterLast = @()
if ($lastFailed) {
    $waitAfterLast = @($waitRows | Where-Object { $_.Timestamp -and $_.Timestamp -gt $lastFailed })
    $snapshotAfterLast = @($snapshotRows | Where-Object { $_.Timestamp -and $_.Timestamp -gt $lastFailed })
    $vdSwapAfterLast = @($vdSwapRows | Where-Object { $_.Timestamp -and $_.Timestamp -gt $lastFailed })
}

$vdSwapFirstTime = ""
$vdSwapLastTime = ""
if ($vdSwapRows.Count -gt 0) {
    $vdSwapFirstTime = Format-TimeValue $vdSwapRows[0].Timestamp
    $vdSwapLastTime = Format-TimeValue $vdSwapRows[$vdSwapRows.Count - 1].Timestamp
}

$ppcMatchCount = 0
$offsetMatchCount = 0
$nextCaseOffsetMatchCount = 0
$flowRows = @()
foreach ($target in $failedTargets) {
    $targetFreeRows = @($failedFreeRows | Where-Object { $_.Ctr -eq $target })
    $flow = Parse-PpcTargetFlow $target $ppcRowsByAddress
    if ($flow.PpcMatched) {
        ++$ppcMatchCount
    }
    $offsetMatch = $false
    $sourceTarget = ""
    $sourceComputedOffsetKnown = $false
    $sourceComputedOffset = [UInt64]0
    $firstTargetFreeRow = @($targetFreeRows | Select-Object -First 1)[0]
    if ($flow.ComputedOffsetKnown) {
        $offsetMatch = $true
        foreach ($row in $targetFreeRows) {
            if ($row.OwnerOffset -ne $flow.ComputedOffset) {
                $offsetMatch = $false
                break
            }
        }
    }
    if (!$offsetMatch) {
        $offsetSource = Find-OffsetSourceFlow $target $firstTargetFreeRow.OwnerOffset $ppcRowsByAddress
        if ($offsetSource.Flow -ne $null) {
            $offsetMatch = $true
            $sourceTarget = $offsetSource.Target
            $sourceComputedOffsetKnown = $offsetSource.Flow.ComputedOffsetKnown
            $sourceComputedOffset = $offsetSource.Flow.ComputedOffset
            if ($sourceTarget -eq (Add-HexOffset $target 0x30)) {
                ++$nextCaseOffsetMatchCount
            }
        }
    } else {
        $sourceTarget = $target
        $sourceComputedOffsetKnown = $flow.ComputedOffsetKnown
        $sourceComputedOffset = $flow.ComputedOffset
    }
    if ($offsetMatch) {
        ++$offsetMatchCount
    }
    $hir = $hirStats[$target]
    $flowRows += [pscustomobject][ordered]@{
        Target = $target
        Count = $targetFreeRows.Count
        Threads = (@($targetFreeRows | ForEach-Object { $_.ThreadId } | Sort-Object -Unique) -join ",")
        FirstBase = $firstTargetFreeRow.BaseAddress
        Owner = ("{0}:{1}" -f $firstTargetFreeRow.PhysicalAllocationBase, $firstTargetFreeRow.PhysicalAllocationSize)
        OwnerOffset = $firstTargetFreeRow.OwnerOffset
        PpcMatched = $flow.PpcMatched
        ComputedOffsetKnown = $flow.ComputedOffsetKnown
        ComputedOffset = $flow.ComputedOffset
        OffsetSourceTarget = $sourceTarget
        SourceComputedOffsetKnown = $sourceComputedOffsetKnown
        SourceComputedOffset = $sourceComputedOffset
        OffsetMatchesFree = $offsetMatch
        CallOne = $flow.CallOne
        CallTwo = $flow.CallTwo
        BranchOnNonZeroTarget = $flow.BranchOnNonZeroTarget
        ExitTarget = $flow.ExitTarget
        HirBlocks = $hir.Blocks
        HirInstrs = $hir.HirInstrs
        HirContextBarriers = $hir.ContextBarriers
        HirCalls = $hir.Calls
        HirExitBranches = $hir.ExitBranches
    }
}

$waitFirstTime = ""
$waitLastTime = ""
if ($waitRows.Count -gt 0) {
    $waitFirstTime = Format-TimeValue $waitRows[0].Timestamp
    $waitLastTime = Format-TimeValue $waitRows[$waitRows.Count - 1].Timestamp
}
$waitBudgetBeforeLastFailed = 0
if ($lastFailed -and $waitRows.Count -gt 0 -and $waitRows[$waitRows.Count - 1].Timestamp -lt $lastFailed) {
    $waitBudgetBeforeLastFailed = 1
}

$snapshotGroupRows = @()
foreach ($snapshot in $snapshotAfterLast) {
    $key = "{0}:{1}:{2}:{3}" -f $snapshot.ThreadId, $snapshot.LastFn, $snapshot.LastRet, $snapshot.Ctr
    $existing = @($snapshotGroupRows | Where-Object { $_.Key -eq $key } | Select-Object -First 1)
    if ($existing.Count -eq 0) {
        $snapshotGroupRows += [pscustomobject][ordered]@{
            Key = $key
            ThreadId = $snapshot.ThreadId
            LastFn = $snapshot.LastFn
            LastRet = $snapshot.LastRet
            Ctr = $snapshot.Ctr
            Count = 1
            FirstTime = $snapshot.Timestamp
            LastTime = $snapshot.Timestamp
        }
    } else {
        $existing[0].Count += 1
        $existing[0].LastTime = $snapshot.Timestamp
    }
}

$waitGroupRows = @()
foreach ($wait in $waitRows) {
    $key = "{0}:{1}:{2}:{3}:{4}" -f $wait.ThreadId, $wait.Api, $wait.Phase, $wait.Status, $wait.Lr
    $existing = @($waitGroupRows | Where-Object { $_.Key -eq $key } | Select-Object -First 1)
    if ($existing.Count -eq 0) {
        $waitGroupRows += [pscustomobject][ordered]@{
            Key = $key
            ThreadId = $wait.ThreadId
            Api = $wait.Api
            Phase = $wait.Phase
            Status = $wait.Status
            Lr = $wait.Lr
            Ctr = $wait.Ctr
            Count = 1
            FirstTime = $wait.Timestamp
            LastTime = $wait.Timestamp
        }
    } else {
        $existing[0].Count += 1
        $existing[0].LastTime = $wait.Timestamp
    }
}

$classification = "project_sylpheed_free_site_flow_join_incomplete"
$reason = "missing failed physical frees or filtered PPC target matches"
$decision = "rerun_with_physical_memory_audit_and_filtered_free_site_dump"
$offsetUnmatchedCount = $failedTargets.Count - $offsetMatchCount
if ($failedFreeRows.Count -gt 0 -and $ppcMatchCount -eq $failedTargets.Count -and
        $failedTargets.Count -gt 0) {
    if ($offsetMatchCount -eq $failedTargets.Count) {
        $classification = "project_sylpheed_guest_suballocator_offsets_mapped"
        $reason = "all failed interior frees map to filtered PPC table blocks whose computed r30 offsets match the failed owner offsets"
    } elseif ($offsetMatchCount -gt 0) {
        $classification = "project_sylpheed_guest_suballocator_table_mapped_partial_offsets"
        $reason = "all failed interior frees map to filtered PPC table blocks, with most computed r30 offsets matching the failed owner offsets"
    }
    if ($classification -ne "project_sylpheed_free_site_flow_join_incomplete") {
        $decision = "do_not_round_down_or_hide_physical_frees; inspect suballocation table and late wait/presentation before behavior"
    }
}
if ($classification -ne "project_sylpheed_free_site_flow_join_incomplete" -and
        $waitAfterLast.Count -gt 0 -and $vdSwapAfterLast.Count -gt 0 -and
        $snapshotAfterLast.Count -gt 0) {
    $classification = "project_sylpheed_guest_suballocator_table_with_live_wait_loop"
    $reason = "free-site table rows are mapped, wait trace covers the post-free window, and VdSwap/A64 snapshots continue afterward"
    $decision = "design_explicit_physical_suballocation_release_or_prove_presentation_stall_independence_before_behavior"
} elseif ($classification -ne "project_sylpheed_free_site_flow_join_incomplete" -and
        $waitBudgetBeforeLastFailed -eq 1 -and $vdSwapRows.Count -gt 100 -and
        $snapshotAfterLast.Count -gt 0) {
    $classification = "project_sylpheed_guest_suballocator_offsets_with_unattributed_live_black_loop"
    $reason = "free-site offsets are mapped, but wait trace ends before the final failed free while VdSwap spans the black-screen window and A64 snapshots continue afterward"
    $decision = "next_capture_or_audit_should_target_late_guest_pcs_824AB214_824AC578_8272D048_82762158_and_delay_wait_trace"
}

$report = @(
    "classification=$classification",
    "reason=$reason",
    "decision=$decision",
    "packet_dir=$PacketDir",
    "log_path=$resolvedLogPath",
    "line_count=$($lines.Count)",
    "failed_interior_free_count=$($failedFreeRows.Count)",
    "failed_ctr_target_count=$($failedTargets.Count)",
    "ppc_target_match_count=$ppcMatchCount",
    "offset_match_count=$offsetMatchCount",
    "offset_unmatched_count=$offsetUnmatchedCount",
    "next_case_offset_match_count=$nextCaseOffsetMatchCount",
    "first_failed_free_time=$(Format-TimeValue $firstFailed)",
    "last_failed_free_time=$(Format-TimeValue $lastFailed)",
    "wait_trace_count=$($waitRows.Count)",
    "wait_trace_first_time=$waitFirstTime",
    "wait_trace_last_time=$waitLastTime",
    "wait_trace_after_last_failed_free_count=$($waitAfterLast.Count)",
    "wait_budget_exhausted_before_last_failed_free=$waitBudgetBeforeLastFailed",
    "a64_snapshot_count=$($snapshotRows.Count)",
    "a64_snapshot_after_last_failed_free_count=$($snapshotAfterLast.Count)",
    "vdswap_count=$($vdSwapRows.Count)",
    "vdswap_first_time=$vdSwapFirstTime",
    "vdswap_last_time=$vdSwapLastTime",
    "vdswap_after_last_failed_free_count=$($vdSwapAfterLast.Count)"
)

$flowIndex = 0
foreach ($flowRow in ($flowRows | Sort-Object Target)) {
    $computedOffsetText = ""
    if ($flowRow.ComputedOffsetKnown) {
        $computedOffsetText = Format-HexValue $flowRow.ComputedOffset
    }
    $sourceComputedOffsetText = ""
    if ($flowRow.SourceComputedOffsetKnown) {
        $sourceComputedOffsetText = Format-HexValue $flowRow.SourceComputedOffset
    }
    $report += ("free_site_flow[{0}]=ctr={1} count={2} threads={3} first_base={4} owner={5} owner_offset={6} ppc_match={7} ctr_computed_offset={8} offset_source={9} source_computed_offset={10} offset_match={11} call_one={12} call_two={13} branch_nonzero={14} exit={15} hir_blocks={16} hir_instrs={17} hir_calls={18} hir_context_barriers={19}" -f
        $flowIndex,
        $flowRow.Target,
        $flowRow.Count,
        $flowRow.Threads,
        $flowRow.FirstBase,
        $flowRow.Owner,
        (Format-HexValue $flowRow.OwnerOffset),
        ([int]$flowRow.PpcMatched),
        $computedOffsetText,
        $flowRow.OffsetSourceTarget,
        $sourceComputedOffsetText,
        ([int]$flowRow.OffsetMatchesFree),
        $flowRow.CallOne,
        $flowRow.CallTwo,
        $flowRow.BranchOnNonZeroTarget,
        $flowRow.ExitTarget,
        $flowRow.HirBlocks,
        $flowRow.HirInstrs,
        $flowRow.HirCalls,
        $flowRow.HirContextBarriers)
    ++$flowIndex
}

$waitIndex = 0
foreach ($waitGroup in ($waitGroupRows | Sort-Object @{Expression = "Count"; Descending = $true} | Select-Object -First ([Math]::Max(1, $TopWaitGroups)))) {
    $report += ("wait_group[{0}]=thread={1} api={2} phase={3} status={4} lr={5} ctr={6} count={7} first={8} last={9}" -f
        $waitIndex,
        $waitGroup.ThreadId,
        $waitGroup.Api,
        $waitGroup.Phase,
        $waitGroup.Status,
        $waitGroup.Lr,
        $waitGroup.Ctr,
        $waitGroup.Count,
        (Format-TimeValue $waitGroup.FirstTime),
        (Format-TimeValue $waitGroup.LastTime))
    ++$waitIndex
}

$snapshotIndex = 0
foreach ($snapshotGroup in ($snapshotGroupRows | Sort-Object @{Expression = "Count"; Descending = $true}, ThreadId | Select-Object -First 12)) {
    $report += ("post_failed_snapshot_group[{0}]=thread={1} last_fn={2} last_ret={3} ctr={4} count={5} first={6} last={7}" -f
        $snapshotIndex,
        $snapshotGroup.ThreadId,
        $snapshotGroup.LastFn,
        $snapshotGroup.LastRet,
        $snapshotGroup.Ctr,
        $snapshotGroup.Count,
        (Format-TimeValue $snapshotGroup.FirstTime),
        (Format-TimeValue $snapshotGroup.LastTime))
    ++$snapshotIndex
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
