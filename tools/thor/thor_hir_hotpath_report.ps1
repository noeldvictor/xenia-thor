param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [ValidateSet("RawHIR", "OptHIR", "Both")]
    [string]$Phase = "OptHIR",
    [int]$Top = 25
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

    if ($Offset -eq 0) {
        return "thread_state"
    }
    if ($Offset -eq 8) {
        return "virtual_membase"
    }
    if ($Offset -eq 16) {
        return "lr"
    }
    if ($Offset -eq 24) {
        return "ctr"
    }
    if ($Offset -ge 32 -and $Offset -lt 288) {
        $index = [Math]::Floor(($Offset - 32) / 8)
        $lane = ($Offset - 32) % 8
        if ($lane -eq 0) {
            return ("r[{0}]" -f $index)
        }
        return ("r[{0}]+{1}" -f $index, $lane)
    }
    if ($Offset -ge 288 -and $Offset -lt 544) {
        $index = [Math]::Floor(($Offset - 288) / 8)
        $lane = ($Offset - 288) % 8
        if ($lane -eq 0) {
            return ("f[{0}]" -f $index)
        }
        return ("f[{0}]+{1}" -f $index, $lane)
    }
    if ($Offset -ge 544 -and $Offset -lt 2592) {
        $index = [Math]::Floor(($Offset - 544) / 16)
        $lane = ($Offset - 544) % 16
        if ($lane -eq 0) {
            return ("v[{0}]" -f $index)
        }
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
    if ($Offset -ge 2628 -and $Offset -lt 2632) {
        return "fpscr"
    }
    if ($Offset -eq 2632) {
        return "vscr_sat"
    }
    if ($Offset -eq 2636) {
        return "thread_id"
    }
    if ($Offset -ge 2640) {
        return "runtime_or_reservation"
    }
    return "unknown"
}

function Get-ContextOffsetClass {
    param([int]$Offset)

    $name = Get-ContextOffsetName $Offset
    if ($name -match "^r\[") {
        return "GPR"
    }
    if ($name -match "^f\[") {
        return "FPR"
    }
    if ($name -match "^v\[") {
        return "VMX"
    }
    if ($name -match "^cr") {
        return "CR"
    }
    if ($name -match "^xer") {
        return "XER"
    }
    if ($name -in @("lr", "ctr")) {
        return "LR/CTR"
    }
    if ($name -in @("fpscr", "vscr_sat")) {
        return "FP/VMX_STATUS"
    }
    if ($name -in @("thread_state", "virtual_membase", "thread_id", "runtime_or_reservation")) {
        return "RUNTIME"
    }
    return "OTHER"
}

function Write-TopTable {
    param(
        [string]$Title,
        [hashtable]$Table,
        [int]$Limit,
        [switch]$AnnotateContextOffsets
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
            if ($AnnotateContextOffsets -and $_.Name -match "^\+([0-9]+)$") {
                $offset = [int]$Matches[1]
                Write-Output ("{0} {1} {2}" -f $_.Name, (Get-ContextOffsetName $offset), $_.Value)
            } else {
                Write-Output ("{0} {1}" -f $_.Name, $_.Value)
            }
        }
}

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}

$resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
$functionPattern = [Regex]::Escape($Function.ToUpperInvariant())
$phasePattern = if ($Phase -eq "Both") { "(RawHIR|OptHIR)" } else { [Regex]::Escape($Phase) }
$linePattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"

$opCounts = @{}
$contextLoadOffsets = @{}
$contextStoreOffsets = @{}
$contextLoadClasses = @{}
$contextStoreClasses = @{}
$permuteControls = @{}
$memoryLoads = @{}
$memoryStores = @{}

$hirLines = 0
$dotProduct3 = 0
$dotProduct4 = 0
$byteSwaps = 0
$v128ByteSwaps = 0
$permutes = 0
$branches = 0
$calls = 0
$contextBarriers = 0
$storeAfterByteSwap = 0
$lastWasByteSwap = $false

