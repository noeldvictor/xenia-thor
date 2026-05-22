param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
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

function Get-ContextOffsetClass {
    param([int]$Offset)

    $name = Get-ContextOffsetName $Offset
    if ($name -match "^r\[") { return "GPR" }
    if ($name -match "^f\[") { return "FPR" }
    if ($name -match "^v\[") { return "VMX" }
    if ($name -match "^cr") { return "CR" }
    if ($name -match "^xer") { return "XER" }
    if ($name -in @("lr", "ctr")) { return "LR/CTR" }
    if ($name -in @("fpscr", "vscr_sat")) { return "FP/VMX_STATUS" }
    if ($name -in @("thread_state", "virtual_membase", "thread_id", "runtime_or_reservation")) {
        return "RUNTIME"
    }
    return "OTHER"
}

function Test-WholeGprOffset {
    param([int]$Offset)
    return $Offset -ge 32 -and $Offset -lt 288 -and (($Offset - 32) % 8) -eq 0
}

function Format-OffsetKey {
    param([int]$Offset)
    return ("+{0} {1}" -f $Offset, (Get-ContextOffsetName $Offset))
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
        gpr_loads = 0
        gpr_stores = 0
        calls = 0
        branches = 0
        volatile_branches = 0
        barriers = 0
        first_gpr_loads = @{}
        last_access = @{}
        saw_offset = @{}
    }
}

