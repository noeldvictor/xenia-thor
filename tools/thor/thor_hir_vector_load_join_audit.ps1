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
    [int]$LookaheadPpc = 12,
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
        [string]$ArgText,
        [string]$Text,
        [string]$PpcAddress,
        [string]$PpcOp
    )
    return [pscustomobject][ordered]@{
        kind = $Kind
        ordinal = $Ordinal
        address = $Address
        op = $Op
        args = $ArgText
        text = $Text
        ppc_address = $PpcAddress
        ppc_op = $PpcOp
    }
}

function New-Expr {
    param(
        [string]$Base = "",
        [int64]$Const = 0,
        [string]$Opaque = ""
    )
    return [pscustomobject][ordered]@{
        base = $Base
        const = $Const
        opaque = $Opaque
    }
}

function New-ConstExpr {
    param([int64]$Value)
    return New-Expr -Base "" -Const $Value
}

function New-BaseExpr {
    param([string]$Base, [int64]$Const = 0)
    return New-Expr -Base $Base -Const $Const
}

function New-OpaqueExpr {
    param([string]$Text)
    return New-Expr -Opaque $Text
}

function Format-Const {
    param([int64]$Value)
    if ($Value -lt 0) {
        return ("-0x{0:X}" -f (-1 * $Value))
    }
    return ("0x{0:X}" -f $Value)
}

function Format-Expr {
    param($Expr)
    if ($null -eq $Expr) {
        return "unknown"
    }
    if (![string]::IsNullOrWhiteSpace($Expr.opaque)) {
        return $Expr.opaque
    }
    if ([string]::IsNullOrWhiteSpace($Expr.base)) {
        return (Format-Const $Expr.const)
    }
    if ($Expr.const -eq 0) {
        return $Expr.base
    }
    if ($Expr.const -gt 0) {
        return ("{0}+{1}" -f $Expr.base, (Format-Const $Expr.const))
    }
    return ("{0}-{1}" -f $Expr.base, (Format-Const (-1 * $Expr.const)))
}

function Add-Expr {
    param($A, $B)
    if ($null -eq $A) { return $B }
    if ($null -eq $B) { return $A }
    if (![string]::IsNullOrWhiteSpace($A.opaque) -or ![string]::IsNullOrWhiteSpace($B.opaque)) {
        return (New-OpaqueExpr ("({0}+{1})" -f (Format-Expr $A), (Format-Expr $B)))
    }
    if ([string]::IsNullOrWhiteSpace($A.base)) {
        return (New-Expr -Base $B.base -Const ($A.const + $B.const))
    }
    if ([string]::IsNullOrWhiteSpace($B.base)) {
        return (New-Expr -Base $A.base -Const ($A.const + $B.const))
    }
    if ($A.base -eq $B.base) {
        return (New-OpaqueExpr ("(2*{0}+{1})" -f $A.base, (Format-Const ($A.const + $B.const))))
    }
    return (New-OpaqueExpr ("({0}+{1})" -f (Format-Expr $A), (Format-Expr $B)))
}

function Parse-Immediate {
    param([string]$Text)
    $t = $Text.Trim()
    $negative = $false
    if ($t.StartsWith("-")) {
        $negative = $true
        $t = $t.Substring(1)
    }
    $value = 0L
    if ($t -match "^0x[0-9A-Fa-f]+$") {
        $value = [Convert]::ToInt64($t.Substring(2), 16)
    } elseif ($t -match "^[0-9]+$") {
        $value = [Convert]::ToInt64($t, 10)
    } elseif ($t -match "^[0-9A-Fa-f]+$") {
        $value = [Convert]::ToInt64($t, 16)
    } else {
        throw "Cannot parse immediate: $Text"
    }
    if ($negative) {
        return (-1 * $value)
    }
    return $value
}

