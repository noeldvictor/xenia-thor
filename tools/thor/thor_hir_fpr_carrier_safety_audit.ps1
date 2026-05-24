param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [int]$Offset = 296,
    [string]$HelperCallPattern = "__savegprlr_|__restgprlr_",
    [int]$Top = 20
)

$ErrorActionPreference = "Stop"

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
    if ($Offset -ge 2628 -and $Offset -lt 2632) { return "fpscr" }
    return "unknown"
}

function Parse-ContextOffset {
    param(
        [string]$Text,
        [string]$Op
    )
    if ($Op -eq "store_context" -and $Text -match '^store_context\s+\+([0-9]+),') {
        return [int]$Matches[1]
    }
    if ($Op -eq "load_context" -and $Text -match '=\s*load_context\s+\+([0-9]+)\b') {
        return [int]$Matches[1]
    }
    return $null
}

function New-HirRow {
    param(
        [int]$Ordinal,
        [string]$Kind,
        [string]$Text,
        [string]$PpcAddress,
        [string]$PpcOp,
        [string]$RegionName
    )

    $op = ""
    if ($Kind -eq "ppc") {
        $op = $PpcOp
    } elseif ($Text -match '^\S+\s*=\s*(?<op>[A-Za-z0-9_\.]+)\b') {
        $op = $Matches.op.ToLowerInvariant()
    } elseif ($Text -match '^(?<op>[A-Za-z0-9_\.]+)\b') {
        $op = $Matches.op.ToLowerInvariant()
    }
    $offset = Parse-ContextOffset $Text $op

    [pscustomobject][ordered]@{
        ordinal = $Ordinal
        kind = $Kind
        text = $Text
        ppc_address = $PpcAddress
        ppc_op = $PpcOp
        op = $op
        context_offset = $offset
        region_name = $RegionName
    }
}

function Read-HirFunction {
    param(
        [string]$Path,
        [string]$Function,
        [string]$Phase
    )

    if (!(Test-Path -LiteralPath $Path)) {
        throw "LogPath not found: $Path"
    }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $functionUpper = $Function.ToUpperInvariant()
    $needle = "Filtered function dump $functionUpper ${Phase}:"
    $rows = New-Object System.Collections.Generic.List[object]
    $currentPpc = ""
    $currentOp = ""
    $regionName = "<entry>"
    $ordinal = 0

    foreach ($line in Get-Content -LiteralPath $resolved) {
        $needleIndex = $line.IndexOf($needle)
        if ($needleIndex -lt 0) {
            continue
        }
        $text = $line.Substring($needleIndex + $needle.Length).Trim()
        if ([string]::IsNullOrWhiteSpace($text) -or $text -match '^<entry>:') {
            continue
        }
        if ($text -match '^;\s*(in:|out:|default|$)') {
            continue
        }
        if ($text -match '^(loc_[0-9A-Fa-f]+|_label[0-9A-Fa-f]+):') {
            $regionName = $Matches[1]
            $rows.Add((New-HirRow $ordinal "label" $text $currentPpc $currentOp $regionName)) | Out-Null
            ++$ordinal
            continue
        }
        if ($text -match '^;\s*([0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+([A-Za-z0-9_\.]+)\b') {
            $currentPpc = $Matches[1].ToUpperInvariant()
            $currentOp = $Matches[2].ToLowerInvariant()
            $rows.Add((New-HirRow $ordinal "ppc" $text $currentPpc $currentOp $regionName)) | Out-Null
            ++$ordinal
            continue
        }
        if ($text -match '^;') {
            continue
        }
        $rows.Add((New-HirRow $ordinal "hir" $text $currentPpc $currentOp $regionName)) | Out-Null
        ++$ordinal
    }

    if ($rows.Count -eq 0) {
        throw "No $Phase filtered dump for $functionUpper in $resolved."
    }

    [pscustomobject][ordered]@{
        log = $resolved
        function = $functionUpper
        rows = $rows
    }
}

$hir = Read-HirFunction -Path $LogPath -Function $Function -Phase $Phase
$targetName = Get-ContextOffsetName $Offset

$calls = New-Object System.Collections.Generic.List[object]
$loads = New-Object System.Collections.Generic.List[object]
$stores = New-Object System.Collections.Generic.List[object]
$callTargets = @{}
$loadPcs = @{}

$helperCallsSeen = 0
$unknownCallsSeen = 0
$barriersSeen = 0
$branchesSeen = 0
$labelsSeen = 0
$storesSeen = 0

