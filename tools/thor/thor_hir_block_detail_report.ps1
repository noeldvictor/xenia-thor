param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [Parameter(Mandatory = $true)]
    [string]$BlockGuest,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [int]$Top = 16,
    [switch]$ShowLines
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

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}

$resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
$functionPattern = [Regex]::Escape($Function.ToUpperInvariant())
$blockPattern = [Regex]::Escape($BlockGuest.ToUpperInvariant())
$phasePattern = [Regex]::Escape($Phase)
$linePattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"

$inBlock = $false
$blockLines = New-Object System.Collections.Generic.List[string]

Get-Content -LiteralPath $resolvedLog | ForEach-Object {
    if ($_ -notmatch $linePattern) {
        return
    }
    $text = $Matches.text
    if ($inBlock -and $text -match "^\s*loc_[0-9A-Fa-f]+:") {
        $inBlock = $false
        return
    }
    if (!$inBlock -and $text -match "^\s*loc_$blockPattern`:") {
        $inBlock = $true
    }
    if ($inBlock) {
        $blockLines.Add($text) | Out-Null
    }
}

if ($blockLines.Count -eq 0) {
    throw "Block loc_$($BlockGuest.ToUpperInvariant()) not found in $Function $Phase dump."
}

$ppcOps = @{}
$ppcTargets = @{}
$contextLoads = @{}
$contextStores = @{}
$memoryLoads = 0
$memoryStores = 0
$permutes = 0
$dot4 = 0
$mulAdd = 0
$splats = 0
$extracts = 0
$branches = @{}
$calls = 0
$barriers = 0
$instructions = 0
$labels = 0

foreach ($text in $blockLines) {
    if ($text -match "^\s*_label[0-9]+:") {
        $labels += 1
        continue
    }
    if ($text -match "^\s*;\s+([0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+([A-Za-z0-9_\.]+)\s*(.*)$") {
        $op = $Matches[2].ToLowerInvariant()
        Add-Count $ppcOps $op
        if ($op -eq "bl" -and $Matches[3] -match "0x([0-9A-Fa-f]{8})") {
            Add-Count $ppcTargets ("bl->0x" + $Matches[1].ToUpperInvariant())
        }
        continue
    }
    if ($text -match "^\s*(;|$)") {
        continue
    }

    $instructions += 1
    if ($text -match "load_context \+([0-9]+)") {
        Add-Count $contextLoads (Get-ContextOffsetName ([int]$Matches[1]))
    }
    if ($text -match "store_context \+([0-9]+)") {
        Add-Count $contextStores (Get-ContextOffsetName ([int]$Matches[1]))
    }
    if ($text -match "\b(load|load_offset|load_vector_sh[lr])(\.[0-9]+)?\b") {
        $memoryLoads += 1
    }
    if ($text -match "\b(store|store_offset|store_vector_sh[lr])(\.[0-9]+)?\b") {
        $memoryStores += 1
    }
    if ($text -match "\bpermute\b") {
        $permutes += 1
    }
    if ($text -match "\bdot_product_4\b") {
        $dot4 += 1
    }
    if ($text -match "\bmul_add\b") {
        $mulAdd += 1
    }
    if ($text -match "\bsplat\b") {
        $splats += 1
    }
    if ($text -match "\bextract\b") {
        $extracts += 1
    }
    if ($text -match "\b(branch(?:_true|_false)?)\b") {
        Add-Count $branches $Matches[1]
    }
    if ($text -match "\bcall(_true|_indirect|_indirect_true|_extern)?\b") {
        $calls += 1
    }
    if ($text -match "\bcontext_barrier\b") {
        $barriers += 1
    }
}

Write-Output "# HIR Block Detail Report"
Write-Output ""
Write-Output "log=$resolvedLog"
Write-Output "function=$($Function.ToUpperInvariant())"
Write-Output "phase=$Phase"
Write-Output "block=loc_$($BlockGuest.ToUpperInvariant())"
Write-Output "labels=$labels"
Write-Output "instructions=$instructions"
Write-Output "context_loads=$(($contextLoads.Values | Measure-Object -Sum).Sum)"
Write-Output "context_stores=$(($contextStores.Values | Measure-Object -Sum).Sum)"
Write-Output "memory_loads=$memoryLoads"
Write-Output "memory_stores=$memoryStores"
Write-Output "permutes=$permutes"
Write-Output "dot_product_4=$dot4"
Write-Output "mul_add=$mulAdd"
Write-Output "splat=$splats"
Write-Output "extract=$extracts"
Write-Output "calls=$calls"
Write-Output "context_barriers=$barriers"
Write-Output "branches=$(Get-TopPairs $branches $Top)"
Write-Output "ppc_ops=$(Get-TopPairs $ppcOps $Top)"
Write-Output "context_load_slots=$(Get-TopPairs $contextLoads $Top)"
Write-Output "context_store_slots=$(Get-TopPairs $contextStores $Top)"
Write-Output "call_targets=$(Get-TopPairs $ppcTargets $Top)"

if ($ShowLines) {
    Write-Output ""
    Write-Output "## Block Lines"
    $blockLines | ForEach-Object { Write-Output $_ }
}