function Split-PpcArgs {
    param([string]$Text)
    return @($Text -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" })
}

function Get-RegExpr {
    param(
        [hashtable]$Regs,
        [string]$Reg,
        [bool]$ZeroMeansZero = $false
    )
    $r = $Reg.Trim().ToLowerInvariant()
    if ($ZeroMeansZero -and $r -eq "r0") {
        return (New-ConstExpr 0)
    }
    if ($Regs.ContainsKey($r)) {
        return $Regs[$r]
    }
    return (New-BaseExpr $r)
}

function Set-RegExpr {
    param([hashtable]$Regs, [string]$Reg, $Expr)
    $Regs[$Reg.Trim().ToLowerInvariant()] = $Expr
}

function Get-HirFacts {
    param($Rows)

    $ops = @{}
    $stores = New-Object System.Collections.Generic.List[string]
    $loads = New-Object System.Collections.Generic.List[string]
    $zeroStore = $false
    foreach ($row in $Rows) {
        Add-Count $ops $row.op
        if ($row.text -match "load_context \+(?<offset>\d+)") {
            $offset = [int]$Matches.offset
            $loads.Add((Get-ContextOffsetName $offset)) | Out-Null
        }
        if ($row.text -match "store_context \+(?<offset>\d+),\s*(?<value>.*)$") {
            $offset = [int]$Matches.offset
            $field = Get-ContextOffsetName $offset
            $stores.Add($field) | Out-Null
            if ($Matches.value -match "^\(0\.000000,0\.000000,0\.000000,0\.000000\)$") {
                $zeroStore = $true
            }
        }
    }

    return [pscustomobject][ordered]@{
        op_counts = $ops
        load_vector_shl = [int]($ops["load_vector_shl"])
        load_vector_shr = [int]($ops["load_vector_shr"])
        load1 = [int]($ops["load.1"])
        load_offset1 = [int]($ops["load_offset.1"])
        permute = [int]($ops["permute"])
        branch_true = [int]($ops["branch_true"])
        context_barrier = [int]($ops["context_barrier"])
        stores = $stores
        loads = $loads
        zero_store = $zeroStore
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
        $argText = $Matches.args.Trim()
        $item = New-ItemRow "ppc" $ordinal $currentPpcAddress $currentPpcOp $argText $text.Trim() $currentPpcAddress $currentPpcOp
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

$slice = @($items | Where-Object { [int]$_.ordinal -ge $startOrdinal -and [int]$_.ordinal -lt $endOrdinal })
$slicePpc = @($slice | Where-Object { $_.kind -eq "ppc" })
$sliceHir = @($slice | Where-Object { $_.kind -eq "hir" })

$lookaheadEnd = $endOrdinal
if ($LookaheadPpc -gt 0) {
    $nextPpcs = @($items |
        Where-Object { $_.kind -eq "ppc" -and [int]$_.ordinal -ge $endOrdinal } |
        Sort-Object -Property ordinal |
        Select-Object -First $LookaheadPpc)
    if ($nextPpcs.Count -gt 0) {
        $lastLookaheadPpc = $nextPpcs[$nextPpcs.Count - 1]
        $afterLast = $items |
            Where-Object { $_.kind -eq "ppc" -and [int]$_.ordinal -gt [int]$lastLookaheadPpc.ordinal } |
            Sort-Object -Property ordinal |
            Select-Object -First 1
        if ($null -ne $afterLast) {
            $lookaheadEnd = [int]$afterLast.ordinal
        } else {
            $lookaheadEnd = $items.Count
        }
    }
}
$lookaheadSlice = @($items | Where-Object { [int]$_.ordinal -ge $startOrdinal -and [int]$_.ordinal -lt $lookaheadEnd })

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
                $edgeRows[$blockGuest] = @{}
            }
            $target = $Matches.target.ToUpperInvariant()
            $total = [int64]$Matches.body_total
            if (!$edgeRows[$blockGuest].ContainsKey($target) -or $total -ge [int64]$edgeRows[$blockGuest][$target].body_ticks_total) {
                $edgeRows[$blockGuest][$target] = [pscustomobject][ordered]@{
                    target = $target
                    calls_total = [int64]$Matches.calls_total
                    body_ticks_total = $total
                    ticks_per_call = [int64]$Matches.tpc
                }
            }
        }
    }
}

