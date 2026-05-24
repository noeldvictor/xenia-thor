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
    [string]$ProvenanceStartGuest = ""
)

$ErrorActionPreference = "Stop"

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

function Convert-HirConst {
    param([string]$Token)
    $t = $Token.Trim()
    if ($t -match "^-([0-9A-Fa-f]+)$") {
        return -[Convert]::ToInt64($Matches[1], 16)
    }
    if ($t -match "^(0x)?([0-9A-Fa-f]+)$") {
        return [Convert]::ToInt64($Matches[2], 16)
    }
    return $null
}

function Format-Offset {
    param([int64]$Offset)
    if ($Offset -eq 0) {
        return ""
    }
    if ($Offset -lt 0) {
        return ("-0x{0:X}" -f (-$Offset))
    }
    return ("+0x{0:X}" -f $Offset)
}

function New-Expr {
    param(
        [string]$Kind,
        [string]$Desc,
        [string]$Base = "",
        [Nullable[int64]]$Offset = $null,
        [Nullable[int64]]$LowNibble = $null,
        [Nullable[int]]$Lane = $null,
        [string]$Proof = "",
        [string[]]$Chain = @()
    )
    return [pscustomobject][ordered]@{
        kind = $Kind
        desc = $Desc
        base = $Base
        offset = $Offset
        low_nibble = $LowNibble
        lane = $Lane
        proof = $Proof
        chain = $Chain
    }
}

function New-UnknownExpr {
    param([string]$Desc, [string[]]$Chain = @())
    return New-Expr -Kind "unknown" -Desc $Desc -Proof "unknown" -Chain $Chain
}

function Copy-ExprWithChain {
    param(
        [object]$Expr,
        [string]$Text
    )
    if ($null -eq $Expr) {
        return New-UnknownExpr -Desc "unknown" -Chain @($Text)
    }
    $chain = @()
    if ($Expr.chain) { $chain += $Expr.chain }
    $chain += $Text
    return New-Expr -Kind $Expr.kind -Desc $Expr.desc -Base $Expr.base `
        -Offset $Expr.offset -LowNibble $Expr.low_nibble -Lane $Expr.lane `
        -Proof $Expr.proof -Chain $chain
}

function Resolve-ValueExpr {
    param(
        [hashtable]$Exprs,
        [string]$Value
    )
    $key = ""
    if ($Value -match "(v\d+)\.") {
        $key = $Matches[1]
    } elseif ($Value -match "^(v\d+)$") {
        $key = $Matches[1]
    }
    if (![string]::IsNullOrWhiteSpace($key) -and $Exprs.ContainsKey($key)) {
        return $Exprs[$key]
    }
    $constant = Convert-HirConst $Value
    if ($null -ne $constant) {
        return New-Expr -Kind "const" -Desc ("0x{0:X}" -f $constant) -Offset $constant -Proof "constant"
    }
    return New-UnknownExpr -Desc $Value
}

function Get-FirstValueToken {
    param([string]$Text)
    if ($Text -match "(v\d+\.[A-Za-z0-9_<>]+)") {
        return $Matches[1]
    }
    return ""
}

function New-Record {
    param([string]$PpcAddress, [string]$PpcText)
    return [pscustomobject][ordered]@{
        ppc_address = $PpcAddress
        ppc_text = $PpcText
        masked_address_value = ""
        masked_address_expr = $null
        index_value = ""
        index_expr = $null
        vector_value = ""
        store_text = ""
        result = "unknown"
        lane = ""
        reason = ""
    }
}

function Complete-Record {
    param([object]$Record)
    if ($null -eq $Record) {
        return $null
    }
    if ($null -ne $Record.index_expr -and $null -ne $Record.index_expr.lane) {
        $Record.result = "proven"
        $Record.lane = [string]$Record.index_expr.lane
        $Record.reason = $Record.index_expr.proof
    } elseif ($null -ne $Record.masked_address_expr -and $Record.masked_address_expr.base -ne "r[1]") {
        $Record.result = "unknown"
        $Record.reason = "masked address base is " + $Record.masked_address_expr.desc
    } else {
        $Record.result = "unknown"
        $Record.reason = "dynamic lane was not reduced to a constant"
    }
    return $Record
}

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}

$resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
$functionUpper = $Function.ToUpperInvariant()
$startUpper = $StartGuest.ToUpperInvariant()
$provenanceStartUpper = $ProvenanceStartGuest.ToUpperInvariant()
if ([string]::IsNullOrWhiteSpace($provenanceStartUpper)) {
    $provenanceStartUpper = $startUpper
}
$endUpper = $EndGuest.ToUpperInvariant()
$functionPattern = [Regex]::Escape($functionUpper)
$phasePattern = [Regex]::Escape($Phase)
$linePattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"