function Add-Candidate {
    param(
        [hashtable]$Table,
        [int]$Offset,
        [string]$PreviousKind,
        [object]$Block
    )

    if (!$Table.ContainsKey($Offset)) {
        $Table[$Offset] = [ordered]@{
            offset = $Offset
            name = Get-ContextOffsetName $Offset
            score = 0
            first_loads = 0
            after_store = 0
            after_load = 0
            dominated_blocks = New-Object System.Collections.Generic.List[string]
        }
    }

    $row = $Table[$Offset]
    $row.first_loads += 1
    if ($PreviousKind -eq "store") {
        $row.after_store += 1
        $row.score += 3
    } elseif ($PreviousKind -eq "load") {
        $row.after_load += 1
        $row.score += 2
    } else {
        $row.score += 1
    }
    if ($row.dominated_blocks.Count -lt 8) {
        $row.dominated_blocks.Add(("{0}:{1}" -f $Block.index, $Block.name)) | Out-Null
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

$blocks = New-Object System.Collections.Generic.List[object]
$currentBlock = $null
$hirLines = 0
$contextLines = 0
$wholeGprLoads = 0
$wholeGprStores = 0
$singlePredBlocks = 0
$multiPredBlocks = 0
$firstLoadsInSinglePred = 0
$contextAccessClasses = @{}
$candidateRows = @{}
$globalLastAccess = @{}

function Start-Block {
    param([string]$Name)
    $script:currentBlock = New-HirBlock $script:blocks.Count $Name
    $script:blocks.Add($script:currentBlock) | Out-Null
}

Get-Content -LiteralPath $resolvedLog | ForEach-Object {
    if ($_ -notmatch $linePattern) {
        return
    }

    $text = $Matches.text
    if ($text -match "^\s*(<entry>|<block[0-9]+>|loc_[0-9A-Fa-f]+|label[0-9]+):") {
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

    if ($text -match "\bcontext_barrier\b") {
        $currentBlock.barriers += 1
    }
    if ($text -match "\bcall(_true|_indirect|_indirect_true|_extern)?\b") {
        $currentBlock.calls += 1
    }
    if ($text -match "\bbranch(_true|_false)?\b") {
        $currentBlock.branches += 1
        if ($text -match "\bbranch_(true|false)\b") {
            $currentBlock.volatile_branches += 1
        }
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

    ++$contextLines
    Add-Count $contextAccessClasses (Get-ContextOffsetClass $offset)
    $isWholeGpr = Test-WholeGprOffset $offset

    if ($isLoad) {
        $currentBlock.loads += 1
        if ($isWholeGpr) {
            ++$wholeGprLoads
            $currentBlock.gpr_loads += 1
            if (!$currentBlock.saw_offset.ContainsKey($offset)) {
                $currentBlock.first_gpr_loads[$offset] = $true
            }
        }
        $currentBlock.saw_offset[$offset] = $true
        $currentBlock.last_access[$offset] = "load"
    } elseif ($isStore) {
        $currentBlock.stores += 1
        if ($isWholeGpr) {
            ++$wholeGprStores
            $currentBlock.gpr_stores += 1
        }
        $currentBlock.saw_offset[$offset] = $true
        $currentBlock.last_access[$offset] = "store"
    }
}

foreach ($block in $blocks) {
    $incomingCount = $block.incoming.Count
    $hasSingleDominatingPred =
        $block.index -gt 0 -and
        $incomingCount -eq 1 -and
        $block.incoming[0] -match "dom:1"

    if ($hasSingleDominatingPred) {
        ++$singlePredBlocks
        foreach ($offsetObject in $block.first_gpr_loads.Keys) {
            $offset = [int]$offsetObject
            ++$firstLoadsInSinglePred
            $previousKind = $null
            if ($globalLastAccess.ContainsKey($offset)) {
                $previousKind = $globalLastAccess[$offset]
            }
            Add-Candidate $candidateRows $offset $previousKind $block
        }
    } elseif ($block.index -gt 0 -and $incomingCount -gt 1) {
        ++$multiPredBlocks
    }

    foreach ($offsetObject in $block.last_access.Keys) {
        $offset = [int]$offsetObject
        $globalLastAccess[$offset] = $block.last_access[$offset]
    }
}

$hotBlocks = @{}
foreach ($block in $blocks) {
    $contextAccesses = $block.loads + $block.stores
    if ($contextAccesses -gt 0) {
        $label = ("{0:D4} {1} in={2} out={3} gpr_loads={4} gpr_stores={5} calls={6} branches={7} barriers={8}" -f
            $block.index, $block.name, $block.incoming.Count,
            $block.outgoing.Count, $block.gpr_loads, $block.gpr_stores,
            $block.calls, $block.branches, $block.barriers)
        $hotBlocks[$label] = $contextAccesses
    }
}

Write-Output "# HIR GPR Promotion Audit"
Write-Output ""
Write-Output ("log={0}" -f $resolvedLog)
Write-Output ("function={0}" -f $Function.ToUpperInvariant())
Write-Output ("phase={0}" -f $Phase)
Write-Output ("hir_lines={0}" -f $hirLines)
Write-Output ("blocks={0}" -f $blocks.Count)
Write-Output ("context_accesses={0}" -f $contextLines)
Write-Output ("whole_gpr_loads={0}" -f $wholeGprLoads)
Write-Output ("whole_gpr_stores={0}" -f $wholeGprStores)
Write-Output ("dominated_single_pred_blocks={0}" -f $singlePredBlocks)
Write-Output ("multi_pred_blocks={0}" -f $multiPredBlocks)
Write-Output ("first_gpr_loads_in_dominated_single_pred_blocks={0}" -f $firstLoadsInSinglePred)
Write-Output "promotion_gate=Do not create cross-block SSA values directly in ContextPromotionPass unless DataFlowAnalysis/local-slot lowering is added before RegisterAllocationPass. The current PPC translator does not run DataFlowAnalysisPass."
Write-Output "audit_scope=Counts first whole-GPR loads in dominated single-predecessor HIR blocks and ranks offsets whose last earlier visible context access was a load or store. Treat this as an implementation ranking, not a correctness proof."

Write-TopTable "Context Access Classes" $contextAccessClasses $Top
Write-TopTable "Hot Blocks By Context Accesses" $hotBlocks $Top

Write-Output ""
Write-Output "## Top Dominated First-GPR-Load Candidates"
if ($candidateRows.Count -eq 0) {
    Write-Output "(none)"
} else {
    $candidateRows.GetEnumerator() |
        ForEach-Object { [pscustomobject]$_.Value } |
        Sort-Object -Property @{ Expression = "score"; Descending = $true },
                              @{ Expression = "offset"; Ascending = $true } |
        Select-Object -First $Top |
        ForEach-Object {
            Write-Output ("+{0} {1} score={2} first_loads={3} after_store={4} after_load={5} sample_blocks={6}" -f
                $_.offset, $_.name, $_.score, $_.first_loads, $_.after_store,
                $_.after_load, ($_.dominated_blocks -join ","))
        }
}

Write-Output ""
Write-Output "## Next Patch Shape"
Write-Output "1. Add a guarded pre-RA context promotion experiment, or add DataFlowAnalysisPass/local-slot lowering before cross-block context values."
Write-Output "2. Restrict the first patch to whole INT64 GPR offsets ranked above, especially r[1] and r[11]."
Write-Output "3. Reset candidate state at volatile ops, calls, conditional branches, returns, traps, multi-predecessor joins, and overlapping context writes."
Write-Output "4. Do not elide stores in the first patch; only replace later loads with proven incoming values or locals."