$regs = @{}
Set-RegExpr $regs "r0" (New-BaseExpr "r0")
Set-RegExpr $regs "r1" (New-BaseExpr "r1")
foreach ($i in 2..31) {
    Set-RegExpr $regs ("r{0}" -f $i) (New-BaseExpr ("r{0}" -f $i))
}

$vectorLoads = New-Object System.Collections.Generic.List[object]
$vectorJoins = New-Object System.Collections.Generic.List[object]
$ppcTrace = New-Object System.Collections.Generic.List[object]

foreach ($row in ($lookaheadSlice | Where-Object { $_.kind -eq "ppc" } | Sort-Object -Property ordinal)) {
    $ppcArgs = @(Split-PpcArgs -Text $row.args)
    $inSlice = ([int]$row.ordinal -ge $startOrdinal -and [int]$row.ordinal -lt $endOrdinal)

    switch ($row.op) {
        "addi" {
            if ($ppcArgs.Count -ge 3) {
                $rd = $ppcArgs[0].ToLowerInvariant()
                $ra = $ppcArgs[1].ToLowerInvariant()
                $imm = Parse-Immediate $ppcArgs[2]
                $expr = Add-Expr (Get-RegExpr $regs $ra $true) (New-ConstExpr $imm)
                Set-RegExpr $regs $rd $expr
                $ppcTrace.Add([pscustomobject][ordered]@{
                    pc = $row.address
                    op = $row.op
                    effect = ("{0}={1}" -f $rd, (Format-Expr $expr))
                    in_slice = $inSlice
                }) | Out-Null
            }
        }
        "add" {
            if ($ppcArgs.Count -ge 3) {
                $rd = $ppcArgs[0].ToLowerInvariant()
                $expr = Add-Expr (Get-RegExpr $regs $ppcArgs[1] $false) (Get-RegExpr $regs $ppcArgs[2] $false)
                Set-RegExpr $regs $rd $expr
                $ppcTrace.Add([pscustomobject][ordered]@{
                    pc = $row.address
                    op = $row.op
                    effect = ("{0}={1}" -f $rd, (Format-Expr $expr))
                    in_slice = $inSlice
                }) | Out-Null
            }
        }
        "lwz" {
            if ($ppcArgs.Count -ge 2 -and $ppcArgs[1] -match "^(?<imm>[-]?(0x)?[0-9A-Fa-f]+)\((?<base>r\d+)\)$") {
                $rd = $ppcArgs[0].ToLowerInvariant()
                $imm = Parse-Immediate $Matches.imm
                $baseExpr = Get-RegExpr $regs $Matches.base $true
                $addrExpr = Add-Expr $baseExpr (New-ConstExpr $imm)
                $expr = New-OpaqueExpr ("mem32[{0}]" -f (Format-Expr $addrExpr))
                Set-RegExpr $regs $rd $expr
                $ppcTrace.Add([pscustomobject][ordered]@{
                    pc = $row.address
                    op = $row.op
                    effect = ("{0}={1}" -f $rd, (Format-Expr $expr))
                    in_slice = $inSlice
                }) | Out-Null
            }
        }
        "rlwinm" {
            if ($ppcArgs.Count -ge 5) {
                $rd = $ppcArgs[0].ToLowerInvariant()
                $src = Format-Expr (Get-RegExpr $regs $ppcArgs[1] $false)
                $expr = New-OpaqueExpr ("rlwinm({0},{1},{2},{3})" -f $src, $ppcArgs[2], $ppcArgs[3], $ppcArgs[4])
                Set-RegExpr $regs $rd $expr
                $ppcTrace.Add([pscustomobject][ordered]@{
                    pc = $row.address
                    op = $row.op
                    effect = ("{0}={1}" -f $rd, (Format-Expr $expr))
                    in_slice = $inSlice
                }) | Out-Null
            }
        }
        "bl" {
            foreach ($i in 3..12) {
                Set-RegExpr $regs ("r{0}" -f $i) (New-OpaqueExpr ("unknown_after_call:{0}" -f $row.address))
            }
            $ppcTrace.Add([pscustomobject][ordered]@{
                pc = $row.address
                op = $row.op
                effect = ("call {0}; clobber r3-r12" -f (($ppcArgs | Select-Object -First 1) -join ""))
                in_slice = $inSlice
            }) | Out-Null
        }
        { $_ -eq "lvlx" -or $_ -eq "lvrx" } {
            if ($ppcArgs.Count -ge 3) {
                $vd = $ppcArgs[0].ToLowerInvariant()
                $ra = $ppcArgs[1].ToLowerInvariant()
                $rb = $ppcArgs[2].ToLowerInvariant()
                $ea = Add-Expr (Get-RegExpr $regs $ra $true) (Get-RegExpr $regs $rb $false)
                $hirRows = @($items | Where-Object { $_.kind -eq "hir" -and $_.ppc_address -eq $row.address })
                $facts = Get-HirFacts $hirRows
                $vectorLoads.Add([pscustomobject][ordered]@{
                    pc = $row.address
                    op = $row.op
                    vd = $vd
                    ra = $ra
                    rb = $rb
                    ea_expr = Format-Expr $ea
                    aligned_expr = ("({0})&~0xF" -f (Format-Expr $ea))
                    shift_expr = ("({0})&0xF" -f (Format-Expr $ea))
                    load_vector_shl = $facts.load_vector_shl
                    load_vector_shr = $facts.load_vector_shr
                    load1 = $facts.load1
                    permute = $facts.permute
                    branch_true = $facts.branch_true
                    zero_path = $facts.zero_store
                    stores = (($facts.stores | Sort-Object -Unique) -join ",")
                    in_slice = $inSlice
                }) | Out-Null
            }
        }
        { $_ -eq "vor" -or $_ -eq "or" } {
            if ($ppcArgs.Count -ge 3 -and $ppcArgs[0] -match "^vr\d+$") {
                $vectorJoins.Add([pscustomobject][ordered]@{
                    pc = $row.address
                    op = $row.op
                    vd = $ppcArgs[0].ToLowerInvariant()
                    va = $ppcArgs[1].ToLowerInvariant()
                    vb = $ppcArgs[2].ToLowerInvariant()
                    in_slice = $inSlice
                }) | Out-Null
            }
        }
    }
}

