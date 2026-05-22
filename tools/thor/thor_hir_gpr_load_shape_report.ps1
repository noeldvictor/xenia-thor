param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [ValidateRange(0, 31)]
    [int]$Gpr = 1,
    [int]$Top = 20
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
        instructions = 0
        loads = 0
        stores = 0
        target_loads = 0
        target_stores = 0
        target_alias_stores = 0
        barriers = 0
        calls = 0
        branches = 0
        target_first_loads = 0
        saw_target = $false
        last_target_access = ""
    }
}

function New-TargetEvent {
    param(
        [object]$Block,
        [int]$Line,
        [int]$InstructionIndex,
        [string]$Text,
        [string]$Kind,
        [string]$PrevInBlock,
        [string]$PrevGlobal,
        [int]$SinceBarrier,
        [int]$SinceBranch,
        [int]$SinceCall,
        [int]$AliasStoresSinceLastLoad
    )
    return [pscustomobject]@{
        BlockIndex = $Block.index
        Block = $Block.name
        Line = $Line
        InstructionIndex = $InstructionIndex
        Kind = $Kind
        PrevInBlock = $PrevInBlock
        PrevGlobal = $PrevGlobal
        Incoming = $Block.incoming.Count
        Outgoing = $Block.outgoing.Count
        SingleDominatingPred = ($Block.index -gt 0 -and $Block.incoming.Count -eq 1 -and $Block.incoming[0] -match "dom:1")
        MultiPred = ($Block.incoming.Count -gt 1)
        SelfPred = (($Block.incoming -join ";") -match [Regex]::Escape($Block.name))
        FirstTargetLoadInBlock = (!$Block.saw_target)
        BarriersInBlock = $Block.barriers
        BranchesInBlock = $Block.branches
        CallsInBlock = $Block.calls
        SinceBarrier = $SinceBarrier
        SinceBranch = $SinceBranch
        SinceCall = $SinceCall
        AliasStoresSinceLastLoad = $AliasStoresSinceLastLoad
        Text = $Text.Trim()
    }
}

