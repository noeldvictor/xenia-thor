param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [Parameter(Mandatory = $true)]
    [string]$StartGuest,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [string]$EndGuest = "",
    [string]$BlockProfileLog = "",
    [int]$Top = 12
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

function Get-Count {
    param(
        [hashtable]$Table,
        [string]$Key
    )
    if ($Table.ContainsKey($Key)) {
        return [int64]$Table[$Key]
    }
    return [int64]0
}

function Add-EstimateRow {
    param(
        [System.Collections.Generic.List[object]]$Rows,
        [string]$Shape,
        [int64]$Count,
        [int64]$FloorPerOp,
        [string]$Source,
        [string]$Note
    )
    if ($Count -le 0 -or $FloorPerOp -le 0) {
        return
    }
    $Rows.Add([pscustomobject][ordered]@{
        shape = $Shape
        count = $Count
        floor_per_op = $FloorPerOp
        floor_total = $Count * $FloorPerOp
        source = $Source
        note = $Note
    }) | Out-Null
}

function Convert-HexToInt64 {
    param([string]$Hex)
    return [Convert]::ToInt64($Hex, 16)
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

function New-ItemRow {
    param(
        [string]$Kind,
        [int]$Ordinal,
        [string]$Address,
        [string]$Op,
        [string]$Text,
        [string]$PpcAddress,
        [string]$PpcOp
    )
    return [pscustomobject][ordered]@{
        kind = $Kind
        ordinal = $Ordinal
        address = $Address
        op = $Op
        text = $Text
        ppc_address = $PpcAddress
        ppc_op = $PpcOp
    }
}

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}

$resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
$functionUpper = $Function.ToUpperInvariant()
$startUpper = $StartGuest.ToUpperInvariant()
$endUpper = $EndGuest.ToUpperInvariant()
$functionPattern = [Regex]::Escape($functionUpper)
$phasePattern = [Regex]::Escape($Phase)
$linePattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"

$items = New-Object System.Collections.Generic.List[object]
$ppcByAddress = @{}
$currentPpcAddress = ""
$currentPpcOp = ""
$ordinal = 0

Get-Content -LiteralPath $resolvedLog | ForEach-Object {
    if ($_ -notmatch $linePattern) {
        return
    }
    $text = $Matches.text
    if ($text -match "^\s*<entry>:") {
        return
    }
    if ($text -match "^\s*(loc_[0-9A-Fa-f]+|_label[0-9A-Fa-f]+):") {
        $items.Add((New-ItemRow "label" $ordinal "" "" $text.Trim() $currentPpcAddress $currentPpcOp)) | Out-Null
        $ordinal += 1
        return
    }
    if ($text -match "^\s*;\s+(?<addr>[0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+(?<op>[A-Za-z0-9_\.]+)\s*(?<args>.*)$") {
        $currentPpcAddress = $Matches.addr.ToUpperInvariant()
        $currentPpcOp = $Matches.op.ToLowerInvariant()
        $item = New-ItemRow "ppc" $ordinal $currentPpcAddress $currentPpcOp $text.Trim() $currentPpcAddress $currentPpcOp
        $items.Add($item) | Out-Null
        if (!$ppcByAddress.ContainsKey($currentPpcAddress)) {
            $ppcByAddress[$currentPpcAddress] = $item
        }
        $ordinal += 1
        return
    }
    if ($text -match "^\s*;\s*(in:|out:|$)" -or [string]::IsNullOrWhiteSpace($text)) {
        return
    }

    $trimmed = $text.Trim()
    $op = ""
    if ($trimmed -match "^\S+\s*=\s*(?<op>[A-Za-z0-9_\.]+)\b") {
        $op = $Matches.op.ToLowerInvariant()
    } elseif ($trimmed -match "^(?<op>[A-Za-z0-9_\.]+)\b") {
        $op = $Matches.op.ToLowerInvariant()
    }
    if ([string]::IsNullOrWhiteSpace($op)) {
        return
    }
    $items.Add((New-ItemRow "hir" $ordinal "" $op $trimmed $currentPpcAddress $currentPpcOp)) | Out-Null
    $ordinal += 1
}

