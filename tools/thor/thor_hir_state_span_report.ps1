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

function Format-OffsetKey {
    param([int]$Offset)
    return ("+{0} {1}" -f $Offset, (Get-ContextOffsetName $Offset))
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

function Add-CandidateStat {
    param(
        [hashtable]$Table,
        [int]$Offset,
        [string]$Metric,
        [int]$Amount = 1
    )

    if ((Get-ContextOffsetClass $Offset) -ne "GPR") {
        return
    }
    if (!$Table.ContainsKey($Offset)) {
        $Table[$Offset] = [ordered]@{
            same_repeated_loads = 0
            same_load_after_store = 0
            same_store_after_load = 0
            cross_load_after_store = 0
            cross_load_after_load = 0
        }
    }
    $Table[$Offset][$Metric] += $Amount
}

function Write-GprCandidatePlan {
    param(
        [hashtable]$Table,
        [int]$Limit
    )

    Write-Output ""
    Write-Output "## Candidate GPR State Cache Plan"
    if ($Table.Count -eq 0) {
        Write-Output "(none)"
        return
    }

    Write-Output "First-patch flush model: keep only clean INT64 GPR values; do not elide stores; preserve across no-op context_barrier only after proving with a guarded cvar; reset on calls, branches, labels, helper-expanded instruction ranges, volatile ops, and overlapping non-GPR context writes."

    $rows = foreach ($entry in $Table.GetEnumerator()) {
        $stats = $entry.Value
        $score =
            (3 * $stats.cross_load_after_store) +
            (2 * $stats.cross_load_after_load) +
            (2 * $stats.same_load_after_store) +
            $stats.same_repeated_loads +
            $stats.same_store_after_load
        [pscustomobject]@{
            Offset = [int]$entry.Key
            Name = Get-ContextOffsetName ([int]$entry.Key)
            Score = $score
            SameRepeatedLoads = $stats.same_repeated_loads
            SameLoadAfterStore = $stats.same_load_after_store
            SameStoreAfterLoad = $stats.same_store_after_load
            CrossLoadAfterStore = $stats.cross_load_after_store
            CrossLoadAfterLoad = $stats.cross_load_after_load
        }
    }

    $rows |
        Sort-Object -Property @{ Expression = "Score"; Descending = $true },
                              @{ Expression = "Offset"; Ascending = $true } |
        Select-Object -First $Limit |
        ForEach-Object {
            Write-Output ("+{0} {1} score={2} same_reloads={3} same_las={4} same_sal={5} cross_las={6} cross_lal={7}" -f
                $_.Offset, $_.Name, $_.Score, $_.SameRepeatedLoads,
                $_.SameLoadAfterStore, $_.SameStoreAfterLoad,
                $_.CrossLoadAfterStore, $_.CrossLoadAfterLoad)
        }
}

function New-Span {
    param(
        [string]$Label,
        [int]$Index
    )
    return [ordered]@{
        index = $Index
        label = $Label
        loads = 0
        stores = 0
        barriers = 0
        calls = 0
        branches = 0
        context_accesses = 0
    }
}

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}

$resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
$functionPattern = [Regex]::Escape($Function.ToUpperInvariant())
$phasePattern = [Regex]::Escape($Phase)
$linePattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"

$sameSpanRepeatedLoads = @{}
$sameSpanLoadAfterStore = @{}
$sameSpanStoreAfterStore = @{}
$sameSpanStoreAfterLoad = @{}
$crossSpanLoadAfterStore = @{}
$crossSpanLoadAfterLoad = @{}
$gprCandidateStats = @{}
$contextAccessClasses = @{}
$spanHotness = @{}

$spans = New-Object System.Collections.Generic.List[object]
$spanIndex = 0
$currentLabel = "<entry>"
$currentSpan = New-Span $currentLabel $spanIndex
$spans.Add($currentSpan) | Out-Null
$spanLastAccess = @{}
$spanSeenOffsets = @{}
$globalLastAccess = @{}

$hirLines = 0
$barriers = 0