$items = New-Object System.Collections.Generic.List[object]
$ppcOrdinals = @{}
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
        $items.Add([pscustomobject][ordered]@{
            kind = "label"; ordinal = $ordinal; address = ""; op = "";
            text = $text.Trim(); ppc_address = $currentPpcAddress;
            ppc_op = $currentPpcOp
        }) | Out-Null
        $ordinal += 1
        return
    }
    if ($text -match "^\s*;\s+(?<addr>[0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+(?<op>[A-Za-z0-9_\.]+)\s*(?<args>.*)$") {
        $currentPpcAddress = $Matches.addr.ToUpperInvariant()
        $currentPpcOp = $Matches.op.ToLowerInvariant()
        $items.Add([pscustomobject][ordered]@{
            kind = "ppc"; ordinal = $ordinal; address = $currentPpcAddress;
            op = $currentPpcOp; text = $text.Trim();
            ppc_address = $currentPpcAddress; ppc_op = $currentPpcOp
        }) | Out-Null
        if (!$ppcOrdinals.ContainsKey($currentPpcAddress)) {
            $ppcOrdinals[$currentPpcAddress] = $ordinal
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
    $items.Add([pscustomobject][ordered]@{
        kind = "hir"; ordinal = $ordinal; address = ""; op = $op;
        text = $trimmed; ppc_address = $currentPpcAddress;
        ppc_op = $currentPpcOp
    }) | Out-Null
    $ordinal += 1
}

if (!$ppcOrdinals.ContainsKey($startUpper)) {
    throw "StartGuest $startUpper not found in $Phase dump for $functionUpper."
}
if (!$ppcOrdinals.ContainsKey($provenanceStartUpper)) {
    throw "ProvenanceStartGuest $provenanceStartUpper not found in $Phase dump for $functionUpper."
}

$targetStartOrdinal = [int]$ppcOrdinals[$startUpper]
$startOrdinal = [int]$ppcOrdinals[$provenanceStartUpper]
if ($startOrdinal -gt $targetStartOrdinal) {
    throw "ProvenanceStartGuest $provenanceStartUpper must be before or equal to StartGuest $startUpper."
}
$endOrdinal = $items.Count
if (![string]::IsNullOrWhiteSpace($endUpper)) {
    if (!$ppcOrdinals.ContainsKey($endUpper)) {
        throw "EndGuest $endUpper not found in $Phase dump for $functionUpper."
    }
    $endOrdinal = [int]$ppcOrdinals[$endUpper]
}
$slice = $items | Where-Object { [int]$_.ordinal -ge $startOrdinal -and [int]$_.ordinal -lt $endOrdinal }

$bodyProfileLine = ""
if (![string]::IsNullOrWhiteSpace($BlockProfileLog) -and (Test-Path -LiteralPath $BlockProfileLog)) {
    $resolvedProfile = (Resolve-Path -LiteralPath $BlockProfileLog).Path
    $bodyPattern = "A64 speed profile block body top \d+: fn $functionPattern .* guest=$startUpper body_ticks_delta=(?<delta>\d+) body_ticks_total=(?<total>\d+) entries_delta=(?<entries>\d+) ticks_per_entry=(?<tpe>\d+)"
    Get-Content -LiteralPath $resolvedProfile | ForEach-Object {
        if ($_ -match $bodyPattern) {
            $bodyProfileLine = "body_ticks_total={0} body_ticks_delta={1} entries_delta={2} ticks_per_entry={3}" -f $Matches.total, $Matches.delta, $Matches.entries, $Matches.tpe
        }
    }
}

$exprs = @{}
$contextExprs = @{}
$records = New-Object System.Collections.Generic.List[object]
$currentRecord = $null
$currentPpcText = ""

foreach ($row in $slice) {
    if ($row.kind -eq "ppc") {
        $completed = Complete-Record $currentRecord
        if ($null -ne $completed) {
            $records.Add($completed) | Out-Null
        }
        $currentRecord = $null
        $currentPpcText = $row.text
        if ($row.op -eq "stvewx") {
            $currentRecord = New-Record $row.address $row.text
        }
        continue
    }
    if ($row.kind -ne "hir") {
        continue
    }

    $dest = ""
    $op = $row.op
    $args = ""
    if ($row.text -match "^(?<dest>v\d+)\.[^=]+=\s*(?<op>[A-Za-z0-9_\.]+)\s*(?<args>.*)$") {
        $dest = $Matches.dest
        $op = $Matches.op.ToLowerInvariant()
        $args = $Matches.args.Trim()
    } elseif ($row.text -match "^(?<op>[A-Za-z0-9_\.]+)\s*(?<args>.*)$") {
        $op = $Matches.op.ToLowerInvariant()
        $args = $Matches.args.Trim()
    }

    if (![string]::IsNullOrWhiteSpace($dest)) {
        if ($op -eq "load_context" -and $args -match "^\+(?<offset>\d+)") {
            $offset = [int]$Matches.offset
            $name = Get-ContextOffsetName $offset
            if ($contextExprs.ContainsKey($name)) {
                $exprs[$dest] = Copy-ExprWithChain $contextExprs[$name] $row.text
            } else {
                $proof = "context"
                if ($name -eq "r[1]") {
                    $proof = "r1_stack_pointer_assumed_16b_aligned"
                }
                $exprs[$dest] = New-Expr -Kind "context" -Desc $name -Base $name `
                    -Offset 0 -Proof $proof -Chain @($row.text)
            }
            continue
        }
        if ($op -eq "add" -and $args -match "^(?<lhs>[^,]+),\s*(?<rhs>.+)$") {
            $lhs = Resolve-ValueExpr $exprs $Matches.lhs
            $rhs = Resolve-ValueExpr $exprs $Matches.rhs
            $constant = $null
            $source = $null
            if ($lhs.kind -eq "const" -and $rhs.base) {
                $constant = $lhs.offset
                $source = $rhs
            } elseif ($rhs.kind -eq "const" -and $lhs.base) {
                $constant = $rhs.offset
                $source = $lhs
            }
            if ($null -ne $source -and $null -ne $constant -and $null -ne $source.offset) {
                $newOffset = [int64]$source.offset + [int64]$constant
                $desc = "{0}{1}" -f $source.base, (Format-Offset $newOffset)
                $chain = @()
                if ($source.chain) { $chain += $source.chain }
                $chain += $row.text
                $exprs[$dest] = New-Expr -Kind "affine" -Desc $desc -Base $source.base `
                    -Offset $newOffset -Proof $source.proof -Chain $chain
            } else {
                $exprs[$dest] = New-UnknownExpr -Desc $row.text -Chain @($row.text)
            }
            continue
        }
        if ($op -eq "and" -and $args -match "^(?<lhs>[^,]+),\s*(?<rhs>.+)$") {
            $lhs = Resolve-ValueExpr $exprs $Matches.lhs
            $rhsConst = Convert-HirConst $Matches.rhs
            if ($null -ne $rhsConst -and $rhsConst -eq -4 -and $null -ne $lhs.offset) {
                $low = $null
                $lane = $null
                $proof = $lhs.proof
                if ($lhs.base -eq "r[1]") {
                    $low = ([int64]$lhs.offset) -band 0xF
                    $low = $low -band 0xC
                    $lane = [int]($low / 4)
                    $proof = "r1_stack_pointer_assumed_16b_aligned"
                }
                $desc = "({0})&~3" -f $lhs.desc
                $chain = @()
                if ($lhs.chain) { $chain += $lhs.chain }
                $chain += $row.text
                $exprs[$dest] = New-Expr -Kind "word_address" -Desc $desc `
                    -Base $lhs.base -Offset $lhs.offset -LowNibble $low `
                    -Lane $lane -Proof $proof -Chain $chain
                if ($null -ne $currentRecord -and $row.ppc_op -eq "stvewx") {
                    $currentRecord.masked_address_value = $dest
                    $currentRecord.masked_address_expr = $exprs[$dest]
                }
            } elseif ($null -ne $rhsConst -and $rhsConst -eq 0xF) {
                $source = $lhs
                $chain = @()
                if ($source.chain) { $chain += $source.chain }
                $chain += $row.text
                $exprs[$dest] = New-Expr -Kind "low_nibble" -Desc ("low4({0})" -f $source.desc) `
                    -Base $source.base -Offset $source.offset -LowNibble $source.low_nibble `
                    -Lane $source.lane -Proof $source.proof -Chain $chain
            } else {
                $exprs[$dest] = New-UnknownExpr -Desc $row.text -Chain @($row.text)
            }
            continue
        }
        if ($op -eq "truncate" -and $args -match "^(?<src>v\d+\.[^,]+)") {
            $exprs[$dest] = Copy-ExprWithChain (Resolve-ValueExpr $exprs $Matches.src) $row.text
            continue
        }
        if ($op -eq "shr" -and $args -match "^(?<src>[^,]+),\s*(?<shift>[0-9A-Fa-f]+)$") {
            $source = Resolve-ValueExpr $exprs $Matches.src
            $shift = Convert-HirConst $Matches.shift
            $lane = $null
            if ($null -ne $source.low_nibble -and $null -ne $shift -and $shift -eq 2) {
                $lane = [int]([int64]$source.low_nibble / 4)
            }
            $chain = @()
            if ($source.chain) { $chain += $source.chain }
            $chain += $row.text
            $exprs[$dest] = New-Expr -Kind "lane" -Desc ("lane({0})" -f $source.desc) `
                -Base $source.base -Offset $source.offset -LowNibble $source.low_nibble `
                -Lane $lane -Proof $source.proof -Chain $chain
            continue
        }
        if ($op -eq "extract" -and $args -match "^(?<vector>[^,]+),\s*(?<index>.+)$") {
            $indexExpr = Resolve-ValueExpr $exprs $Matches.index
            $exprs[$dest] = New-UnknownExpr -Desc $row.text -Chain @($row.text)
            if ($null -ne $currentRecord -and $row.ppc_op -eq "stvewx") {
                $currentRecord.vector_value = $Matches.vector.Trim()
                $currentRecord.index_value = $Matches.index.Trim()
                $currentRecord.index_expr = $indexExpr
            }
            continue
        }
        $exprs[$dest] = New-UnknownExpr -Desc $row.text -Chain @($row.text)
        continue
    }

    if ($op -eq "context_barrier" -or $op -like "call*") {
        $contextExprs.Clear()
        continue
    }

    if ($op -eq "store_context" -and $args -match "^\+(?<offset>\d+),\s*(?<value>.+)$") {
        $offset = [int]$Matches.offset
        $name = Get-ContextOffsetName $offset
        $source = Resolve-ValueExpr $exprs $Matches.value
        if ($null -ne $source -and $source.kind -ne "unknown") {
            $contextExprs[$name] = Copy-ExprWithChain $source $row.text
        } elseif ($contextExprs.ContainsKey($name)) {
            $contextExprs.Remove($name)
        }
        continue
    }

    if ($op -eq "store.1" -and $null -ne $currentRecord) {
        $currentRecord.store_text = $row.text
    }
}

$completedTail = Complete-Record $currentRecord
if ($null -ne $completedTail) {
    $records.Add($completedTail) | Out-Null
}

Write-Output "# HIR STVEWX Lane Audit"
Write-Output ""
Write-Output ("log={0}" -f $resolvedLog)
if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    Write-Output ("block_profile_log={0}" -f (Resolve-Path -LiteralPath $BlockProfileLog).Path)
}
Write-Output ("function={0}" -f $functionUpper)
Write-Output ("phase={0}" -f $Phase)
Write-Output ("slice={0}-{1}" -f $startUpper, $(if ([string]::IsNullOrWhiteSpace($endUpper)) { "next" } else { $endUpper }))
if ($provenanceStartUpper -ne $startUpper) {
    Write-Output ("provenance_slice={0}-{1}" -f $provenanceStartUpper, $(if ([string]::IsNullOrWhiteSpace($endUpper)) { "next" } else { $endUpper }))
}
if (![string]::IsNullOrWhiteSpace($bodyProfileLine)) {
    Write-Output $bodyProfileLine
}
Write-Output ("stvewx_count={0}" -f $records.Count)
Write-Output ""
Write-Output "## Results"

foreach ($record in $records) {
    $addressDesc = "-"
    $chain = @()
    if ($null -ne $record.masked_address_expr) {
        $addressDesc = $record.masked_address_expr.desc
        $chain = $record.masked_address_expr.chain
    }
    $indexDesc = "-"
    if ($null -ne $record.index_expr) {
        $indexDesc = $record.index_expr.desc
        if (!$chain -and $record.index_expr.chain) {
            $chain = $record.index_expr.chain
        }
    }
    Write-Output ("pc={0} result={1} lane={2} address={3} index={4} vector={5} reason={6}" -f `
        $record.ppc_address, $record.result, $(if ([string]::IsNullOrWhiteSpace($record.lane)) { "-" } else { $record.lane }), `
        $addressDesc, $indexDesc, $record.vector_value, $record.reason)
    Write-Output ("  ppc={0}" -f $record.ppc_text)
    if (![string]::IsNullOrWhiteSpace($record.store_text)) {
        Write-Output ("  store={0}" -f $record.store_text)
    }
    if ($chain) {
        $chainText = ($chain | Select-Object -Last 8) -join " | "
        Write-Output ("  chain={0}" -f $chainText)
    }
}