if (!$ppcByAddress.ContainsKey($startUpper)) {
    throw "StartGuest $startUpper not found in $Phase dump for $functionUpper."
}
$startOrdinal = [int]$ppcByAddress[$startUpper].ordinal

$profileRows = @{}
$edgeRows = @{}
if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    if (!(Test-Path -LiteralPath $BlockProfileLog)) {
        throw "BlockProfileLog not found: $BlockProfileLog"
    }
    $resolvedProfile = (Resolve-Path -LiteralPath $BlockProfileLog).Path
    $bodyPattern = "A64 speed profile block body top \d+: fn $functionPattern .* block=(?<block>\d+) guest=(?<guest>[0-9A-Fa-f]{8}) body_ticks_delta=(?<delta>\d+) body_ticks_total=(?<total>\d+) entries_delta=(?<entries>\d+) ticks_per_entry=(?<tpe>\d+)"
    $edgePattern = "A64 speed profile call edge top \d+: fn $functionPattern .* edge=(?<edge>\d+) block=(?<block>[0-9A-Fa-f]{8}) target=(?<target>[0-9A-Fa-f]{8}) calls_delta=(?<calls_delta>\d+) calls_total=(?<calls_total>\d+) body_ticks_delta=(?<body_delta>\d+) body_ticks_total=(?<body_total>\d+) ticks_per_call=(?<tpc>\d+)"
    Get-Content -LiteralPath $resolvedProfile | ForEach-Object {
        if ($_ -match $bodyPattern) {
            $guest = $Matches.guest.ToUpperInvariant()
            $total = [int64]$Matches.total
            if (!$profileRows.ContainsKey($guest) -or $total -ge [int64]$profileRows[$guest].body_ticks_total) {
                $profileRows[$guest] = [pscustomobject][ordered]@{
                    guest = $guest
                    block = [int]$Matches.block
                    body_ticks_total = $total
                    body_ticks_delta = [int64]$Matches.delta
                    entries_delta = [int64]$Matches.entries
                    ticks_per_entry = [int64]$Matches.tpe
                }
            }
            return
        }
        if ($_ -match $edgePattern) {
            $blockGuest = $Matches.block.ToUpperInvariant()
            if (!$edgeRows.ContainsKey($blockGuest)) {
                $edgeRows[$blockGuest] = New-Object System.Collections.Generic.List[object]
            }
            $edgeRows[$blockGuest].Add([pscustomobject][ordered]@{
                target = $Matches.target.ToUpperInvariant()
                calls_total = [int64]$Matches.calls_total
                body_ticks_total = [int64]$Matches.body_total
                ticks_per_call = [int64]$Matches.tpc
            }) | Out-Null
        }
    }
}

$endOrdinal = $items.Count
if (![string]::IsNullOrWhiteSpace($endUpper)) {
    if (!$ppcByAddress.ContainsKey($endUpper)) {
        throw "EndGuest $endUpper not found in $Phase dump for $functionUpper."
    }
    $endOrdinal = [int]$ppcByAddress[$endUpper].ordinal
} elseif ($profileRows.Count -gt 0) {
    foreach ($guest in $profileRows.Keys) {
        if (!$ppcByAddress.ContainsKey($guest)) {
            continue
        }
        $candidateOrdinal = [int]$ppcByAddress[$guest].ordinal
        if ($candidateOrdinal -gt $startOrdinal -and $candidateOrdinal -lt $endOrdinal) {
            $endOrdinal = $candidateOrdinal
            $endUpper = $guest
        }
    }
}
if ($endOrdinal -eq $items.Count) {
    $nextLabel = $items |
        Where-Object { $_.kind -eq "label" -and [int]$_.ordinal -gt $startOrdinal } |
        Sort-Object -Property ordinal |
        Select-Object -First 1
    if ($null -ne $nextLabel) {
        $endOrdinal = [int]$nextLabel.ordinal
    }
}

$slice = $items | Where-Object { [int]$_.ordinal -ge $startOrdinal -and [int]$_.ordinal -lt $endOrdinal }
$ppc = $slice | Where-Object { $_.kind -eq "ppc" }
$hir = $slice | Where-Object { $_.kind -eq "hir" }

$ppcOps = @{}
$hirOps = @{}
$loads = @{}
$stores = @{}
$stvewxPcs = @{}
$dynamicExtractPcs = @{}
$splatPcs = @{}
$mulAddPcs = @{}

