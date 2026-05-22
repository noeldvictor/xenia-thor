param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [ValidateRange(0, 31)]
    [int]$Gpr = 1,
    [int]$Top = 30
)

$ErrorActionPreference = "Stop"

function Add-Count {
    param(
        [hashtable]$Table,
        [string]$Key,
        [int]$Amount = 1
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

function Add-UniqueString {
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

function Get-HirValueSize {
    param([string]$Text)

    if ($Text -match "\.v128\b") { return 16 }
    if ($Text -match "\.(i64|f64)\b") { return 8 }
    if ($Text -match "\.(i32|f32)\b") { return 4 }
    if ($Text -match "\.i16\b") { return 2 }
    if ($Text -match "\.i8\b") { return 1 }
    return 1
}

function Test-Overlap {
    param(
        [int]$OffsetA,
        [int]$SizeA,
        [int]$OffsetB,
        [int]$SizeB
    )
    return ($OffsetA -lt ($OffsetB + $SizeB)) -and (($OffsetA + $SizeA) -gt $OffsetB)
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
    return "unknown"
}

function New-HirBlock {
    param(
        [int]$Index,
        [string]$Name
    )
    return [ordered]@{
        index = $Index
        name = $Name
        incoming = New-Object System.Collections.Generic.List[string]
        outgoing = New-Object System.Collections.Generic.List[string]
        instructions = New-Object System.Collections.Generic.List[object]
        target_loads = 0
        target_stores = 0
        target_alias_stores = 0
        calls = 0
        barriers = 0
        branches = 0
        exits = 0
    }
}

function New-HirInstruction {
    param(
        [int]$Line,
        [int]$Index,
        [string]$Text,
        [int]$TargetOffset,
        [int]$TargetSize
    )

    $isLoad = $false
    $isStore = $false
    $offset = $null
    if ($Text -match "load_context \+([0-9]+)") {
        $isLoad = $true
        $offset = [int]$Matches[1]
    } elseif ($Text -match "store_context \+([0-9]+)") {
        $isStore = $true
        $offset = [int]$Matches[1]
    }

    $size = if ($null -ne $offset) { Get-HirValueSize $Text } else { 0 }
    $isTargetExact = ($null -ne $offset -and $offset -eq $TargetOffset -and $size -eq $TargetSize)
    $isTargetOverlap = ($null -ne $offset -and (Test-Overlap $offset $size $TargetOffset $TargetSize))
    $branchTarget = ""
    if ($Text -match "^\s*branch(?:_(?:true|false))?(?:\s+\S+,)?\s+(?<target><block[0-9]+>|loc_[0-9A-Fa-f]+|_label[0-9A-Fa-f]+|label[0-9]+)\b") {
        $branchTarget = $Matches.target
    }

    return [pscustomobject]@{
        Line = $Line
        Index = $Index
        Text = $Text.Trim()
        IsLoad = $isLoad
        IsStore = $isStore
        Offset = $offset
        Size = $size
        IsTargetExact = $isTargetExact
        IsTargetOverlap = $isTargetOverlap
        IsBarrier = ($Text -match "\bcontext_barrier\b")
        IsCall = ($Text -match "\bcall(\.[0-9]+)?(_true|_indirect|_indirect_true|_extern)?\b")
        IsBranch = ($Text -match "\bbranch(_true|_false)?\b")
        BranchTarget = $branchTarget
        IsExit = ($Text -match "\b(return|trap|debugbreak|debug_break)\b")
    }
}

function Get-EdgeKey {
    param(
        [string]$From,
        [string]$To
    )
    return ("{0}->{1}" -f $From, $To)
}

function Add-Edge {
    param(
        [hashtable]$Edges,
        [string]$From,
        [string]$To
    )
    if ([string]::IsNullOrWhiteSpace($From) -or [string]::IsNullOrWhiteSpace($To)) {
        return
    }
    $key = Get-EdgeKey $From $To
    if (!$Edges.ContainsKey($key)) {
        $Edges[$key] = [ordered]@{
            from = $From
            to = $To
        }
    }
}

function Invoke-Transfer {
    param(
        [object]$Block,
        [bool]$EntryClean,
        [string]$EntryReason,
        [bool]$KillOnBarrier,
        [bool]$CollectEvents
    )

    $clean = $EntryClean
    $reason = if ($EntryClean) { $EntryReason } else { "entry_unknown" }
    $source = if ($EntryClean) { "entry" } else { "none" }
    $edgeStates = @{}
    $events = New-Object System.Collections.Generic.List[object]
    $firstTargetLoadSeen = $false

    foreach ($instruction in $Block.instructions) {
        if ($instruction.IsBarrier) {
            if ($KillOnBarrier) {
                $clean = $false
                $reason = "context_barrier"
                $source = "none"
            }
        }
        if ($instruction.IsCall) {
            $clean = $false
            $reason = "call"
            $source = "none"
        }
        if ($instruction.IsExit) {
            $clean = $false
            $reason = "exit_or_exception"
            $source = "none"
        }

        if ($instruction.IsStore -and $instruction.IsTargetOverlap) {
            if ($instruction.IsTargetExact) {
                $clean = $true
                $reason = "target_store"
                $source = "target_store"
            } else {
                $clean = $false
                $reason = "alias_store"
                $source = "none"
            }
        } elseif ($instruction.IsLoad -and $instruction.IsTargetExact) {
            $event = [pscustomobject]@{
                BlockIndex = $Block.index
                Block = $Block.name
                Line = $instruction.Line
                InstructionIndex = $instruction.Index
                FirstTargetLoadInBlock = (!$firstTargetLoadSeen)
                Replaceable = $clean
                Reason = if ($clean) { $reason } else { $reason }
                Source = $source
                Text = $instruction.Text
            }
            if ($CollectEvents) {
                $events.Add($event) | Out-Null
            }
            $firstTargetLoadSeen = $true
            $clean = $true
            $reason = "target_load"
            $source = "target_load"
        }

        if ($instruction.IsBranch -and ![string]::IsNullOrWhiteSpace($instruction.BranchTarget)) {
            $edgeStates[$instruction.BranchTarget] = [pscustomobject]@{
                Clean = $clean
                Reason = $reason
                Source = $source
            }
        }
    }

    foreach ($successor in $Block.outgoing) {
        if (!$edgeStates.ContainsKey($successor)) {
            $edgeStates[$successor] = [pscustomobject]@{
                Clean = $clean
                Reason = $reason
                Source = $source
            }
        }
    }

    return [pscustomobject]@{
        ExitClean = $clean
        ExitReason = $reason
        ExitSource = $source
        EdgeStates = $edgeStates
        Events = $events
    }
}

function Invoke-AvailabilityMode {
    param(
        [string]$ModeName,
        [bool]$KillOnBarrier,
        [System.Collections.Generic.List[object]]$Blocks,
        [hashtable]$NameToBlock,
        [hashtable]$Edges,
        [int]$Top
    )

    $entryClean = @{}
    $entryReason = @{}
    $edgeClean = @{}
    $edgeReason = @{}
    foreach ($block in $Blocks) {
        $entryClean[$block.name] = ($block.incoming.Count -gt 0)
        $entryReason[$block.name] = if ($block.incoming.Count -gt 0) { "optimistic_init" } else { "function_entry" }
    }
    foreach ($edge in $Edges.Values) {
        $key = Get-EdgeKey $edge.from $edge.to
        $edgeClean[$key] = $true
        $edgeReason[$key] = "optimistic_init"
    }

    $iterations = 0
    $changed = $true
    while ($changed -and $iterations -lt 100) {
        ++$iterations
        $changed = $false
        foreach ($block in $Blocks) {
            $preds = @($Edges.Values | Where-Object { $_.to -eq $block.name })
            $newEntryClean = $false
            $newEntryReason = "function_entry"
            if ($preds.Count -gt 0) {
                $cleanPreds = 0
                $reasons = @{}
                foreach ($pred in $preds) {
                    $key = Get-EdgeKey $pred.from $pred.to
                    if ($edgeClean.ContainsKey($key) -and $edgeClean[$key]) {
                        ++$cleanPreds
                    } else {
                        $r = if ($edgeReason.ContainsKey($key)) { $edgeReason[$key] } else { "missing_edge" }
                        Add-Count $reasons $r
                    }
                }
                $newEntryClean = ($cleanPreds -eq $preds.Count)
                $newEntryReason = if ($newEntryClean) { "all_predecessors_clean" } else {
                    (($reasons.GetEnumerator() | Sort-Object -Property @{ Expression = "Value"; Descending = $true } | Select-Object -First 1).Name)
                }
            }
            if ($entryClean[$block.name] -ne $newEntryClean -or $entryReason[$block.name] -ne $newEntryReason) {
                $entryClean[$block.name] = $newEntryClean
                $entryReason[$block.name] = $newEntryReason
                $changed = $true
            }

            $transfer = Invoke-Transfer $block $newEntryClean $newEntryReason $KillOnBarrier $false
            foreach ($successor in $transfer.EdgeStates.Keys) {
                if (!$NameToBlock.ContainsKey($successor)) {
                    continue
                }
                $key = Get-EdgeKey $block.name $successor
                if (!$Edges.ContainsKey($key)) {
                    Add-Edge $Edges $block.name $successor
                    $edgeClean[$key] = $true
                    $edgeReason[$key] = "late_edge_init"
                }
                $newEdgeClean = [bool]$transfer.EdgeStates[$successor].Clean
                $newEdgeReason = [string]$transfer.EdgeStates[$successor].Reason
                if (!$edgeClean.ContainsKey($key) -or $edgeClean[$key] -ne $newEdgeClean -or $edgeReason[$key] -ne $newEdgeReason) {
                    $edgeClean[$key] = $newEdgeClean
                    $edgeReason[$key] = $newEdgeReason
                    $changed = $true
                }
            }
        }
    }

    $events = New-Object System.Collections.Generic.List[object]
    foreach ($block in $Blocks) {
        $transfer = Invoke-Transfer $block ([bool]$entryClean[$block.name]) ([string]$entryReason[$block.name]) $KillOnBarrier $true
        foreach ($event in $transfer.Events) {
            $events.Add($event) | Out-Null
        }
    }

    $blockRows = New-Object System.Collections.Generic.List[object]
    foreach ($block in $Blocks) {
        $blockEvents = @($events | Where-Object { $_.Block -eq $block.name })
        if ($blockEvents.Count -eq 0) {
            continue
        }
        $preds = @($Edges.Values | Where-Object { $_.to -eq $block.name })
        $cleanPreds = 0
        $dirtyReasons = @{}
        foreach ($pred in $preds) {
            $key = Get-EdgeKey $pred.from $pred.to
            if ($edgeClean.ContainsKey($key) -and $edgeClean[$key]) {
                ++$cleanPreds
            } else {
                $r = if ($edgeReason.ContainsKey($key)) { $edgeReason[$key] } else { "missing_edge" }
                Add-Count $dirtyReasons $r
            }
        }
        $status = if ($preds.Count -eq 0) {
            "entry"
        } elseif ($cleanPreds -eq $preds.Count) {
            "all_preds_clean"
        } elseif ($cleanPreds -gt 0) {
            "partial_preds_clean"
        } else {
            "no_preds_clean"
        }
        $topDirtyReason = if ($dirtyReasons.Count -gt 0) {
            ($dirtyReasons.GetEnumerator() | Sort-Object -Property @{ Expression = "Value"; Descending = $true } | Select-Object -First 1).Name
        } else {
            "none"
        }
        $firstEvent = @($blockEvents | Where-Object { $_.FirstTargetLoadInBlock } | Select-Object -First 1)
        $firstReplaceable = if ($firstEvent.Count -gt 0) { [bool]$firstEvent[0].Replaceable } else { $false }
        $firstReason = if ($firstEvent.Count -gt 0) { [string]$firstEvent[0].Reason } else { "none" }
        $blockRows.Add([pscustomobject]@{
            BlockIndex = $block.index
            Block = $block.name
            Loads = $blockEvents.Count
            Preds = $preds.Count
            CleanPreds = $cleanPreds
            Status = $status
            FirstReplaceable = $firstReplaceable
            FirstReason = $firstReason
            DirtyReason = $topDirtyReason
            Incoming = ($block.incoming -join ";")
        }) | Out-Null
    }

    $replaceableLoads = @($events | Where-Object { $_.Replaceable }).Count
    $firstLoads = @($events | Where-Object { $_.FirstTargetLoadInBlock }).Count
    $replaceableFirstLoads = @($events | Where-Object { $_.FirstTargetLoadInBlock -and $_.Replaceable }).Count
    $reasonTable = @{}
    $sourceTable = @{}
    foreach ($event in $events) {
        if ($event.Replaceable) {
            Add-Count $sourceTable $event.Source
        } else {
            Add-Count $reasonTable $event.Reason
        }
    }
    $statusTable = @{}
    foreach ($row in $blockRows) {
        Add-Count $statusTable $row.Status
    }

    return [pscustomobject]@{
        ModeName = $ModeName
        KillOnBarrier = $KillOnBarrier
        Iterations = $iterations
        Converged = (!$changed)
        EntryClean = $entryClean
        EdgeClean = $edgeClean
        EdgeReason = $edgeReason
        Events = $events
        BlockRows = $blockRows
        ReplaceableLoads = $replaceableLoads
        FirstLoads = $firstLoads
        ReplaceableFirstLoads = $replaceableFirstLoads
        ReasonTable = $reasonTable
        SourceTable = $sourceTable
        StatusTable = $statusTable
    }
}

function Write-TopTable {
    param(
        [string]$Title,
        [hashtable]$Table,
        [int]$Limit
    )
    Write-Output ""
    Write-Output "### $Title"
    if ($Table.Count -eq 0) {
        Write-Output "(none)"
        return
    }
    $Table.GetEnumerator() |
        Sort-Object -Property @{ Expression = "Value"; Descending = $true },
                              @{ Expression = "Name"; Ascending = $true } |
        Select-Object -First $Limit |
        ForEach-Object {
            Write-Output ("{0} {1}" -f $_.Name, $_.Value)
        }
}

function Write-ModeReport {
    param(
        [object]$Result,
        [int]$Top
    )

    Write-Output ""
    Write-Output ("## Mode: {0}" -f $Result.ModeName)
    Write-Output ("kill_context_barrier={0}" -f $Result.KillOnBarrier)
    Write-Output ("iterations={0}" -f $Result.Iterations)
    Write-Output ("converged={0}" -f $Result.Converged)
    Write-Output ("target_loads={0}" -f $Result.Events.Count)
    Write-Output ("replaceable_loads={0}" -f $Result.ReplaceableLoads)
    Write-Output ("first_target_loads={0}" -f $Result.FirstLoads)
    Write-Output ("replaceable_first_target_loads={0}" -f $Result.ReplaceableFirstLoads)

    Write-TopTable "Target-Load Block Entry Status" $Result.StatusTable $Top
    Write-TopTable "Not-Replaceable Load Reasons" $Result.ReasonTable $Top
    Write-TopTable "Replaceable Load Sources" $Result.SourceTable $Top

    Write-Output ""
    Write-Output "### Target-Load Blocks"
    if ($Result.BlockRows.Count -eq 0) {
        Write-Output "(none)"
    } else {
        $Result.BlockRows |
            Sort-Object -Property @{ Expression = "Status"; Ascending = $true },
                                  @{ Expression = "Loads"; Descending = $true },
                                  @{ Expression = "BlockIndex"; Ascending = $true } |
            Select-Object -First $Top |
            ForEach-Object {
                Write-Output ("{0:D4} {1} loads={2} preds={3} clean_preds={4} status={5} first_replaceable={6} first_reason={7} dirty_reason={8}" -f
                    $_.BlockIndex, $_.Block, $_.Loads, $_.Preds, $_.CleanPreds,
                    $_.Status, $_.FirstReplaceable, $_.FirstReason, $_.DirtyReason)
            }
    }

    Write-Output ""
    Write-Output "### Sample Target Loads"
    if ($Result.Events.Count -eq 0) {
        Write-Output "(none)"
    } else {
        $Result.Events |
            Select-Object -First $Top |
            ForEach-Object {
                Write-Output ("line={0} instr={1} block={2:D4}:{3} first={4} replaceable={5} reason={6} source={7} text={8}" -f
                    $_.Line, $_.InstructionIndex, $_.BlockIndex, $_.Block,
                    $_.FirstTargetLoadInBlock, $_.Replaceable, $_.Reason,
                    $_.Source, $_.Text)
            }
    }
}

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}

$resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
$functionPattern = [Regex]::Escape($Function.ToUpperInvariant())
$phasePattern = [Regex]::Escape($Phase)
$linePattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"
$targetOffset = 32 + (8 * $Gpr)
$targetSize = 8
$targetName = Get-ContextOffsetName $targetOffset

$blocks = New-Object System.Collections.Generic.List[object]
$nameToBlock = @{}
$edges = @{}
$currentBlock = $null
$lineNumber = 0
$hirLines = 0
$targetLoads = 0
$targetStores = 0
$targetAliasStores = 0

function Start-Block {
    param([string]$Name)
    $script:currentBlock = New-HirBlock $script:blocks.Count $Name
    $script:blocks.Add($script:currentBlock) | Out-Null
    if (!$script:nameToBlock.ContainsKey($Name)) {
        $script:nameToBlock[$Name] = $script:currentBlock
    }
}

Get-Content -LiteralPath $resolvedLog | ForEach-Object {
    ++$lineNumber
    if ($_ -notmatch $linePattern) {
        return
    }

    $text = $Matches.text
    if ($text -match "^\s*(<entry>|<block[0-9]+>|loc_[0-9A-Fa-f]+|_label[0-9A-Fa-f]+|label[0-9]+):") {
        Start-Block $Matches[1]
        return
    }
    if ($null -eq $currentBlock) {
        Start-Block "<unknown-entry>"
    }

    if ($text -match "^\s*;\s+in:\s+(?<source>[^,\s]+)") {
        Add-UniqueString $currentBlock.incoming $Matches.source
        Add-Edge $edges $Matches.source $currentBlock.name
        return
    }
    if ($text -match "^\s*;\s+out:\s+(?<dest>[^,\s]+)") {
        Add-UniqueString $currentBlock.outgoing $Matches.dest
        Add-Edge $edges $currentBlock.name $Matches.dest
        return
    }
    if ($text -match "^\s*(;|$)") {
        return
    }

    ++$hirLines
    $instruction = New-HirInstruction $lineNumber $hirLines $text $targetOffset $targetSize
    $currentBlock.instructions.Add($instruction) | Out-Null
    if ($instruction.IsCall) { $currentBlock.calls += 1 }
    if ($instruction.IsBarrier) { $currentBlock.barriers += 1 }
    if ($instruction.IsBranch) { $currentBlock.branches += 1 }
    if ($instruction.IsExit) { $currentBlock.exits += 1 }
    if ($instruction.IsBranch -and ![string]::IsNullOrWhiteSpace($instruction.BranchTarget)) {
        Add-UniqueString $currentBlock.outgoing $instruction.BranchTarget
        Add-Edge $edges $currentBlock.name $instruction.BranchTarget
    }
    if ($instruction.IsTargetExact -and $instruction.IsLoad) {
        ++$targetLoads
        $currentBlock.target_loads += 1
    } elseif ($instruction.IsTargetOverlap -and $instruction.IsStore) {
        if ($instruction.IsTargetExact) {
            ++$targetStores
            $currentBlock.target_stores += 1
        } else {
            ++$targetAliasStores
            $currentBlock.target_alias_stores += 1
        }
    }
}

$strictEdges = @{}
foreach ($edge in $edges.GetEnumerator()) {
    $strictEdges[$edge.Name] = $edge.Value
}
$barrierEdges = @{}
foreach ($edge in $edges.GetEnumerator()) {
    $barrierEdges[$edge.Name] = $edge.Value
}

$strict = Invoke-AvailabilityMode "strict-helper-and-barrier-flush" $true $blocks $nameToBlock $strictEdges $Top
$barrierPreserve = Invoke-AvailabilityMode "helper-flush-barrier-preserve" $false $blocks $nameToBlock $barrierEdges $Top

Write-Output "# HIR GPR Live-In Availability Report"
Write-Output ""
Write-Output ("log={0}" -f $resolvedLog)
Write-Output ("function={0}" -f $Function.ToUpperInvariant())
Write-Output ("phase={0}" -f $Phase)
Write-Output ("target={0} offset=+{1}" -f $targetName, $targetOffset)
Write-Output ("hir_lines={0}" -f $hirLines)
Write-Output ("blocks={0}" -f $blocks.Count)
Write-Output ("edges={0}" -f $edges.Count)
Write-Output ("target_loads={0}" -f $targetLoads)
Write-Output ("target_stores={0}" -f $targetStores)
Write-Output ("target_alias_stores={0}" -f $targetAliasStores)
Write-Output ""
Write-Output "report_scope=Conservative block/edge availability for replacing exact target GPR context loads before A64 register allocation. Branch edges use the clean/dirty state at the branch instruction when the branch target is visible; unmatched outgoing edges use block-final state."
Write-Output "implementation_gate=If replaceable_first_target_loads is small, do not build a new GPR cache yet. If it is large, the patch still needs a real edge carrier, phi/local merge, or pinned state strategy because predecessor values can differ."

Write-ModeReport $strict $Top
Write-ModeReport $barrierPreserve $Top

Write-Output ""
Write-Output "## Decision Aid"
if ($barrierPreserve.ReplaceableFirstLoads -lt 8) {
    Write-Output "Even preserving context_barrier does not expose many first target loads. Prefer another target slot or a broader data-flow design before coding a runtime patch."
} elseif ($strict.ReplaceableFirstLoads -eq 0 -and $barrierPreserve.ReplaceableFirstLoads -gt 0) {
    Write-Output "Replacement depends on preserving state across context_barrier. The runtime patch must prove those barriers are no-op for this GPR before enabling load replacement."
} else {
    Write-Output "There is a bounded first-load replacement set. Inspect the all_preds_clean block rows before implementing a guarded pre-RA patch."
}