foreach ($row in $hir.rows) {
    if ($row.kind -eq "label") {
        ++$labelsSeen
        continue
    }
    if ($row.op -eq "context_barrier") {
        ++$barriersSeen
        continue
    }
    if ($row.op -like "branch*") {
        ++$branchesSeen
        continue
    }
    if ($row.op -like "call*") {
        $target = $row.text
        if ($target -match '^call(\.[0-9]+)?\s+(?<target>.+)$') {
            $target = $Matches.target.Trim()
        }
        $isHelper = $target -match $HelperCallPattern
        if ($isHelper) {
            ++$helperCallsSeen
        } else {
            ++$unknownCallsSeen
        }
        Add-Count $callTargets $target 1
        $calls.Add([pscustomobject][ordered]@{
            ordinal = $row.ordinal
            pc = $row.ppc_address
            target = $target
            helper = $isHelper
            text = $row.text
        }) | Out-Null
        continue
    }

    if ($row.op -ne "load_context" -and $row.op -ne "store_context") {
        continue
    }
    if ($null -eq $row.context_offset -or [int]$row.context_offset -ne $Offset) {
        continue
    }

    if ($row.op -eq "store_context") {
        ++$storesSeen
        $stores.Add([pscustomobject][ordered]@{
            ordinal = $row.ordinal
            pc = $row.ppc_address
            region = $row.region_name
            helper_calls_before = $helperCallsSeen
            unknown_calls_before = $unknownCallsSeen
            barriers_before = $barriersSeen
            branches_before = $branchesSeen
            labels_before = $labelsSeen
            stores_before = $storesSeen - 1
            text = $row.text
        }) | Out-Null
        continue
    }

    Add-Count $loadPcs $row.ppc_address 1
    $helperOnly = ($unknownCallsSeen -eq 0 -and $storesSeen -eq 0)
    $noCall = (($helperCallsSeen + $unknownCallsSeen) -eq 0 -and $storesSeen -eq 0)
    $unknownCallBlocked = ($unknownCallsSeen -gt 0)
    $storeBlocked = ($storesSeen -gt 0)
    $loads.Add([pscustomobject][ordered]@{
        ordinal = $row.ordinal
        pc = $row.ppc_address
        region = $row.region_name
        helper_calls_before = $helperCallsSeen
        unknown_calls_before = $unknownCallsSeen
        barriers_before = $barriersSeen
        branches_before = $branchesSeen
        labels_before = $labelsSeen
        stores_before = $storesSeen
        no_call_policy = $noCall
        helper_whitelist_policy = $helperOnly
        unknown_call_blocked = $unknownCallBlocked
        store_blocked = $storeBlocked
        text = $row.text
    }) | Out-Null
}

$noCallLoads = @($loads | Where-Object { $_.no_call_policy }).Count
$helperWhitelistLoads = @($loads | Where-Object { $_.helper_whitelist_policy }).Count
$unknownBlockedLoads = @($loads | Where-Object { $_.unknown_call_blocked }).Count
$storeBlockedLoads = @($loads | Where-Object { $_.store_blocked }).Count

Write-Output "# HIR FPR Carrier Safety Audit"
Write-Output ""
Write-Output ("log={0}" -f $hir.log)
Write-Output ("function={0} phase={1} target_offset={2}:{3}" -f $hir.function, $Phase, $Offset, $targetName)
Write-Output ("parsed_rows={0} calls={1} helper_calls={2} unknown_calls={3} target_loads={4} target_stores={5}" -f $hir.rows.Count, $calls.Count, $helperCallsSeen, $unknownCallsSeen, $loads.Count, $stores.Count)
Write-Output ("load_policy_counts no_call={0} helper_whitelist={1} unknown_call_blocked={2} store_blocked={3}" -f $noCallLoads, $helperWhitelistLoads, $unknownBlockedLoads, $storeBlockedLoads)
Write-Output ("call_targets={0}" -f (Get-TopPairs $callTargets $Top))
Write-Output ("load_pcs={0}" -f (Get-TopPairs $loadPcs $Top))
Write-Output ""

Write-Output "## Call Sites"
if ($calls.Count -eq 0) {
    Write-Output "(none)"
} else {
    foreach ($call in $calls) {
        Write-Output ("call ordinal={0} pc={1} helper={2} target={3} text={4}" -f $call.ordinal, $call.pc, $call.helper, $call.target, $call.text)
    }
}

Write-Output ""
Write-Output "## Target Loads"
if ($loads.Count -eq 0) {
    Write-Output "(none)"
} else {
    foreach ($load in $loads) {
        Write-Output ("load ordinal={0} pc={1} region={2} helper_calls_before={3} unknown_calls_before={4} barriers_before={5} branches_before={6} labels_before={7} stores_before={8} no_call_policy={9} helper_whitelist_policy={10} text={11}" -f
            $load.ordinal, $load.pc, $load.region, $load.helper_calls_before,
            $load.unknown_calls_before, $load.barriers_before,
            $load.branches_before, $load.labels_before, $load.stores_before,
            $load.no_call_policy, $load.helper_whitelist_policy, $load.text)
    }
}

Write-Output ""
Write-Output "## Decision"
if ($loads.Count -eq 0 -and $stores.Count -eq 0) {
    Write-Output "decision=no_target_context_access_observed"
    Write-Output "reason=the filtered function dump does not load or store the target offset; only carry across calls in this function after proving those calls are returns or target-preserving."
} elseif ($stores.Count -gt 0) {
    Write-Output "decision=no_fpr_entry_carrier"
    Write-Output "reason=target offset is stored inside the callee, so an entry carrier would need exact dirty forwarding."
} elseif ($helperWhitelistLoads -gt 0 -and $unknownBlockedLoads -gt 0) {
    Write-Output "decision=partial_helper_whitelist_candidate"
    Write-Output "reason=some loads are reachable before unknown calls if save/restore GPRLR helpers are proven not to clobber the FPR value; later loads need call-clobber proof."
} elseif ($helperWhitelistLoads -gt 0) {
    Write-Output "decision=helper_whitelist_candidate"
    Write-Output "reason=all observed target loads are blocked only by helper calls under a linear scan; prove helper semantics and CFG safety before a runtime probe."
} else {
    Write-Output "decision=no_behavior_patch_yet"
    Write-Output "reason=all useful target loads cross unknown calls or target stores under the current conservative scan."
}
Write-Output "next_experiment=source-review the helper/unknown call sites, then implement only a default-off audit/probe that counts helper-whitelist replacement opportunities before any speed A/B."