$extract = 0
$extractDynamic = 0
$extractConstant = 0
$splat = 0
$mulAdd = 0
$load1 = 0
$loadOffset1 = 0
$store1 = 0
$contextBarrier = 0
$calls = 0
$branches = 0
$stvewx = 0
$stvewxDynamicExtract = 0
$stvewxStore1 = 0
$stvewxMaskedAddress = 0

foreach ($row in $ppc) {
    Add-Count $ppcOps $row.op
    if ($row.op -eq "stvewx") {
        $stvewx += 1
        Add-Count $stvewxPcs $row.address
    }
    if ($row.op -eq "vmaddfp") {
        Add-Count $mulAddPcs $row.address
    }
}

foreach ($row in $hir) {
    Add-Count $hirOps $row.op
    if ($row.text -match "\bload_context\s+\+(?<offset>\d+)") {
        $offset = [int]$Matches.offset
        Add-Count $loads (Get-ContextOffsetName $offset)
    }
    if ($row.text -match "\bstore_context\s+\+(?<offset>\d+)") {
        $offset = [int]$Matches.offset
        Add-Count $stores (Get-ContextOffsetName $offset)
    }
    if ($row.op -eq "extract") {
        $extract += 1
        if ($row.text -match "\bextract\s+[^,]+,\s+v[0-9]+\.(i8|i16|i32|i64)") {
            $extractDynamic += 1
            Add-Count $dynamicExtractPcs $row.ppc_address
            if ($row.ppc_op -eq "stvewx") {
                $stvewxDynamicExtract += 1
            }
        } else {
            $extractConstant += 1
        }
    }
    if ($row.op -eq "splat") {
        $splat += 1
        Add-Count $splatPcs $row.ppc_address
    }
    if ($row.op -eq "mul_add") { $mulAdd += 1 }
    if ($row.op -eq "load.1") { $load1 += 1 }
    if ($row.op -eq "load_offset.1") { $loadOffset1 += 1 }
    if ($row.op -eq "store.1") {
        $store1 += 1
        if ($row.ppc_op -eq "stvewx") {
            $stvewxStore1 += 1
        }
    }
    if ($row.op -eq "context_barrier") { $contextBarrier += 1 }
    if ($row.op -like "call*") { $calls += 1 }
    if ($row.op -like "branch*") { $branches += 1 }
    if ($row.ppc_op -eq "stvewx" -and $row.text -match "\band\s+[^,]+,\s+-4\b") {
        $stvewxMaskedAddress += 1
    }
}

$profile = $null
if ($profileRows.ContainsKey($startUpper)) {
    $profile = $profileRows[$startUpper]
}
$edgeBody = 0
$edgeCalls = 0
$edgeTargets = "-"
if ($edgeRows.ContainsKey($startUpper)) {
    $latestByTarget = @{}
    foreach ($edge in $edgeRows[$startUpper]) {
        if (!$latestByTarget.ContainsKey($edge.target) -or
            [int64]$edge.body_ticks_total -ge [int64]$latestByTarget[$edge.target].body_ticks_total) {
            $latestByTarget[$edge.target] = $edge
        }
    }
    $targetParts = New-Object System.Collections.Generic.List[string]
    foreach ($edge in $latestByTarget.Values) {
        $edgeBody += [int64]$edge.body_ticks_total
        $edgeCalls += [int64]$edge.calls_total
        $targetParts.Add(("{0}:{1}" -f $edge.target, $edge.body_ticks_total)) | Out-Null
    }
    $edgeTargets = $targetParts -join ","
}
$exclusive = 0
$exclusivePct = 0.0
if ($null -ne $profile) {
    $exclusive = [Math]::Max([int64]0, [int64]$profile.body_ticks_total - [int64]$edgeBody)
    if ([int64]$profile.body_ticks_total -gt 0) {
        $exclusivePct = [Math]::Round(($exclusive * 100.0) / [int64]$profile.body_ticks_total, 2)
    }
}