Get-Content -LiteralPath $resolvedLog | ForEach-Object {
    if ($_ -notmatch $linePattern) {
        return
    }

    $text = $Matches.text
    if ($text -match "^\s*loc_([0-9A-Fa-f]+):") {
        $currentLabel = $Matches[0].TrimEnd(":")
        ++$spanIndex
        $currentSpan = New-Span $currentLabel $spanIndex
        $spans.Add($currentSpan) | Out-Null
        $spanLastAccess = @{}
        $spanSeenOffsets = @{}
        return
    }
    if ($text -match "^\s*(;|<entry>|$)") {
        return
    }

    ++$hirLines

    if ($text -match "\bcontext_barrier\b") {
        ++$barriers
        $currentSpan.barriers += 1
        ++$spanIndex
        $currentSpan = New-Span ("{0}#after_barrier_{1}" -f $currentLabel, $barriers) $spanIndex
        $spans.Add($currentSpan) | Out-Null
        $spanLastAccess = @{}
        $spanSeenOffsets = @{}
        return
    }
    if ($text -match "\bcall(_true|_indirect|_indirect_true|_extern)?\b") {
        $currentSpan.calls += 1
    }
    if ($text -match "\bbranch(_true|_false)?\b") {
        $currentSpan.branches += 1
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

    $key = Format-OffsetKey $offset
    Add-Count $contextAccessClasses (Get-ContextOffsetClass $offset)
    $currentSpan.context_accesses += 1

    if ($isLoad) {
        $currentSpan.loads += 1
        if ($spanLastAccess.ContainsKey($offset)) {
            if ($spanLastAccess[$offset] -eq "load") {
                Add-Count $sameSpanRepeatedLoads $key
                Add-CandidateStat $gprCandidateStats $offset "same_repeated_loads"
            } elseif ($spanLastAccess[$offset] -eq "store") {
                Add-Count $sameSpanLoadAfterStore $key
                Add-CandidateStat $gprCandidateStats $offset "same_load_after_store"
            }
        } elseif ($globalLastAccess.ContainsKey($offset)) {
            if ($globalLastAccess[$offset] -eq "store") {
                Add-Count $crossSpanLoadAfterStore $key
                Add-CandidateStat $gprCandidateStats $offset "cross_load_after_store"
            } elseif ($globalLastAccess[$offset] -eq "load") {
                Add-Count $crossSpanLoadAfterLoad $key
                Add-CandidateStat $gprCandidateStats $offset "cross_load_after_load"
            }
        }
        $spanLastAccess[$offset] = "load"
        $globalLastAccess[$offset] = "load"
    } elseif ($isStore) {
        $currentSpan.stores += 1
        if ($spanLastAccess.ContainsKey($offset)) {
            if ($spanLastAccess[$offset] -eq "store") {
                Add-Count $sameSpanStoreAfterStore $key
            } elseif ($spanLastAccess[$offset] -eq "load") {
                Add-Count $sameSpanStoreAfterLoad $key
                Add-CandidateStat $gprCandidateStats $offset "same_store_after_load"
            }
        }
        $spanLastAccess[$offset] = "store"
        $globalLastAccess[$offset] = "store"
    }
}

foreach ($span in $spans) {
    if ($span.context_accesses -gt 0) {
        $label = ("{0:D4} {1} loads={2} stores={3} calls={4} branches={5}" -f
            $span.index, $span.label, $span.loads, $span.stores, $span.calls,
            $span.branches)
        $spanHotness[$label] = $span.context_accesses
    }
}

Write-Output "# HIR State Span Report"
Write-Output ""
Write-Output ("log={0}" -f $resolvedLog)
Write-Output ("function={0}" -f $Function.ToUpperInvariant())
Write-Output ("phase={0}" -f $Phase)
Write-Output ("hir_lines={0}" -f $hirLines)
Write-Output ("spans={0}" -f $spans.Count)
Write-Output ("context_barriers={0}" -f $barriers)

Write-TopTable "Context Access Classes" $contextAccessClasses $Top
Write-TopTable "Hot Spans By Context Accesses" $spanHotness $Top
Write-TopTable "Same-Span Repeated Loads" $sameSpanRepeatedLoads $Top
Write-TopTable "Same-Span Load After Store" $sameSpanLoadAfterStore $Top
Write-TopTable "Same-Span Store After Store" $sameSpanStoreAfterStore $Top
Write-TopTable "Same-Span Store After Load" $sameSpanStoreAfterLoad $Top
Write-TopTable "Cross-Span Load After Store" $crossSpanLoadAfterStore $Top
Write-TopTable "Cross-Span Load After Load" $crossSpanLoadAfterLoad $Top
Write-GprCandidatePlan $gprCandidateStats $Top
