param(
    [Parameter(Mandatory = $true)]
    [string]$ParentLogPath,
    [Parameter(Mandatory = $true)]
    [string]$ParentFunction,
    [Parameter(Mandatory = $true)]
    [string]$ParentCallPc,
    [Parameter(Mandatory = $true)]
    [string]$CalleeLogPath,
    [Parameter(Mandatory = $true)]
    [string]$CalleeFunction,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [int]$Offset = 296,
    [string]$EdgeProfileLog = "",
    [string]$PreservedHelperPattern = "__savegprlr_|__restgprlr_",
    [string]$PreservedCallPcs = "",
    [switch]$CompareExistingStackCarrier,
    [int]$Top = 24
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
        context_field = if ($null -ne $offset) { Get-ContextOffsetName $offset } else { "" }
        region_name = $RegionName
    }
}

function Read-HirFunction {
    param(
        [string]$LogPath,
        [string]$Function,
        [string]$Phase
    )

    if (!(Test-Path -LiteralPath $LogPath)) {
        throw "LogPath not found: $LogPath"
    }
    $resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
    $functionUpper = $Function.ToUpperInvariant()
    $needle = "Filtered function dump $functionUpper ${Phase}:"
    $rows = New-Object System.Collections.Generic.List[object]
    $currentPpc = ""
    $currentOp = ""
    $regionName = "<entry>"
    $ordinal = 0

    foreach ($line in Get-Content -LiteralPath $resolvedLog) {
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
        throw "No $Phase filtered dump for $functionUpper in $resolvedLog."
    }

    [pscustomobject][ordered]@{
        log = $resolvedLog
        function = $functionUpper
        rows = $rows
    }
}

function Read-EdgeProfile {
    param(
        [string]$Path,
        [string]$ParentFunction,
        [string]$CalleeFunction
    )

    if ([string]::IsNullOrWhiteSpace($Path) -or !(Test-Path -LiteralPath $Path)) {
        return $null
    }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $parent = $ParentFunction.ToUpperInvariant()
    $callee = $CalleeFunction.ToUpperInvariant()
    $best = $null
    foreach ($line in Get-Content -LiteralPath $resolved) {
        if ($line -notmatch "A64 speed profile call edge top") {
            continue
        }
        if ($line -match "fn\s+$parent\b.*block=(?<block>[0-9A-Fa-f]{8}).*target=$callee\b.*calls_delta=(?<calls_delta>[0-9]+).*calls_total=(?<calls_total>[0-9]+).*body_ticks_delta=(?<body_delta>[0-9]+).*body_ticks_total=(?<body_total>[0-9]+).*ticks_per_call=(?<tpc>[0-9]+)") {
            $best = [pscustomobject][ordered]@{
                log = $resolved
                block = $Matches.block.ToUpperInvariant()
                calls_delta = [int64]$Matches.calls_delta
                calls_total = [int64]$Matches.calls_total
                body_ticks_delta = [int64]$Matches.body_delta
                body_ticks_total = [int64]$Matches.body_total
                ticks_per_call = [int64]$Matches.tpc
                line = $line.Trim()
            }
        }
    }
    return $best
}

function Get-CallTarget {
    param([object]$Row)
    $target = $Row.text
    if ($target -match '^call(_indirect)?(\.[0-9]+)?\s+(?<target>.+)$') {
        return $Matches.target.Trim()
    }
    return $target.Trim()
}

$preservedPcSet = @{}
foreach ($raw in ($PreservedCallPcs -split ',')) {
    $pc = $raw.Trim()
    if (![string]::IsNullOrWhiteSpace($pc)) {
        $preservedPcSet[$pc.ToUpperInvariant()] = $true
    }
}

$parent = Read-HirFunction -LogPath $ParentLogPath -Function $ParentFunction -Phase $Phase
$callee = Read-HirFunction -LogPath $CalleeLogPath -Function $CalleeFunction -Phase $Phase
$edge = Read-EdgeProfile -Path $EdgeProfileLog -ParentFunction $ParentFunction -CalleeFunction $CalleeFunction
$field = Get-ContextOffsetName $Offset
$callPcUpper = $ParentCallPc.ToUpperInvariant()

$parentCallPpc = $parent.rows | Where-Object {
    $_.ppc_address -eq $callPcUpper -and $_.kind -eq "ppc"
} | Select-Object -First 1
$parentCallRow = $parent.rows | Where-Object {
    $_.ppc_address -eq $callPcUpper -and $_.op -like "call*"
} | Select-Object -First 1
if ($null -eq $parentCallPpc) {
    throw "Parent call PPC row $callPcUpper not found in $($parent.log)."
}

$parentStores = @($parent.rows | Where-Object {
    $_.ordinal -lt $parentCallPpc.ordinal -and $_.op -eq "store_context" -and
        $null -ne $_.context_offset -and [int]$_.context_offset -eq $Offset
} | Sort-Object ordinal)
$parentSeedStore = $parentStores | Select-Object -Last 1