$loadContextCount = Get-Count $hirOps "load_context"
$storeContextCount = Get-Count $hirOps "store_context"
$loadVectorShl = Get-Count $hirOps "load_vector_shl"
$loadVectorShr = Get-Count $hirOps "load_vector_shr"
$permute = Get-Count $hirOps "permute"
$orCount = Get-Count $hirOps "or"
$andCount = Get-Count $hirOps "and"
$addCount = Get-Count $hirOps "add"
$truncateCount = Get-Count $hirOps "truncate"
$zeroExtendCount = Get-Count $hirOps "zero_extend"
$compareCount =
    (Get-Count $hirOps "compare_eq") +
    (Get-Count $hirOps "compare_ne") +
    (Get-Count $hirOps "compare_slt") +
    (Get-Count $hirOps "compare_sle") +
    (Get-Count $hirOps "compare_sgt") +
    (Get-Count $hirOps "compare_sge") +
    (Get-Count $hirOps "compare_ult") +
    (Get-Count $hirOps "compare_ule") +
    (Get-Count $hirOps "compare_ugt") +
    (Get-Count $hirOps "compare_uge")
$callCount = $calls
$branchCount = $branches

$knownScalarFloor =
    $extractDynamic * 11 +
    $extractConstant +
    $splat +
    $load1 * 2 +
    $loadOffset1 * 2 +
    $store1 * 2 +
    $loadContextCount +
    $storeContextCount

$estimateRows = New-Object System.Collections.Generic.List[object]
Add-EstimateRow $estimateRows "load_context" $loadContextCount 1 "a64_sequences.cc" "direct PPC context ldr floor; nonzero offsets may need a mov"
Add-EstimateRow $estimateRows "store_context" $storeContextCount 1 "a64_sequences.cc" "direct PPC context str floor; nonzero offsets may need a mov"
Add-EstimateRow $estimateRows "load.1" $load1 2 "a64_seq_memory.cc" "guest address materialization plus byte load floor; endian paths add work"
Add-EstimateRow $estimateRows "load_offset.1" $loadOffset1 2 "a64_seq_memory.cc" "guest offset load floor"
Add-EstimateRow $estimateRows "store.1" $store1 2 "a64_seq_memory.cc" "guest address materialization plus byte store floor"
Add-EstimateRow $estimateRows "load_vector_shl" $loadVectorShl 6 "a64_seq_vector.cc:LOAD_VECTOR_SHL_I8" "two mov, fmov, ins, dup/movi, add floor"
Add-EstimateRow $estimateRows "load_vector_shr" $loadVectorShr 6 "a64_seq_vector.cc:LOAD_VECTOR_SHR_I8" "two mov, fmov, ins, dup/movi, sub floor"
Add-EstimateRow $estimateRows "permute" $permute 8 "a64_seq_vector.cc:PERMUTE_V128" "TBL setup floor with possible src/control copies"
Add-EstimateRow $estimateRows "extract_dynamic_i32" $extractDynamic 11 "a64_seq_vector.cc:EXTRACT_I32" "TBL control build plus tbl and umov"
Add-EstimateRow $estimateRows "extract_constant_i32" $extractConstant 1 "a64_seq_vector.cc:EXTRACT_I32" "constant lane umov"
Add-EstimateRow $estimateRows "splat_i32" $splat 1 "a64_seq_vector.cc:SPLAT_I32" "variable lane dup floor"
Add-EstimateRow $estimateRows "mul_add_v128" $mulAdd 10 "a64_sequences.cc:MUL_ADD_V128" "no-denormal/no-NaN fast-path floor; slow paths add FPCR, flush, and NaN-fixup work"
Add-EstimateRow $estimateRows "or" $orCount 1 "a64_sequences.cc" "single NEON/scalar logical op floor"
Add-EstimateRow $estimateRows "and" $andCount 1 "a64_sequences.cc" "single logical op floor; immediate materialization may add work"
Add-EstimateRow $estimateRows "add" $addCount 1 "a64_sequences.cc" "single add floor; immediate materialization may add work"
Add-EstimateRow $estimateRows "truncate" $truncateCount 1 "a64_sequences.cc" "register move/mask floor"
Add-EstimateRow $estimateRows "zero_extend" $zeroExtendCount 1 "a64_sequences.cc" "register move/mask floor"
Add-EstimateRow $estimateRows "compare" $compareCount 1 "a64_sequences.cc" "cmp/fcmp floor; condition materialization may add work"
Add-EstimateRow $estimateRows "branch" $branchCount 1 "a64_seq_control.cc" "direct branch floor"
Add-EstimateRow $estimateRows "call" $callCount 2 "a64_seq_control.cc" "guest call/branch setup floor; callee body excluded separately when edge rows exist"

