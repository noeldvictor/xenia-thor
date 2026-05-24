param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [Parameter(Mandatory = $true)]
    [string]$StartGuest,
    [Parameter(Mandatory = $true)]
    [string]$EndGuest,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [ValidateSet("BlueDragonCallBoundaryDead", "AllSpanStoreContext")]
    [string]$TargetMode = "BlueDragonCallBoundaryDead",
    [int]$Top = 24
)

$ErrorActionPreference = "Stop"

function Convert-HexToInt64 {
    param([string]$Hex)
    return [Convert]::ToInt64($Hex, 16)
}

function Add-Count {
    param(
        [hashtable]$Table,
        [string]$Key,
        [int64]$Amount = 1
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

function Get-TopPairs {
    param(
        [hashtable]$Table,
        [int]$Limit = 12
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
        $names = @("xer_ca", "xer_ov", "xer_so", "xer_pad")
        return $names[$Offset - 2592]
    }
    if ($Offset -ge 2596 -and $Offset -lt 2628) {
        $crIndex = [Math]::Floor(($Offset - 2596) / 4)
        $fieldIndex = ($Offset - 2596) % 4
        $fields = @(
            @("lt", "gt", "eq", "so"),
            @("fx", "fex", "vx", "ox"),
            @("0", "1", "2", "3"),
            @("0", "1", "2", "3"),
            @("0", "1", "2", "3"),
            @("0", "1", "2", "3"),
            @("all_equal", "1", "none_equal", "3"),
            @("0", "1", "2", "3")
        )
        return ("cr{0}.{1}" -f $crIndex, $fields[$crIndex][$fieldIndex])
    }
    if ($Offset -ge 2628 -and $Offset -lt 2632) { return "fpscr" }
    if ($Offset -eq 2632) { return "vscr_sat" }
    if ($Offset -eq 2636) { return "thread_id" }
    if ($Offset -ge 2640) { return "runtime_or_reservation" }
    return "unknown"
}

function Parse-StoreContextOffset {
    param([string]$Text)
    if ($Text -match '^store_context\s+\+([0-9]+),') {
        return [int]$Matches[1]
    }
    return $null
}

function Get-ValueRefs {
    param([string]$Text)
    $refs = New-Object System.Collections.Generic.List[string]
    foreach ($match in [regex]::Matches($Text, '\bv([0-9]+)\.[A-Za-z0-9_]+(?:<[^>]+>)?')) {
        $refs.Add(("v{0}" -f $match.Groups[1].Value)) | Out-Null
    }
    return $refs
}

function Is-SideEffectFreeAssignment {
    param([pscustomobject]$Node)
    if ([string]::IsNullOrWhiteSpace($Node.Dest)) {
        return $false
    }
    switch -Regex ($Node.Op) {
        '^(store|store\.|store_offset|store_context)$' { return $false }
        '^(call|branch|branch_true|branch_false|context_barrier|set_return_address)$' { return $false }
        default { return $true }
    }
}

function New-Node {
    param(
        [int]$Index,
        [int64]$Pc,
        [string]$Text
    )
    $dest = ""
    $op = ""
    $srcs = New-Object System.Collections.Generic.List[string]

    if ($Text -match '^(v[0-9]+)\.[^=]+=\s*([^\s]+)\s*(.*)$') {
        $dest = $Matches[1]
        $op = $Matches[2]
        $rhs = $Matches[3]
        foreach ($ref in Get-ValueRefs $rhs) {
            $srcs.Add($ref) | Out-Null
        }
    } else {
        $parts = $Text -split '\s+', 2
        $op = $parts[0]
        foreach ($ref in Get-ValueRefs $Text) {
            $srcs.Add($ref) | Out-Null
        }
    }

    $storeOffset = Parse-StoreContextOffset $Text
    [pscustomobject][ordered]@{
        index = $Index
        pc = $Pc
        pc_hex = ("{0:X8}" -f $Pc)
        text = $Text
        op = $op
        dest = $dest
        srcs = @($srcs)
        store_offset = $storeOffset
        target_store = $false
    }
}

$resolvedLog = (Resolve-Path $LogPath).Path
$start = Convert-HexToInt64 $StartGuest
$end = Convert-HexToInt64 $EndGuest
$functionUpper = $Function.ToUpperInvariant()
$dumpNeedle = "Filtered function dump $functionUpper ${Phase}:"

$nodes = New-Object System.Collections.Generic.List[object]
$currentPc = -1
$index = 0
foreach ($line in Get-Content -Path $resolvedLog) {
    $needleIndex = $line.IndexOf($dumpNeedle)
    if ($needleIndex -lt 0) {
        continue
    }
    $text = $line.Substring($needleIndex + $dumpNeedle.Length).Trim()
    if ([string]::IsNullOrWhiteSpace($text)) {
        continue
    }
    if ($text -match '^;\s*([0-9A-Fa-f]{8})\s') {
        $currentPc = Convert-HexToInt64 $Matches[1]
        continue
    }
    if ($text -match '^loc_' -or $text -match '^;') {
        continue
    }
    if ($currentPc -lt 0) {
        continue
    }
    $nodes.Add((New-Node -Index $index -Pc $currentPc -Text $text)) | Out-Null
    ++$index
}

$targetPairs = @{}
if ($TargetMode -eq "BlueDragonCallBoundaryDead") {
    $deadPairs = @(
        @("82282534", 720), @("8228253C", 688), @("82282540", 672),
        @("82282554", 704), @("82282574", 752), @("82282578", 736),
        @("8228257C", 544), @("8228252C", 72),  @("82282544", 96),
        @("82282548", 88),  @("82282558", 104), @("8228258C", 392),
        @("82282590", 288)
    )
    foreach ($pair in $deadPairs) {
        $targetPairs[("{0}:{1}" -f $pair[0].ToUpperInvariant(), $pair[1])] = $true
    }
}

$targetStores = New-Object System.Collections.Generic.List[object]
foreach ($node in $nodes) {
    $inSpan = $node.pc -ge $start -and $node.pc -le $end
    if (!$inSpan -or $node.op -ne "store_context" -or $null -eq $node.store_offset) {
        continue
    }
    if ($TargetMode -eq "AllSpanStoreContext") {
        $node.target_store = $true
    } else {
        $key = "{0}:{1}" -f $node.pc_hex, $node.store_offset
        $node.target_store = $targetPairs.ContainsKey($key)
    }
    if ($node.target_store) {
        $targetStores.Add($node) | Out-Null
    }
}

$useCounts = @{}
$producerByValue = @{}
foreach ($node in $nodes) {
    if (![string]::IsNullOrWhiteSpace($node.dest)) {
        $producerByValue[$node.dest] = $node
    }
}
foreach ($node in $nodes) {
    if ($node.target_store) {
        continue
    }
    foreach ($src in $node.srcs) {
        Add-Count $useCounts $src 1
    }
}

$queue = New-Object System.Collections.Generic.Queue[object]
foreach ($node in $nodes) {
    if ((Is-SideEffectFreeAssignment $node) -and
        (!$useCounts.ContainsKey($node.dest) -or $useCounts[$node.dest] -eq 0)) {
        $queue.Enqueue($node)
    }
}

$removed = @{}
$deadNodes = New-Object System.Collections.Generic.List[object]
while ($queue.Count -gt 0) {
    $node = $queue.Dequeue()
    if ($removed.ContainsKey($node.index)) {
        continue
    }
    if (!(Is-SideEffectFreeAssignment $node)) {
        continue
    }
    if ($useCounts.ContainsKey($node.dest) -and $useCounts[$node.dest] -gt 0) {
        continue
    }
    $removed[$node.index] = $true
    $deadNodes.Add($node) | Out-Null
    foreach ($src in $node.srcs) {
        if (!$useCounts.ContainsKey($src)) {
            continue
        }
        --$useCounts[$src]
        if ($useCounts[$src] -eq 0 -and $producerByValue.ContainsKey($src)) {
            $producer = $producerByValue[$src]
            if (Is-SideEffectFreeAssignment $producer) {
                $queue.Enqueue($producer)
            }
        }
    }
}

$targetStoreFields = @{}
$targetStorePcs = @{}
foreach ($store in $targetStores) {
    Add-Count $targetStoreFields (Get-ContextOffsetName $store.store_offset) 1
    Add-Count $targetStorePcs ("{0}:{1}" -f $store.pc_hex, (Get-ContextOffsetName $store.store_offset)) 1
}

$deadOps = @{}
$deadPcs = @{}
$deadInSpan = 0
$deadOutsideSpan = 0
foreach ($node in $deadNodes) {
    Add-Count $deadOps $node.op 1
    Add-Count $deadPcs $node.pc_hex 1
    if ($node.pc -ge $start -and $node.pc -le $end) {
        ++$deadInSpan
    } else {
        ++$deadOutsideSpan
    }
}

$spanStoreCount = 0
foreach ($node in $nodes) {
    if ($node.pc -ge $start -and $node.pc -le $end -and $node.op -eq "store_context") {
        ++$spanStoreCount
    }
}

$decision = "no_hir_dce_patch"
$reason = "targeted store removal has no recursive dead assignment fallout beyond the store_context side effects"
if ($deadNodes.Count -ge 8) {
    $decision = "possible_hir_dce_probe"
    $reason = "targeted store removal exposes non-trivial upstream dead assignments; still needs route-gated default-off proof"
}
if ($TargetMode -eq "AllSpanStoreContext") {
    $decision = "upper_bound_only"
    $reason = "all-span store removal includes live/debug/branch/call state and is not directly safe"
}

Write-Output "# HIR Dead State Store DCE Audit"
Write-Output ""
Write-Output ("log={0}" -f $resolvedLog)
Write-Output ("function={0}" -f $functionUpper)
Write-Output ("phase={0}" -f $Phase)
Write-Output ("span={0}-{1}" -f $StartGuest.ToUpperInvariant(), $EndGuest.ToUpperInvariant())
Write-Output ("target_mode={0}" -f $TargetMode)
Write-Output ("function_hir_instr={0} span_store_context={1} target_store_context={2}" -f $nodes.Count, $spanStoreCount, $targetStores.Count)
Write-Output ("target_fields={0}" -f (Get-TopPairs $targetStoreFields $Top))
Write-Output ("target_pcs={0}" -f (Get-TopPairs $targetStorePcs $Top))
Write-Output ""
Write-Output "## Recursive DCE Estimate"
Write-Output ("dead_assignments={0} dead_in_span={1} dead_outside_span={2}" -f $deadNodes.Count, $deadInSpan, $deadOutsideSpan)
Write-Output ("dead_ops={0}" -f (Get-TopPairs $deadOps $Top))
Write-Output ("dead_pcs={0}" -f (Get-TopPairs $deadPcs $Top))
Write-Output ("decision={0}" -f $decision)
Write-Output ("reason={0}" -f $reason)
Write-Output ""
Write-Output "## Target Stores"
foreach ($store in ($targetStores | Sort-Object pc, store_offset)) {
    $srcList = if ($store.srcs.Count -gt 0) { $store.srcs -join "," } else { "-" }
    Write-Output ("target pc={0} offset={1} field={2} srcs={3} text={4}" -f $store.pc_hex, $store.store_offset, (Get-ContextOffsetName $store.store_offset), $srcList, $store.text)
}
Write-Output ""
Write-Output "## Dead Assignments"
foreach ($node in ($deadNodes | Sort-Object pc, index | Select-Object -First $Top)) {
    $srcList = if ($node.srcs.Count -gt 0) { $node.srcs -join "," } else { "-" }
    Write-Output ("dead pc={0} op={1} dest={2} srcs={3} text={4}" -f $node.pc_hex, $node.op, $node.dest, $srcList, $node.text)
}
