param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [Parameter(Mandatory = $true)]
    [string]$StartGuest,
    [string]$EndGuest = "",
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [string]$BlockProfileLog = "",
    [string]$EdgeProfileLog = "",
    [int]$Top = 16
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
        [string]$Args,
        [string]$Text,
        [string]$PpcAddress,
        [string]$PpcOp
    )
    return [pscustomobject][ordered]@{
        kind = $Kind
        ordinal = $Ordinal
        address = $Address
        op = $Op
        args = $Args
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
        $items.Add((New-ItemRow "label" $ordinal "" "" "" $text.Trim() $currentPpcAddress $currentPpcOp)) | Out-Null
        $ordinal += 1
        return
    }
    if ($text -match "^\s*;\s+(?<addr>[0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+(?<op>[A-Za-z0-9_\.]+)\s*(?<args>.*)$") {
        $currentPpcAddress = $Matches.addr.ToUpperInvariant()
        $currentPpcOp = $Matches.op.ToLowerInvariant()
        $args = $Matches.args.Trim()
        $item = New-ItemRow "ppc" $ordinal $currentPpcAddress $currentPpcOp $args $text.Trim() $currentPpcAddress $currentPpcOp
        $items.Add($item) | Out-Null
        if (!$ppcByAddress.ContainsKey($currentPpcAddress)) {
            $ppcByAddress[$currentPpcAddress] = $item
        }
        $ordinal += 1
        return
    }
    if ($text -match "^\s*;\s*(in:|out:|default|$)" -or [string]::IsNullOrWhiteSpace($text)) {
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
    $items.Add((New-ItemRow "hir" $ordinal "" $op "" $trimmed $currentPpcAddress $currentPpcOp)) | Out-Null
    $ordinal += 1
}

if (!$ppcByAddress.ContainsKey($startUpper)) {
    throw "StartGuest $startUpper not found in $Phase dump for $functionUpper."
}

$startOrdinal = [int]$ppcByAddress[$startUpper].ordinal
$endOrdinal = $items.Count
$sliceEndLabel = "eof"

if (![string]::IsNullOrWhiteSpace($endUpper)) {
    if (!$ppcByAddress.ContainsKey($endUpper)) {
        throw "EndGuest $endUpper not found in $Phase dump for $functionUpper."
    }
    $endPpcOrdinal = [int]$ppcByAddress[$endUpper].ordinal
    $nextPpc = $items |
        Where-Object { $_.kind -eq "ppc" -and [int]$_.ordinal -gt $endPpcOrdinal } |
        Sort-Object -Property ordinal |
        Select-Object -First 1
    if ($null -ne $nextPpc) {
        $endOrdinal = [int]$nextPpc.ordinal
    }
    $sliceEndLabel = $endUpper
} else {
    $nextLabel = $items |
        Where-Object { $_.kind -eq "label" -and [int]$_.ordinal -gt $startOrdinal } |
        Sort-Object -Property ordinal |
        Select-Object -First 1
    if ($null -ne $nextLabel) {
        $endOrdinal = [int]$nextLabel.ordinal
        if ($nextLabel.text -match "loc_(?<addr>[0-9A-Fa-f]{8})") {
            $sliceEndLabel = ("before_{0}" -f $Matches.addr.ToUpperInvariant())
        } else {
            $sliceEndLabel = "next_label"
        }
    }
}

$slice = $items | Where-Object { [int]$_.ordinal -ge $startOrdinal -and [int]$_.ordinal -lt $endOrdinal }
$ppc = $slice | Where-Object { $_.kind -eq "ppc" }
$hir = $slice | Where-Object { $_.kind -eq "hir" }

