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
$phasePattern = if ($Phase -eq "Both") { "(RawHIR|OptHIR)" } else { [Regex]::Escape($Phase) }
$linePattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"

$opCounts = @{}
$contextLoadOffsets = @{}
$contextStoreOffsets = @{}
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
        Add-Count $contextLoadOffsets ("+" + $Matches[1])
    }
    if ($text -match "store_context \+([0-9]+)") {
        Add-Count $contextStoreOffsets ("+" + $Matches[1])
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
Write-TopTable "Context Load Offsets" $contextLoadOffsets $Top
Write-TopTable "Context Store Offsets" $contextStoreOffsets $Top
Write-TopTable "PERMUTE_I32 Controls" $permuteControls $Top
Write-TopTable "Memory Load Ops" $memoryLoads $Top
Write-TopTable "Memory Store Ops" $memoryStores $Top