$hirOps = @{}
$loads = @{}
$stores = @{}
foreach ($row in $sliceHir) {
    Add-Count $hirOps $row.op
    if ($row.text -match "load_context \+(?<offset>\d+)") {
        Add-Count $loads (Get-ContextOffsetName ([int]$Matches.offset))
    }
    if ($row.text -match "store_context \+(?<offset>\d+)") {
        Add-Count $stores (Get-ContextOffsetName ([int]$Matches.offset))
    }
}

$profile = $null
$edgeTotal = 0L
$edgeText = "-"
if ($profileRows.ContainsKey($startUpper)) {
    $profile = $profileRows[$startUpper]
    if ($edgeRows.ContainsKey($startUpper)) {
        $edges = @($edgeRows[$startUpper].Values | Sort-Object -Property body_ticks_total -Descending)
        $edgeTotal = [int64](($edges | Measure-Object -Property body_ticks_total -Sum).Sum)
        $edgeText = (($edges | Select-Object -First $Top | ForEach-Object {
            "{0}:{1}/calls={2}/tpc={3}" -f $_.target, $_.body_ticks_total, $_.calls_total, $_.ticks_per_call
        }) -join ",")
    }
}

$selfContainedJoin = $false
$crossSpanJoin = $false
$joinReason = "no vector join found in slice or lookahead"
if ($vectorLoads.Count -gt 0 -and $vectorJoins.Count -gt 0) {
    $loadedRegs = @($vectorLoads | ForEach-Object { $_.vd })
    foreach ($join in $vectorJoins) {
        if ($loadedRegs -contains $join.va -or $loadedRegs -contains $join.vb) {
            if ($join.in_slice) {
                $selfContainedJoin = $true
                $joinReason = ("join pc {0} is inside requested span" -f $join.pc)
            } else {
                $crossSpanJoin = $true
                $joinReason = ("join pc {0} is outside requested span" -f $join.pc)
            }
            break
        }
    }
}