$profileRows = @{}
$edgeRows = @{}
$profileLogPaths = New-Object System.Collections.Generic.List[string]
if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    $profileLogPaths.Add($BlockProfileLog) | Out-Null
}
if (![string]::IsNullOrWhiteSpace($EdgeProfileLog)) {
    $profileLogPaths.Add($EdgeProfileLog) | Out-Null
}
foreach ($profileLogPath in $profileLogPaths) {
    if (!(Test-Path -LiteralPath $profileLogPath)) {
        throw "Profile log not found: $profileLogPath"
    }
    $resolvedProfile = (Resolve-Path -LiteralPath $profileLogPath).Path
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

$ppcOps = @{}
$hirOps = @{}
$contextLoads = @{}
$contextStores = @{}
$contextLoadPcs = @{}
$contextStorePcs = @{}
$ppcVectorOps = @{}
$ppcVectorPcs = @{}
$extractPcs = @{}
$splatPcs = @{}
$mulAddPcs = @{}
$loadVectorPcs = @{}
$storeVectorPcs = @{}
$cr6StorePcs = @{}
$crComparePcs = @{}
$directCalls = @{}

$extract = 0
$extractConstant = 0
$extractDynamic = 0
$splat = 0
$mulAdd = 0
$loadVectorShl = 0
$loadVectorShr = 0
$permute = 0
$permute2 = 0
$load1 = 0
$loadOffset1 = 0
$store1 = 0
$storeOffset1 = 0
$contextBarrier = 0
$hirBranches = 0
$hirCalls = 0
$compareOps = 0
$crStores = 0
$cr6Stores = 0
$fpscrLoads = 0
$fpscrStores = 0

$vectorPpcSet = @{
    "lvlx" = $true
    "lvrx" = $true
    "lvx" = $true
    "lvx128" = $true
    "stvx" = $true
    "stvlx" = $true
    "stvrx" = $true
    "vor" = $true
    "vspltw" = $true
    "vmaddfp" = $true
}

foreach ($row in $ppc) {
    Add-Count $ppcOps $row.op
    if ($vectorPpcSet.ContainsKey($row.op)) {
        Add-Count $ppcVectorOps $row.op
        Add-Count $ppcVectorPcs ("{0}:{1}" -f $row.address, $row.op)
    }
    if ($row.op -eq "bl" -and $row.text -match "0x(?<target>[0-9A-Fa-f]+)") {
        Add-Count $directCalls ("{0}->{1}" -f $row.address, $Matches.target.ToUpperInvariant())
    }
}

foreach ($row in $hir) {
    Add-Count $hirOps $row.op

    if ($row.text -match "\bload_context\s+\+(?<offset>\d+)") {
        $offset = [int]$Matches.offset
        $name = Get-ContextOffsetName $offset
        Add-Count $contextLoads $name
        Add-Count $contextLoadPcs ("{0}:{1}" -f $row.ppc_address, $name)
        if ($offset -eq 2628) { $fpscrLoads += 1 }
    }
    if ($row.text -match "\bstore_context\s+\+(?<offset>\d+)") {
        $offset = [int]$Matches.offset
        $name = Get-ContextOffsetName $offset
        Add-Count $contextStores $name
        Add-Count $contextStorePcs ("{0}:{1}" -f $row.ppc_address, $name)
        if ($offset -ge 2596 -and $offset -lt 2628) {
            $crStores += 1
            if ($offset -ge 2620 -and $offset -lt 2624) {
                $cr6Stores += 1
                Add-Count $cr6StorePcs ("{0}:{1}" -f $row.ppc_address, $name)
            }
        }
        if ($offset -eq 2628) { $fpscrStores += 1 }
    }

    if ($row.op -eq "extract") {
        $extract += 1
        Add-Count $extractPcs $row.ppc_address
        if ($row.text -match "\bextract\s+[^,]+,\s+v[0-9]+\.") {
            $extractDynamic += 1
        } else {
            $extractConstant += 1
        }
    } elseif ($row.op -eq "splat") {
        $splat += 1
        Add-Count $splatPcs $row.ppc_address
    } elseif ($row.op -eq "mul_add") {
        $mulAdd += 1
        Add-Count $mulAddPcs $row.ppc_address
    } elseif ($row.op -eq "load_vector_shl") {
        $loadVectorShl += 1
        Add-Count $loadVectorPcs ("{0}:shl" -f $row.ppc_address)
    } elseif ($row.op -eq "load_vector_shr") {
        $loadVectorShr += 1
        Add-Count $loadVectorPcs ("{0}:shr" -f $row.ppc_address)
    } elseif ($row.op -eq "permute") {
        $permute += 1
    } elseif ($row.op -eq "permute.2") {
        $permute2 += 1
    } elseif ($row.op -eq "load.1") {
        $load1 += 1
    } elseif ($row.op -eq "load_offset.1") {
        $loadOffset1 += 1
    } elseif ($row.op -eq "store.1") {
        $store1 += 1
        Add-Count $storeVectorPcs $row.ppc_address
    } elseif ($row.op -eq "store_offset.1") {
        $storeOffset1 += 1
    } elseif ($row.op -eq "context_barrier") {
        $contextBarrier += 1
    } elseif ($row.op -like "branch*") {
        $hirBranches += 1
    } elseif ($row.op -like "call*") {
        $hirCalls += 1
    } elseif ($row.op -like "compare*") {
        $compareOps += 1
        Add-Count $crComparePcs $row.ppc_address
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
        $targetParts.Add(("{0}:{1}/calls={2}/tpc={3}" -f $edge.target, $edge.body_ticks_total, $edge.calls_total, $edge.ticks_per_call)) | Out-Null
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

$vectorLoadStoreScore = $loadVectorShl + $loadVectorShr + $permute + $permute2
$contextTrafficScore = 0
if ($hirOps.ContainsKey("load_context")) { $contextTrafficScore += [int]$hirOps["load_context"] }
if ($hirOps.ContainsKey("store_context")) { $contextTrafficScore += [int]$hirOps["store_context"] }

Write-Output "# HIR Span Reduction Audit"
Write-Output ""
Write-Output ("log={0}" -f $resolvedLog)
if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    Write-Output ("block_profile_log={0}" -f (Resolve-Path -LiteralPath $BlockProfileLog).Path)
}
if (![string]::IsNullOrWhiteSpace($EdgeProfileLog)) {
    Write-Output ("edge_profile_log={0}" -f (Resolve-Path -LiteralPath $EdgeProfileLog).Path)
}
Write-Output ("function={0}" -f $functionUpper)
Write-Output ("phase={0}" -f $Phase)
Write-Output ("span={0}-{1}" -f $startUpper, $sliceEndLabel)
if ($null -ne $profile) {
    Write-Output ("body_ticks_total={0} body_ticks_delta={1} entries_delta={2} ticks_per_entry={3} edge_targets={4} edge_body_total={5} edge_calls_total={6} approx_exclusive={7} exclusive_pct={8}" -f $profile.body_ticks_total, $profile.body_ticks_delta, $profile.entries_delta, $profile.ticks_per_entry, $edgeTargets, $edgeBody, $edgeCalls, $exclusive, $exclusivePct)
}
Write-Output ("ppc_count={0} hir_count={1}" -f $ppc.Count, $hir.Count)
Write-Output ("direct_calls={0}" -f (Get-TopPairs $directCalls $Top))
Write-Output ("ppc_ops={0}" -f (Get-TopPairs $ppcOps $Top))
Write-Output ("ppc_vector_ops={0}" -f (Get-TopPairs $ppcVectorOps $Top))
Write-Output ("hir_ops={0}" -f (Get-TopPairs $hirOps $Top))
Write-Output ("context_loads={0}" -f (Get-TopPairs $contextLoads $Top))
Write-Output ("context_stores={0}" -f (Get-TopPairs $contextStores $Top))
Write-Output ("context_load_pcs={0}" -f (Get-TopPairs $contextLoadPcs $Top))
Write-Output ("context_store_pcs={0}" -f (Get-TopPairs $contextStorePcs $Top))
Write-Output ""
Write-Output "## Reduction Lanes"
Write-Output ("cr6_branch_chain: compares={0} cr_store_total={1} cr6_store_total={2} context_barriers={3} hir_branches={4} cr6_store_pcs={5}" -f $compareOps, $crStores, $cr6Stores, $contextBarrier, $hirBranches, (Get-TopPairs $cr6StorePcs $Top))
Write-Output "cr6_branch_chain_decision=reject_for_now reason=previous CR compare/barrier fusion and CR-store elision crashed or regressed this title; use only as audit evidence, not the next patch."
Write-Output ("vector_join_store: ppc_vector_ops={0} load_vector_shl={1} load_vector_shr={2} permute={3} permute2={4} load_vector_pcs={5}" -f (Get-TopPairs $ppcVectorOps $Top), $loadVectorShl, $loadVectorShr, $permute, $permute2, (Get-TopPairs $loadVectorPcs $Top))
Write-Output ("constant_extract_splat_vmadd: extract={0} constant={1} dynamic={2} splat={3} mul_add={4} extract_pcs={5} splat_pcs={6} mul_add_pcs={7}" -f $extract, $extractConstant, $extractDynamic, $splat, $mulAdd, (Get-TopPairs $extractPcs $Top), (Get-TopPairs $splatPcs $Top), (Get-TopPairs $mulAddPcs $Top))
Write-Output ("context_state_traffic: load_context_plus_store_context={0} fpscr_loads={1} fpscr_stores={2} load_context_pcs={3} store_context_pcs={4}" -f $contextTrafficScore, $fpscrLoads, $fpscrStores, (Get-TopPairs $contextLoadPcs $Top), (Get-TopPairs $contextStorePcs $Top))
Write-Output ("scalar_memory_floor: load1={0} load_offset1={1} store1={2} store_offset1={3}" -f $load1, $loadOffset1, $store1, $storeOffset1)
Write-Output ""
Write-Output "## Recommendation"
if ($null -ne $profile -and $edgeBody -gt 0 -and $exclusivePct -lt 15.0) {
    Write-Output "next_lane=callee_edge_first"
    Write-Output ("reason=dynamic call-edge rows account for nearly all of this block body ({0}% exclusive remains), so parent-side state/vector reductions are not body-proven yet; focus the dominant callee edge before a local codegen toggle." -f $exclusivePct)
} elseif ($vectorLoadStoreScore -ge 20 -and $cr6Stores -ge 12) {
    Write-Output "next_lane=vector_join_store_provenance_audit"
    Write-Output "reason=the span has a large body-backed local cost, but the largest obvious scalar lane is CR6 and is a known rejected path; the safer next move is proving lvlx/lvrx/vor/stvx provenance before a function/span-gated vector superpath."
} elseif ($contextTrafficScore -ge 80) {
    Write-Output "next_lane=state_traffic_micro_audit"
    Write-Output "reason=context traffic is high enough to justify a pre-patch alias/lifetime audit, but broad GPR caches and r1 live-in were not speed-proven, so keep any experiment function/span-gated and default-off."
} else {
    Write-Output "next_lane=no_patch_yet"
    Write-Output "reason=this span does not expose a large enough safe single lowering shape for a codegen toggle without more provenance."
}
Write-Output "guardrails=do_not_revive_cr_fusion_or_cr_store_elision; do_not_repeat_exact_stvewx_lane_fold_ab; do_not_enable_broad_vmx_dot_or_broad_gpr_cache"
Write-Output ""
Write-Output "## PPC Preview"
foreach ($row in ($ppc | Select-Object -First $Top)) {
    Write-Output ("{0}" -f $row.text)
}