$estimatedFloorTotal = [int64]0
foreach ($row in $estimateRows) {
    $estimatedFloorTotal += [int64]$row.floor_total
}
$exclusiveTicksPerEstimatedInstr = 0.0
$bodyTicksPerEstimatedInstr = 0.0
if ($estimatedFloorTotal -gt 0) {
    if ($exclusive -gt 0) {
        $exclusiveTicksPerEstimatedInstr = [Math]::Round($exclusive / [double]$estimatedFloorTotal, 2)
    }
    if ($null -ne $profile -and [int64]$profile.body_ticks_total -gt 0) {
        $bodyTicksPerEstimatedInstr = [Math]::Round([int64]$profile.body_ticks_total / [double]$estimatedFloorTotal, 2)
    }
}

Write-Output "# HIR A64 Codegen Audit"
Write-Output ""
Write-Output ("log={0}" -f $resolvedLog)
if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    Write-Output ("block_profile_log={0}" -f (Resolve-Path -LiteralPath $BlockProfileLog).Path)
}
Write-Output ("function={0}" -f $functionUpper)
Write-Output ("phase={0}" -f $Phase)
$endLabel = $endUpper
if ([string]::IsNullOrWhiteSpace($endLabel)) {
    $endLabel = "next_label"
}
Write-Output ("slice={0}-{1}" -f $startUpper, $endLabel)
if ($null -ne $profile) {
    Write-Output ("body_ticks_total={0} body_ticks_delta={1} entries_delta={2} ticks_per_entry={3} edge_targets={4} edge_body_total={5} edge_calls_total={6} approx_exclusive={7} exclusive_pct={8}" -f $profile.body_ticks_total, $profile.body_ticks_delta, $profile.entries_delta, $profile.ticks_per_entry, $edgeTargets, $edgeBody, $edgeCalls, $exclusive, $exclusivePct)
}
Write-Output ("ppc_count={0} hir_count={1}" -f $ppc.Count, $hir.Count)
Write-Output ("ppc_ops={0}" -f (Get-TopPairs $ppcOps $Top))
Write-Output ("hir_ops={0}" -f (Get-TopPairs $hirOps $Top))
Write-Output ("context_loads={0}" -f (Get-TopPairs $loads $Top))
Write-Output ("context_stores={0}" -f (Get-TopPairs $stores $Top))
Write-Output ""
Write-Output "## Hot Lowering Shapes"
Write-Output ("extract={0} dynamic={1} constant={2} dynamic_pcs={3}" -f $extract, $extractDynamic, $extractConstant, (Get-TopPairs $dynamicExtractPcs $Top))
Write-Output ("splat={0} pcs={1}" -f $splat, (Get-TopPairs $splatPcs $Top))
Write-Output ("mul_add_v128={0} vmaddfp_pcs={1}" -f $mulAdd, (Get-TopPairs $mulAddPcs $Top))
Write-Output ("load1={0} load_offset1={1} store1={2}" -f $load1, $loadOffset1, $store1)
Write-Output ("stvewx={0} masked_address={1} dynamic_extract={2} store1={3} pcs={4}" -f $stvewx, $stvewxMaskedAddress, $stvewxDynamicExtract, $stvewxStore1, (Get-TopPairs $stvewxPcs $Top))
Write-Output ("context_barriers={0} calls={1} branches={2}" -f $contextBarrier, $calls, $branches)
Write-Output ("known_scalar_codegen_floor={0}" -f $knownScalarFloor)
if ($mulAdd -gt 0 -or $mulAddPcs.Count -gt 0) {
    $mulAddRows = $ppc | Where-Object { $_.op -eq "vmaddfp" }
    Write-Output ""
    Write-Output "## MUL_ADD_V128 Source-Cost Audit"
    Write-Output "source=src/xenia/cpu/backend/a64/a64_sequences.cc:MUL_ADD_V128"
    Write-Output "helper_source=src/xenia/cpu/backend/a64/a64_seq_util.h:EmitWithVmxFpcr,FlushDenormals_V128,PrepareVmxFpSources,FixupVmxNan_V128_Fma"
    Write-Output ("observed_vmaddfp_pcs={0}" -f (Get-TopPairs $mulAddPcs $Top))
    foreach ($row in ($mulAddRows | Select-Object -First $Top)) {
        Write-Output ("vmaddfp={0}" -f $row.text)
    }
    Write-Output "source_shape=EmitWithVmxFpcr; src3 copy/optional denormal flush; str q3; PrepareVmxFpSources for src1/src2; str q0/q1; ldr q2; fmla; FixupVmxNan_V128_Fma; optional output denormal flush; mov dest"
    Write-Output "likely_hot_cost=lazy FPCR switch if previous mode differs; three scratch Q stores; one scratch Q reload; one fmla; NaN fast-path test; result copy"
    Write-Output "slow_cost=software denormal flushing when kA64FZFlushesInputs is false; per-lane PPC NaN repair when any result lane is NaN; source copies when allocation needs scratch"
    Write-Output "semantics_gate=x64 MUL_ADD_V128 intentionally avoids host FMA because vfmadd differed from vmul+vadd tests; A64 currently uses fmla, so shortcuts need explicit semantics coverage"
    Write-Output "correctness_gate=do not replace with raw fmla unless a route audit proves src/result NaN and denormal cases are absent or an exact fallback is kept; PPC NaN precedence is src1,src2,src3,default"
    Write-Output "next_experiment=default-off function/span/PC-gated runtime audit counters for these vmaddfp PCs before any shortcut"
}
Write-Output ""
Write-Output "## Source-Reviewed A64 Floor Estimate"
Write-Output "estimate_kind=heuristic_source_floor not_exact_instruction_count"
Write-Output ("estimated_floor_total={0} body_ticks_per_estimated_instr={1} approx_exclusive_ticks_per_estimated_instr={2}" -f $estimatedFloorTotal, $bodyTicksPerEstimatedInstr, $exclusiveTicksPerEstimatedInstr)
foreach ($row in ($estimateRows | Sort-Object -Property @{ Expression = "floor_total"; Descending = $true }, @{ Expression = "shape"; Ascending = $true })) {
    Write-Output ("shape={0} count={1} floor_per_op={2} floor_total={3} source={4} note={5}" -f $row.shape, $row.count, $row.floor_per_op, $row.floor_total, $row.source, $row.note)
}
if ($contextBarrier -gt 0) {
    Write-Output ("context_barrier_note=count:{0} not_in_floor can force register/materialization churn around aliases and calls" -f $contextBarrier)
}
Write-Output ""
Write-Output "## A64 Lowering Anchors"
Write-Output "load_context/store_context: src/xenia/cpu/backend/a64/a64_sequences.cc emits direct ldr/str from the PPC context register; nonzero constants add a mov."
Write-Output "load.1/store.1: src/xenia/cpu/backend/a64/a64_seq_memory.cc computes a 32-bit guest address through x0 before ldr/str; byte-swap paths add rev."
Write-Output "extract dynamic i32: src/xenia/cpu/backend/a64/a64_seq_vector.cc builds a TBL control with scalar ops, then tbl+umov. This audit counts it as an 11-instruction scalar/vector floor."
Write-Output "splat i32: src/xenia/cpu/backend/a64/a64_seq_vector.cc variable i32 splat lowers to dup."
Write-Output "load_vector_shl/shr: src/xenia/cpu/backend/a64/a64_seq_vector.cc builds the byte-control vector with two movs, fmov, ins, dup/movi, and add/sub."
Write-Output "permute v128: src/xenia/cpu/backend/a64/a64_seq_vector.cc copies table/control vectors when needed, endian-adjusts control bytes, and emits tbl."
Write-Output "mul_add v128: src/xenia/cpu/backend/a64/a64_sequences.cc uses VMX FPCR mode, scratch stack saves, fmla, NaN fixup, and denormal handling when required; the floor counts the no-NaN/no-software-denormal fast path only."
Write-Output ""
Write-Output "## PPC Preview"
foreach ($row in ($ppc | Select-Object -First $Top)) {
    Write-Output ("{0}" -f $row.text)
}