Get-Content -LiteralPath $resolvedLog | ForEach-Object {
    if ($_ -notmatch $linePattern) {
        return
    }

    $text = $Matches.text
    if ($text -match "^\s*(;|loc_|$)") {
        $lastWasByteSwap = $false
        return
    }

    ++$hirLines
    $op = "other"
    if ($text -match "=\s*([a-z0-9_\.]+)") {
        $op = $Matches[1]
    } elseif ($text -match "^\s*([a-z0-9_\.]+)") {
        $op = $Matches[1]
    }
    Add-Count $opCounts $op

    if ($text -match "load_context \+([0-9]+)") {
        $offset = [int]$Matches[1]
        Add-Count $contextLoadOffsets ("+" + $offset)
        Add-Count $contextLoadClasses (Get-ContextOffsetClass $offset)
    }
    if ($text -match "store_context \+([0-9]+)") {
        $offset = [int]$Matches[1]
        Add-Count $contextStoreOffsets ("+" + $offset)
        Add-Count $contextStoreClasses (Get-ContextOffsetClass $offset)
    }
    if ($text -match "permute\.2\s+([0-9A-Fa-f]+)") {
        Add-Count $permuteControls ("0x" + $Matches[1].ToUpperInvariant())
    }
    if ($text -match "\b(load|load_offset|load_vector_sh[lr])(\.[0-9]+)?\b") {
        Add-Count $memoryLoads $Matches[1]
    }
    if ($text -match "\b(store|store_offset|store_vector_sh[lr])(\.[0-9]+)?\b") {
        Add-Count $memoryStores $Matches[1]
    }

    if ($text -match "dot_product_3") {
        ++$dotProduct3
    }
    if ($text -match "dot_product_4") {
        ++$dotProduct4
    }
    if ($text -match "byte_swap") {
        ++$byteSwaps
        if ($text -match "\.v128") {
            ++$v128ByteSwaps
        }
    }
    if ($text -match "permute") {
        ++$permutes
    }
    if ($text -match "\bbranch(_true|_false)?\b") {
        ++$branches
    }
    if ($text -match "\bcall(_true|_indirect|_indirect_true|_extern)?\b") {
        ++$calls
    }
    if ($text -match "\bcontext_barrier\b") {
        ++$contextBarriers
    }
    if ($lastWasByteSwap -and $text -match "^\s*store(\.[0-9]+)?\s") {
        ++$storeAfterByteSwap
    }
    $lastWasByteSwap = $text -match "byte_swap"
}

Write-Output "# HIR Hotpath Report"
Write-Output ""
Write-Output ("log={0}" -f $resolvedLog)
Write-Output ("function={0}" -f $Function.ToUpperInvariant())
Write-Output ("phase={0}" -f $Phase)
Write-Output ("hir_lines={0}" -f $hirLines)
Write-Output ("dot_product_3={0}" -f $dotProduct3)
Write-Output ("dot_product_4={0}" -f $dotProduct4)
Write-Output ("permutes={0}" -f $permutes)
Write-Output ("byte_swaps={0}" -f $byteSwaps)
Write-Output ("v128_byte_swaps={0}" -f $v128ByteSwaps)
Write-Output ("store_after_byte_swap={0}" -f $storeAfterByteSwap)
Write-Output ("branches={0}" -f $branches)
Write-Output ("calls={0}" -f $calls)
Write-Output ("context_barriers={0}" -f $contextBarriers)

Write-TopTable "Opcode Top" $opCounts $Top
Write-TopTable "Context Load Classes" $contextLoadClasses $Top
Write-TopTable "Context Store Classes" $contextStoreClasses $Top
Write-TopTable "Context Load Offsets" $contextLoadOffsets $Top -AnnotateContextOffsets
Write-TopTable "Context Store Offsets" $contextStoreOffsets $Top -AnnotateContextOffsets
Write-TopTable "PERMUTE_I32 Controls" $permuteControls $Top
Write-TopTable "Memory Load Ops" $memoryLoads $Top
Write-TopTable "Memory Store Ops" $memoryStores $Top