$calleeLoads = New-Object System.Collections.Generic.List[object]
$calleeStores = New-Object System.Collections.Generic.List[object]
$calleeCalls = New-Object System.Collections.Generic.List[object]
$loadPcs = @{}
$storePcs = @{}
$callKinds = @{}
$unsafeCallsSeen = 0
$preservedCallsSeen = 0
$helperCallsSeen = 0
$childCallsSeen = 0
$exitCallsSeen = 0
$barriersSeen = 0
$branchesSeen = 0

foreach ($row in $callee.rows) {
    if ($row.op -eq "context_barrier") {
        ++$barriersSeen
        continue
    }
    if ($row.op -like "branch*") {
        ++$branchesSeen
        continue
    }
    if ($row.op -like "call*") {
        $target = Get-CallTarget $row
        $kind = "unsafe_or_unknown"
        if ($row.op -like "call_indirect*") {
            $kind = "indirect_exit_or_unknown"
            ++$exitCallsSeen
        } elseif ($target -match $PreservedHelperPattern) {
            $kind = "preserved_helper"
            ++$helperCallsSeen
            ++$preservedCallsSeen
        } elseif ($preservedPcSet.ContainsKey($row.ppc_address)) {
            $kind = "preserved_child_pc"
            ++$childCallsSeen
            ++$preservedCallsSeen
        } else {
            ++$unsafeCallsSeen
        }
        Add-Count $callKinds $kind 1
        $calleeCalls.Add([pscustomobject][ordered]@{
            ordinal = $row.ordinal
            pc = $row.ppc_address
            op = $row.op
            target = $target
            kind = $kind
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
        Add-Count $storePcs $row.ppc_address 1
        $calleeStores.Add([pscustomobject][ordered]@{
            ordinal = $row.ordinal
            pc = $row.ppc_address
            region = $row.region_name
            text = $row.text
        }) | Out-Null
        continue
    }

    Add-Count $loadPcs $row.ppc_address 1
    $unsafeBefore = 0
    foreach ($call in $calleeCalls) {
        if ($call.ordinal -lt $row.ordinal -and
            ($call.kind -eq "unsafe_or_unknown" -or
             $call.kind -eq "indirect_exit_or_unknown")) {
            ++$unsafeBefore
        }
    }
    $calleeLoads.Add([pscustomobject][ordered]@{
        ordinal = $row.ordinal
        pc = $row.ppc_address
        region = $row.region_name
        unsafe_calls_before = $unsafeBefore
        barriers_before = $barriersSeen
        branches_before = $branchesSeen
        text = $row.text
    }) | Out-Null
}

$unsafeLoads = @($calleeLoads | Where-Object { $_.unsafe_calls_before -gt 0 })
$replaceableLoads = if ($calleeStores.Count -eq 0) {
    $calleeLoads.Count - $unsafeLoads.Count
} else {
    0
}
$staticReplaceUpper = "-"
if ($null -ne $edge -and $replaceableLoads -gt 0) {
    $staticReplaceUpper = [string]([int64]$replaceableLoads * [int64]$edge.calls_total)
}

$parentStoreStatus = if ($null -ne $parentSeedStore) { "present" } else { "missing" }
$callAbiConstraint = "a64_direct_call_abi_passes_guest_return_in_x0_only"
$variantShape = "no_patch"
$decision = "no_behavior_patch_yet"
if ($parentStoreStatus -eq "present" -and $calleeStores.Count -eq 0 -and
    $replaceableLoads -eq $calleeLoads.Count -and $calleeLoads.Count -gt 0) {
    $variantShape = "function_pair_entry_seed_candidate"
}
if ($calleeStores.Count -gt 0) {
    $variantShape = "blocked_by_callee_writes"
} elseif ($unsafeLoads.Count -gt 0) {
    $variantShape = "blocked_by_unsafe_call_before_load"
}

Write-Output "# HIR Function-Pair Variant Audit"
Write-Output ""
Write-Output ("parent_log={0}" -f $parent.log)
Write-Output ("callee_log={0}" -f $callee.log)
Write-Output ("parent_function={0} parent_call_pc={1} callee_function={2} phase={3}" -f $parent.function, $callPcUpper, $callee.function, $Phase)
Write-Output ("target_offset={0}:{1}" -f $Offset, $field)
Write-Output ("preserved_helper_pattern={0}" -f $PreservedHelperPattern)
Write-Output ("preserved_call_pcs={0}" -f ($(if ([string]::IsNullOrWhiteSpace($PreservedCallPcs)) { "-" } else { $PreservedCallPcs })))
if ($null -ne $edge) {
    Write-Output ("edge block={0} calls_total={1} calls_delta={2} body_ticks_total={3} body_ticks_delta={4} ticks_per_call={5}" -f $edge.block, $edge.calls_total, $edge.calls_delta, $edge.body_ticks_total, $edge.body_ticks_delta, $edge.ticks_per_call)
} else {
    Write-Output "edge=-"
}
Write-Output ""

Write-Output "## Parent Seed"
if ($null -ne $parentSeedStore) {
    $distance = $parentCallPpc.ordinal - $parentSeedStore.ordinal
    Write-Output ("parent_seed status=present pc={0} ordinal={1} distance_to_call={2} text={3}" -f $parentSeedStore.ppc_address, $parentSeedStore.ordinal, $distance, $parentSeedStore.text)
} else {
    Write-Output "parent_seed status=missing"
}
if ($null -ne $parentCallRow) {
    Write-Output ("parent_call row=present ordinal={0} text={1}" -f $parentCallRow.ordinal, $parentCallRow.text)
} else {
    Write-Output "parent_call row=missing_in_hir_call_rows"
}
Write-Output ""

Write-Output "## Callee Target Access"
Write-Output ("callee_access field={0} loads={1} stores={2} replaceable_loads={3} unsafe_loads={4} load_pcs={5} store_pcs={6}" -f $field, $calleeLoads.Count, $calleeStores.Count, $replaceableLoads, $unsafeLoads.Count, (Get-TopPairs $loadPcs $Top), (Get-TopPairs $storePcs $Top))
foreach ($load in ($calleeLoads | Sort-Object ordinal | Select-Object -First $Top)) {
    Write-Output ("load pc={0} ordinal={1} region={2} unsafe_calls_before={3} barriers_before={4} branches_before={5}" -f $load.pc, $load.ordinal, $load.region, $load.unsafe_calls_before, $load.barriers_before, $load.branches_before)
}
Write-Output ""

Write-Output "## Callee Calls"
Write-Output ("callee_calls total={0} preserved={1} helpers={2} preserved_child={3} indirect_or_exit={4} unsafe_or_unknown={5} kinds={6}" -f $calleeCalls.Count, $preservedCallsSeen, $helperCallsSeen, $childCallsSeen, $exitCallsSeen, $unsafeCallsSeen, (Get-TopPairs $callKinds $Top))
foreach ($call in ($calleeCalls | Sort-Object ordinal | Select-Object -First $Top)) {
    Write-Output ("call pc={0} ordinal={1} op={2} kind={3} target={4}" -f $call.pc, $call.ordinal, $call.op, $call.kind, $call.target)
}
Write-Output ""

Write-Output "## Variant Shape"
Write-Output ("variant_shape={0}" -f $variantShape)
Write-Output ("call_abi_constraint={0}" -f $callAbiConstraint)
Write-Output ("static_replace_upper={0}" -f $staticReplaceUpper)
Write-Output "required_design=pair-specific callee entry or thunk; do not alter normal callee entry; keep parent context store visible unless a later visibility audit proves it movable"
Write-Output "required_flush=kill or fall back on unknown calls, indirect child calls before target loads, callee target stores, exits/exceptions, and overlapping context writes"
Write-Output ""

if ($CompareExistingStackCarrier) {
    $existingCarrierSeedLoads = if ($replaceableLoads -gt 0) { 1 } else { 0 }
    $existingCarrierReuseLoads = [Math]::Max(0, $replaceableLoads - $existingCarrierSeedLoads)
    $pairIncrementalContextLoads = $existingCarrierSeedLoads
    $pairIncrementalStaticUpper = "-"
    if ($null -ne $edge -and $pairIncrementalContextLoads -gt 0) {
        $pairIncrementalStaticUpper =
            [string]([int64]$pairIncrementalContextLoads * [int64]$edge.calls_total)
    }
    Write-Output "## Existing Stack-Carrier Comparison"
    Write-Output (
        "stack_carrier_compare enabled=True existing_seed_context_loads={0} existing_reuse_loads={1} pair_incremental_context_loads={2} pair_incremental_static_upper={3}" -f
        $existingCarrierSeedLoads, $existingCarrierReuseLoads,
        $pairIncrementalContextLoads, $pairIncrementalStaticUpper)
    Write-Output "stack_carrier_read=pair entry removes only the seed context load if the current stack-slot carrier already covers the remaining loads; weigh this against new pair-entry ABI/prolog/flag overhead."
    Write-Output ""
}

Write-Output "## Decision"
Write-Output ("decision={0}" -f $decision)
if ($variantShape -eq "function_pair_entry_seed_candidate") {
    Write-Output "reason=offline HIR says the callee target is read-only and all target loads are before only preserved child/helper calls, but A64 direct-call ABI currently passes guest return in x0 only."
    Write-Output "next_experiment=source-design a default-off pair-specific call/entry seed path for f[1], or add a compile-time audit that proves the caller can seed a callee-local slot without changing the generic A64 call ABI."
} else {
    Write-Output "reason=target is not safe for a pair-specific entry seed under the current static audit."
    Write-Output "next_experiment=do not patch generated behavior; improve the audit or choose another body-backed lane."
}