Write-Output "# HIR Vector Load Join Provenance Audit"
Write-Output ""
Write-Output ('- function: `{0}`' -f $functionUpper)
Write-Output ('- phase: `{0}`' -f $Phase)
Write-Output ('- span: `{0}-{1}`' -f $startUpper, $sliceEndLabel)
Write-Output ('- ppc_rows: `{0}`' -f $slicePpc.Count)
Write-Output ('- hir_rows: `{0}`' -f $sliceHir.Count)
if ($null -ne $profile) {
    $exclusive = [int64]$profile.body_ticks_total - $edgeTotal
    if ($exclusive -lt 0) { $exclusive = 0 }
    Write-Output ('- block_body: `guest={0} body_ticks_total={1} entries_delta={2} ticks_per_entry={3}`' -f $profile.guest, $profile.body_ticks_total, $profile.entries_delta, $profile.ticks_per_entry)
    Write-Output ('- child_edges: `{0}`' -f $edgeText)
    Write-Output ('- approx_exclusive_after_edges: `{0}`' -f $exclusive)
}
Write-Output ('- hir_ops_top: `{0}`' -f (Get-TopPairs $hirOps $Top))
Write-Output ('- context_loads_top: `{0}`' -f (Get-TopPairs $loads $Top))
Write-Output ('- context_stores_top: `{0}`' -f (Get-TopPairs $stores $Top))
Write-Output ""
Write-Output "## PPC Provenance Trace"
Write-Output ""
foreach ($entry in $ppcTrace) {
    $scope = "lookahead"
    if ($entry.in_slice) { $scope = "span" }
    Write-Output ('- `{0}` `{1}` {2}: `{3}`' -f $entry.pc, $entry.op, $scope, $entry.effect)
}
Write-Output ""
Write-Output "## Vector Load Rows"
Write-Output ""
if ($vectorLoads.Count -eq 0) {
    Write-Output "- none"
} else {
    foreach ($load in $vectorLoads) {
        $scope = "lookahead"
        if ($load.in_slice) { $scope = "span" }
        Write-Output ('- `{0}` `{1}` {2}: `vd={3} ra={4} rb={5} ea={6} aligned={7} shift={8} load_vector_shl={9} load_vector_shr={10} load1={11} permute={12} branch_true={13} zero_path={14} stores={15}`' -f $load.pc, $load.op, $scope, $load.vd, $load.ra, $load.rb, $load.ea_expr, $load.aligned_expr, $load.shift_expr, $load.load_vector_shl, $load.load_vector_shr, $load.load1, $load.permute, $load.branch_true, $load.zero_path, $load.stores)
    }
}
Write-Output ""
Write-Output "## Vector Join Rows"
Write-Output ""
if ($vectorJoins.Count -eq 0) {
    Write-Output "- none"
} else {
    foreach ($join in $vectorJoins) {
        $scope = "lookahead"
        if ($join.in_slice) { $scope = "span" }
        Write-Output ('- `{0}` `{1}` {2}: `vd={3} va={4} vb={5}`' -f $join.pc, $join.op, $scope, $join.vd, $join.va, $join.vb)
    }
}
Write-Output ""
Write-Output "## Decision"
Write-Output ""
Write-Output ('- join_self_contained: `{0}`' -f $selfContainedJoin.ToString().ToLowerInvariant())
Write-Output ('- cross_span_join_seen: `{0}`' -f $crossSpanJoin.ToString().ToLowerInvariant())
Write-Output ('- reason: `{0}`' -f $joinReason)
if ($selfContainedJoin) {
    Write-Output '- recommendation: `eligible_for_default_off_function_span_gated_codegen_probe_after_source_review`'
} elseif ($crossSpanJoin) {
    Write-Output '- recommendation: `no_local_span_patch_yet; audit the cross-span join/user before codegen`'
} else {
    Write-Output '- recommendation: `no_codegen_patch_yet; provenance incomplete`'
}