function Write-TopTable {
    param(
        [string]$Title,
        [hashtable]$Table,
        [int]$Limit
    )
    Write-Output ""
    Write-Output "## $Title"
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
$events = New-Object System.Collections.Generic.List[object]
$currentBlock = $null
$lineNumber = 0
$hirLines = 0
$targetLoads = 0
$targetStores = 0
$targetAliasStores = 0
$loadsAfterGlobalLoad = 0
$loadsAfterGlobalStore = 0
$firstLoadsInBlocks = 0
$loadsInSingleDominatingPred = 0
$loadsInMultiPred = 0
$loadsInSelfPred = 0
$loadsAfterBarrierBeforeBranch = 0
$loadsAfterBranch = 0
$loadsAfterCall = 0
$loadsWithAliasSinceLastLoad = 0
$lastGlobalTargetAccess = ""
$lastTargetLoadEvent = $null
$aliasStoresSinceLastLoad = 0
$lastBarrierInstruction = -1
$lastBranchInstruction = -1
$lastCallInstruction = -1
$prevGlobalTable = @{}
$blockShapeTable = @{}
$blockLoadTable = @{}
$incomingShapeTable = @{}
$contextLineCount = 0

function Start-Block {
    param([string]$Name)
    $script:currentBlock = New-HirBlock $script:blocks.Count $Name
    $script:blocks.Add($script:currentBlock) | Out-Null
    $script:lastBarrierInstruction = -1
    $script:lastBranchInstruction = -1
    $script:lastCallInstruction = -1
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

    if ($text -match "^\s*;\s+in:\s+(?<source>.*)$") {
        $currentBlock.incoming.Add($Matches.source.Trim()) | Out-Null
        return
    }
    if ($text -match "^\s*;\s+out:\s+(?<dest>.*)$") {
        $currentBlock.outgoing.Add($Matches.dest.Trim()) | Out-Null
        return
    }
    if ($text -match "^\s*(;|$)") {
        return
    }

    ++$hirLines
    $currentBlock.instructions += 1
    $instructionIndex = $hirLines

    if ($text -match "\bcontext_barrier\b") {
        $currentBlock.barriers += 1
        $lastBarrierInstruction = $instructionIndex
    }
    if ($text -match "\bcall(_true|_indirect|_indirect_true|_extern)?\b") {
        $currentBlock.calls += 1
        $lastCallInstruction = $instructionIndex
    }
    if ($text -match "\bbranch(_true|_false)?\b") {
        $currentBlock.branches += 1
        $lastBranchInstruction = $instructionIndex
    }

    $isLoad = $false
    $isStore = $false
    $offset = $null
    if ($text -match "load_context \+([0-9]+)") {
        $isLoad = $true
        $offset = [int]$Matches[1]
    } elseif ($text -match "store_context \+([0-9]+)") {
        $isStore = $true
        $offset = [int]$Matches[1]
    }

    if ($null -eq $offset) {
        return
    }

    ++$contextLineCount
    $size = Get-HirValueSize $text
    $isTargetExact = ($offset -eq $targetOffset -and $size -eq $targetSize)
    $isTargetOverlap = Test-Overlap $offset $size $targetOffset $targetSize

    if ($isLoad) {
        $currentBlock.loads += 1
    } elseif ($isStore) {
        $currentBlock.stores += 1
    }

    if ($isStore -and $isTargetOverlap) {
        if ($isTargetExact) {
            ++$targetStores
            $currentBlock.target_stores += 1
        } else {
            ++$targetAliasStores
            $currentBlock.target_alias_stores += 1
        }
        ++$aliasStoresSinceLastLoad
        $currentBlock.last_target_access = "store"
        $lastGlobalTargetAccess = "store"
        return
    }

    if (!$isLoad -or !$isTargetExact) {
        return
    }

    ++$targetLoads
    $currentBlock.target_loads += 1
    if (!$currentBlock.saw_target) {
        ++$firstLoadsInBlocks
        $currentBlock.target_first_loads += 1
    }

    if ($lastGlobalTargetAccess -eq "load") {
        ++$loadsAfterGlobalLoad
    } elseif ($lastGlobalTargetAccess -eq "store") {
        ++$loadsAfterGlobalStore
    }

    $incomingShape = "entry"
    if ($currentBlock.incoming.Count -eq 0) {
        $incomingShape = "no_incoming"
    } elseif ($currentBlock.incoming.Count -eq 1 -and $currentBlock.incoming[0] -match "dom:1") {
        $incomingShape = "single_dom"
        ++$loadsInSingleDominatingPred
    } elseif ($currentBlock.incoming.Count -eq 1) {
        $incomingShape = "single_not_dom"
    } else {
        $incomingShape = "multi_pred"
        ++$loadsInMultiPred
    }
    if (($currentBlock.incoming -join ";") -match [Regex]::Escape($currentBlock.name)) {
        ++$loadsInSelfPred
    }

    $sinceBarrier = if ($lastBarrierInstruction -ge 0) { $instructionIndex - $lastBarrierInstruction } else { -1 }
    $sinceBranch = if ($lastBranchInstruction -ge 0) { $instructionIndex - $lastBranchInstruction } else { -1 }
    $sinceCall = if ($lastCallInstruction -ge 0) { $instructionIndex - $lastCallInstruction } else { -1 }

    if ($sinceBarrier -ge 0 -and ($sinceBranch -lt 0 -or $lastBarrierInstruction -gt $lastBranchInstruction)) {
        ++$loadsAfterBarrierBeforeBranch
    }
    if ($sinceBranch -ge 0) {
        ++$loadsAfterBranch
    }
    if ($sinceCall -ge 0) {
        ++$loadsAfterCall
    }
    if ($aliasStoresSinceLastLoad -gt 0) {
        ++$loadsWithAliasSinceLastLoad
    }

    $prevGlobalKey = if (![string]::IsNullOrWhiteSpace($lastGlobalTargetAccess)) { $lastGlobalTargetAccess } else { "none" }
    Add-Count $prevGlobalTable $prevGlobalKey
    Add-Count $incomingShapeTable $incomingShape
    Add-Count $blockLoadTable ("{0:D4} {1} in={2} out={3}" -f
        $currentBlock.index, $currentBlock.name, $currentBlock.incoming.Count,
        $currentBlock.outgoing.Count)
    Add-Count $blockShapeTable ("in={0} out={1} barriers={2} branches={3} calls={4}" -f
        $currentBlock.incoming.Count, $currentBlock.outgoing.Count,
        $currentBlock.barriers, $currentBlock.branches, $currentBlock.calls)

    $event = New-TargetEvent `
        -Block $currentBlock `
        -Line $lineNumber `
        -InstructionIndex $instructionIndex `
        -Text $text `
        -Kind "load" `
        -PrevInBlock $currentBlock.last_target_access `
        -PrevGlobal $lastGlobalTargetAccess `
        -SinceBarrier $sinceBarrier `
        -SinceBranch $sinceBranch `
        -SinceCall $sinceCall `
        -AliasStoresSinceLastLoad $aliasStoresSinceLastLoad
    $events.Add($event) | Out-Null

    $currentBlock.saw_target = $true
    $currentBlock.last_target_access = "load"
    $lastGlobalTargetAccess = "load"
    $lastTargetLoadEvent = $event
    $aliasStoresSinceLastLoad = 0
}

$blocksWithTargetLoads = ($blocks | Where-Object { $_.target_loads -gt 0 }).Count
$blocksWithTargetStores = ($blocks | Where-Object { $_.target_stores -gt 0 }).Count

Write-Output "# HIR GPR Load Shape Report"
Write-Output ""
Write-Output ("log={0}" -f $resolvedLog)
Write-Output ("function={0}" -f $Function.ToUpperInvariant())
Write-Output ("phase={0}" -f $Phase)
Write-Output ("target={0} offset=+{1}" -f $targetName, $targetOffset)
Write-Output ("hir_lines={0}" -f $hirLines)
Write-Output ("blocks={0}" -f $blocks.Count)
Write-Output ("context_accesses={0}" -f $contextLineCount)
Write-Output ("target_loads={0}" -f $targetLoads)
Write-Output ("target_stores={0}" -f $targetStores)
Write-Output ("target_alias_stores={0}" -f $targetAliasStores)
Write-Output ("blocks_with_target_loads={0}" -f $blocksWithTargetLoads)
Write-Output ("blocks_with_target_stores={0}" -f $blocksWithTargetStores)
Write-Output ("first_target_loads_in_blocks={0}" -f $firstLoadsInBlocks)
Write-Output ("loads_after_global_load={0}" -f $loadsAfterGlobalLoad)
Write-Output ("loads_after_global_store={0}" -f $loadsAfterGlobalStore)
Write-Output ("loads_in_single_dominating_pred_blocks={0}" -f $loadsInSingleDominatingPred)
Write-Output ("loads_in_multi_pred_blocks={0}" -f $loadsInMultiPred)
Write-Output ("loads_in_self_pred_blocks={0}" -f $loadsInSelfPred)
Write-Output ("loads_after_barrier_before_branch={0}" -f $loadsAfterBarrierBeforeBranch)
Write-Output ("loads_after_any_branch_in_block={0}" -f $loadsAfterBranch)
Write-Output ("loads_after_any_call_in_block={0}" -f $loadsAfterCall)
Write-Output ("loads_with_alias_store_since_previous_target_load={0}" -f $loadsWithAliasSinceLastLoad)

Write-TopTable "Previous Global Target Access Before Load" $prevGlobalTable $Top
Write-TopTable "Incoming Shape For Target Loads" $incomingShapeTable $Top
Write-TopTable "Target Loads By Block" $blockLoadTable $Top
Write-TopTable "Target Load Site Shape At Load" $blockShapeTable $Top

Write-Output ""
Write-Output "## Candidate Interpretation"
if ($targetLoads -eq 0) {
    Write-Output "No target loads found."
} else {
    if ($loadsAfterGlobalLoad -eq 0) {
        Write-Output "No target load follows another visible target load without an intervening target store or aliasing write in the linear dump order."
    } else {
        Write-Output ("{0} target loads follow a previous target load in linear dump order; inspect block safety before caching." -f $loadsAfterGlobalLoad)
    }
    if ($loadsInMultiPred -gt ($targetLoads / 2)) {
        Write-Output "Most target loads are in multi-predecessor blocks, so a simple fallthrough/pinned emit cache is expected to miss."
    }
    if ($firstLoadsInBlocks -gt ($targetLoads / 2)) {
        Write-Output "Most target loads are the first target access in their block; a pre-RA live-in/value-availability pass is more appropriate than post-RA emit caching."
    }
    if ($loadsAfterBarrierBeforeBranch -gt 0) {
        Write-Output "Some target loads occur after a context_barrier inside the same block; barrier semantics must be explicit in any state-cache design."
    }
}

Write-Output ""
Write-Output "## Sample Target Load Events"
if ($events.Count -eq 0) {
    Write-Output "(none)"
} else {
    $events |
        Select-Object -First $Top |
        ForEach-Object {
            $prevInBlock = if (![string]::IsNullOrWhiteSpace($_.PrevInBlock)) { $_.PrevInBlock } else { "none" }
            $prevGlobal = if (![string]::IsNullOrWhiteSpace($_.PrevGlobal)) { $_.PrevGlobal } else { "none" }
            Write-Output ("line={0} instr={1} block={2:D4}:{3} incoming={4} outgoing={5} prev_block={6} prev_global={7} single_dom={8} multi={9} self={10} first_in_block={11} since_barrier={12} since_branch={13} since_call={14} alias_since_last={15} text={16}" -f
                $_.Line, $_.InstructionIndex, $_.BlockIndex, $_.Block,
                $_.Incoming, $_.Outgoing,
                $prevInBlock,
                $prevGlobal,
                $_.SingleDominatingPred, $_.MultiPred, $_.SelfPred,
                $_.FirstTargetLoadInBlock, $_.SinceBarrier, $_.SinceBranch,
                $_.SinceCall, $_.AliasStoresSinceLastLoad, $_.Text)
        }
}
