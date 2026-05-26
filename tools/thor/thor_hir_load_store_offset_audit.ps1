param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
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

function Convert-HirInt {
    param([string]$Value)

    $trimmed = $Value.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmed)) {
        return [int64]0
    }
    $negative = $false
    if ($trimmed.StartsWith("-")) {
        $negative = $true
        $trimmed = $trimmed.Substring(1)
    }
    if ($trimmed.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        $trimmed = $trimmed.Substring(2)
    }
    $parsed = [Convert]::ToInt64($trimmed, 16)
    if ($negative) {
        return -$parsed
    }
    return $parsed
}

function Format-HexSigned {
    param($Value)
    if ($null -eq $Value) {
        return "-"
    }
    $intValue = [int64]$Value
    if ($intValue -lt 0) {
        return ("-0x{0:X}" -f (-$intValue))
    }
    return ("0x{0:X}" -f $intValue)
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
        if ($lane -eq 0) { return ("r{0}" -f $index) }
        return ("r{0}+{1}" -f $index, $lane)
    }
    if ($Offset -ge 288 -and $Offset -lt 544) {
        $index = [Math]::Floor(($Offset - 288) / 8)
        $lane = ($Offset - 288) % 8
        if ($lane -eq 0) { return ("f{0}" -f $index) }
        return ("f{0}+{1}" -f $index, $lane)
    }
    if ($Offset -ge 544 -and $Offset -lt 2592) {
        $index = [Math]::Floor(($Offset - 544) / 16)
        $lane = ($Offset - 544) % 16
        if ($lane -eq 0) { return ("v{0}" -f $index) }
        return ("v{0}+{1}" -f $index, $lane)
    }
    if ($Offset -ge 2596 -and $Offset -lt 2628) {
        $crIndex = [Math]::Floor(($Offset - 2596) / 4)
        return ("cr{0}" -f $crIndex)
    }
    if ($Offset -ge 2628 -and $Offset -lt 2632) { return "fpscr" }
    return "context+0x{0:X}" -f $Offset
}

function New-BaseInfo {
    param(
        [string]$Kind,
        [string]$Name,
        [Nullable[int64]]$Displacement
    )
    return [pscustomobject][ordered]@{
        kind = $Kind
        name = $Name
        displacement = $Displacement
    }
}

function Get-BaseKind {
    param($BaseInfo)
    if ($null -eq $BaseInfo) {
        return "unknown"
    }
    if ($BaseInfo.kind -eq "context_gpr" -and $BaseInfo.name -eq "r1") {
        return "guest_stack"
    }
    if ($BaseInfo.kind -eq "context_gpr") {
        return "context_gpr"
    }
    return $BaseInfo.kind
}

function New-OffsetRow {
    param(
        [string]$Function,
        [string]$Phase,
        [object]$Block,
        [string]$Kind,
        [string]$Size,
        [string]$BaseReg,
        [object]$BaseInfo,
        [int64]$Offset,
        [string]$PpcAddress,
        [string]$PpcOp,
        [string]$Text
    )
    $effective = $null
    if ($null -ne $BaseInfo -and $null -ne $BaseInfo.displacement) {
        $effective = [int64]$BaseInfo.displacement + $Offset
    }
    return [pscustomobject][ordered]@{
        function = $Function
        phase = $Phase
        block = [int]$Block.ordinal
        block_guest = $Block.first_guest
        block_last_guest = $Block.last_guest
        kind = $Kind
        size = $Size
        base_reg = $BaseReg
        base_kind = (Get-BaseKind $BaseInfo)
        base_name = if ($null -ne $BaseInfo) { $BaseInfo.name } else { "" }
        base_disp = if ($null -ne $BaseInfo) { $BaseInfo.displacement } else { $null }
        offset = $Offset
        effective = $effective
        ppc_address = $PpcAddress
        ppc_op = $PpcOp
        text = $Text
        body_ticks_total = [int64]0
        entries_delta = [int64]0
        body_weight = [int64]0
    }
}

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}

$resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
$functionUpper = $Function.ToUpperInvariant()
$functionPattern = [Regex]::Escape($functionUpper)
$phasePattern = [Regex]::Escape($Phase)
$dumpPattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"

$bodyByBlock = @{}
$bodyPattern = "A64 speed profile block body top\s+\d+:\s+fn\s+$functionPattern\b.*?\bblock=(?<block>\d+)\s+guest=(?<guest>[0-9A-Fa-f]+)\s+body_ticks_delta=(?<delta>\d+)\s+body_ticks_total=(?<total>\d+)\s+entries_delta=(?<entries>\d+)\s+ticks_per_entry=(?<tpe>-?\d+).*?\bfirst_source=(?<first_source>[0-9A-Fa-f]+)\s+last_source=(?<last_source>[0-9A-Fa-f]+)\s+first_guest=(?<first_guest>[0-9A-Fa-f]+)\s+last_guest=(?<last_guest>[0-9A-Fa-f]+).*?\blabel=(?<label>[0-9A-Fa-f]+)\s+hir_instrs=(?<hir_instrs>\d+)"

$rows = New-Object System.Collections.Generic.List[object]
$provenance = @{}
$currentBlock = $null
$currentPpcAddress = ""
$currentPpcOp = ""
$blockCount = 0
$matchedDumpRows = 0

Get-Content -LiteralPath $resolvedLog | ForEach-Object {
    $line = $_
    $bodyMatch = [regex]::Match($line, $bodyPattern)
    if ($bodyMatch.Success) {
        $blockId = [int]$bodyMatch.Groups["block"].Value
        $bodyByBlock[$blockId] = [pscustomobject][ordered]@{
            block = $blockId
            guest = $bodyMatch.Groups["guest"].Value.ToUpperInvariant()
            body_ticks_total = [int64]$bodyMatch.Groups["total"].Value
            body_ticks_delta = [int64]$bodyMatch.Groups["delta"].Value
            entries_delta = [int64]$bodyMatch.Groups["entries"].Value
            ticks_per_entry = [int64]$bodyMatch.Groups["tpe"].Value
            first_source = $bodyMatch.Groups["first_source"].Value.ToUpperInvariant()
            last_source = $bodyMatch.Groups["last_source"].Value.ToUpperInvariant()
            first_guest = $bodyMatch.Groups["first_guest"].Value.ToUpperInvariant()
            last_guest = $bodyMatch.Groups["last_guest"].Value.ToUpperInvariant()
            label = $bodyMatch.Groups["label"].Value.ToUpperInvariant()
            hir_instrs = [int]$bodyMatch.Groups["hir_instrs"].Value
        }
    }

    if ($line -notmatch $dumpPattern) {
        return
    }

    $matchedDumpRows += 1
    $text = $Matches["text"].TrimEnd()

    if ($text -match "^\s*;\s+block_profile\s+ordinal=(?<ordinal>\d+)\s+first_source=(?<first_source>[0-9A-Fa-f]+)\s+last_source=(?<last_source>[0-9A-Fa-f]+)\s+first_guest=(?<first_guest>[0-9A-Fa-f]+)\s+last_guest=(?<last_guest>[0-9A-Fa-f]+)\s+first_comment=(?<first_comment>[0-9A-Fa-f]+)\s+last_comment=(?<last_comment>[0-9A-Fa-f]+)\s+label=(?<label>[0-9A-Fa-f]+)\s+hir_instrs=(?<hir_instrs>\d+)") {
        $currentBlock = [pscustomobject][ordered]@{
            ordinal = [int]$Matches["ordinal"]
            first_source = $Matches["first_source"].ToUpperInvariant()
            last_source = $Matches["last_source"].ToUpperInvariant()
            first_guest = $Matches["first_guest"].ToUpperInvariant()
            last_guest = $Matches["last_guest"].ToUpperInvariant()
            first_comment = $Matches["first_comment"].ToUpperInvariant()
            last_comment = $Matches["last_comment"].ToUpperInvariant()
            label = $Matches["label"].ToUpperInvariant()
            hir_instrs = [int]$Matches["hir_instrs"]
        }
        $provenance = @{}
        $currentPpcAddress = ""
        $currentPpcOp = ""
        $blockCount += 1
        return
    }

    if ($null -eq $currentBlock) {
        return
    }

    if ($text -match "^\s*;\s+(?<addr>[0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+(?<op>[A-Za-z0-9_\.]+)\s*(?<args>.*)$") {
        $currentPpcAddress = $Matches["addr"].ToUpperInvariant()
        $currentPpcOp = $Matches["op"].ToLowerInvariant()
        return
    }

    if ($text -match "^\s*(;|$)" -or $text -match "^\s*(loc_[0-9A-Fa-f]+|_label[0-9A-Fa-f]+):") {
        return
    }

    if ($text -match "^\s*(?<dest>v\d+)\.[^\s]+\s*=\s*load_context\s+\+(?<offset>\d+)") {
        $dest = $Matches["dest"]
        $contextOffset = [int]$Matches["offset"]
        $contextName = Get-ContextOffsetName $contextOffset
        if ($contextName -match "^r\d+$") {
            $provenance[$dest] = New-BaseInfo "context_gpr" $contextName ([int64]0)
        } else {
            $provenance[$dest] = New-BaseInfo "context" $contextName ([int64]0)
        }
        return
    }

    if ($text -match "^\s*(?<dest>v\d+)\.[^\s]+\s*=\s*(assign|zero_extend|sign_extend|truncate)\s+(?<src>v\d+)\.") {
        $dest = $Matches["dest"]
        $src = $Matches["src"]
        if ($provenance.ContainsKey($src)) {
            $provenance[$dest] = $provenance[$src]
        } else {
            $provenance.Remove($dest)
        }
    } elseif ($text -match "^\s*(?<dest>v\d+)\.[^\s]+\s*=\s*add\s+(?<src>v\d+)\.[^,]+,\s*(?<imm>-?(?:0x)?[0-9A-Fa-f]+)") {
        $dest = $Matches["dest"]
        $src = $Matches["src"]
        if ($provenance.ContainsKey($src)) {
            $srcInfo = $provenance[$src]
            $imm = Convert-HirInt $Matches["imm"]
            if ($null -ne $srcInfo.displacement) {
                $provenance[$dest] = New-BaseInfo $srcInfo.kind $srcInfo.name ([int64]$srcInfo.displacement + $imm)
            } else {
                $provenance[$dest] = New-BaseInfo $srcInfo.kind $srcInfo.name $null
            }
        } else {
            $provenance.Remove($dest)
        }
    } elseif ($text -match "^\s*(?<dest>v\d+)\.[^\s]+\s*=") {
        $provenance.Remove($Matches["dest"])
    }

    if ($text -match "^\s*(?:(?<dest>v\d+)\.[^\s]+\s*=\s*)?load_offset(?:\.(?<size>\d+))?\s+(?<base>v\d+)\.[^,]+,\s*(?<offset>-?(?:0x)?[0-9A-Fa-f]+)") {
        $base = $Matches["base"]
        $baseInfo = if ($provenance.ContainsKey($base)) { $provenance[$base] } else { $null }
        $size = if ($Matches["size"]) { $Matches["size"] } else { "native" }
        $rows.Add((New-OffsetRow $functionUpper $Phase $currentBlock "load" $size $base $baseInfo (Convert-HirInt $Matches["offset"]) $currentPpcAddress $currentPpcOp $text.Trim())) | Out-Null
        return
    }

    if ($text -match "^\s*store_offset(?:\.(?<size>\d+))?\s+(?<base>v\d+)\.[^,]+,\s*(?<offset>-?(?:0x)?[0-9A-Fa-f]+),\s*(?<value>.+)$") {
        $base = $Matches["base"]
        $baseInfo = if ($provenance.ContainsKey($base)) { $provenance[$base] } else { $null }
        $size = if ($Matches["size"]) { $Matches["size"] } else { "native" }
        $rows.Add((New-OffsetRow $functionUpper $Phase $currentBlock "store" $size $base $baseInfo (Convert-HirInt $Matches["offset"]) $currentPpcAddress $currentPpcOp $text.Trim())) | Out-Null
        return
    }
}

foreach ($row in $rows) {
    if ($bodyByBlock.ContainsKey($row.block)) {
        $body = $bodyByBlock[$row.block]
        $row.body_ticks_total = [int64]$body.body_ticks_total
        $row.entries_delta = [int64]$body.entries_delta
        $row.body_weight = [int64]$body.body_ticks_total
    }
}

$loadCount = @($rows | Where-Object { $_.kind -eq "load" }).Count
$storeCount = @($rows | Where-Object { $_.kind -eq "store" }).Count
$stackRows = @($rows | Where-Object { $_.base_kind -eq "guest_stack" })
$contextRows = @($rows | Where-Object { $_.base_kind -eq "context_gpr" })
$unknownRows = @($rows | Where-Object { $_.base_kind -eq "unknown" })
$mappedBodyRows = @($rows | Where-Object { $_.body_ticks_total -gt 0 })
$weightedTotal = [int64](($rows | Measure-Object -Property body_weight -Sum).Sum)
$weightedStack = [int64](($stackRows | Measure-Object -Property body_weight -Sum).Sum)
$weightedContext = [int64](($contextRows | Measure-Object -Property body_weight -Sum).Sum)
$weightedUnknown = [int64](($unknownRows | Measure-Object -Property body_weight -Sum).Sum)

$byOffset = @{}
$byBlock = @{}
$byPpc = @{}
$byBase = @{}
foreach ($row in $rows) {
    Add-Count $byOffset ("{0}:{1}:{2}:{3}" -f $row.kind, $row.base_kind, $row.size, (Format-HexSigned $row.effective)) $row.body_weight
    Add-Count $byBlock ("{0}:{1}-{2}" -f $row.block, $row.block_guest, $row.block_last_guest) $row.body_weight
    Add-Count $byPpc ("{0}:{1}" -f $row.ppc_address, $row.ppc_op) $row.body_weight
    Add-Count $byBase ("{0}:{1}" -f $row.base_kind, $row.base_name) $row.body_weight
}

Write-Output "# HIR Load/Store Offset Audit"
Write-Output ""
Write-Output "log=$resolvedLog"
Write-Output "function=$functionUpper"
Write-Output "phase=$Phase"
Write-Output "matched_dump_rows=$matchedDumpRows"
Write-Output "hir_blocks_seen=$blockCount"
Write-Output "body_profile_blocks=$($bodyByBlock.Count)"
Write-Output "offset_ops=$($rows.Count)"
Write-Output "load_offset_ops=$loadCount"
Write-Output "store_offset_ops=$storeCount"
Write-Output "mapped_body_ops=$($mappedBodyRows.Count)"
Write-Output "guest_stack_ops=$($stackRows.Count)"
Write-Output "context_gpr_ops=$($contextRows.Count)"
Write-Output "unknown_base_ops=$($unknownRows.Count)"
Write-Output "body_weighted_offset_ops=$weightedTotal"
Write-Output "body_weighted_guest_stack_ops=$weightedStack"
Write-Output "body_weighted_context_gpr_ops=$weightedContext"
Write-Output "body_weighted_unknown_base_ops=$weightedUnknown"
Write-Output "payload_materializations_allowed=0"
Write-Output "behavior_changed=0"
Write-Output ""
Write-Output "top_offsets_by_body_weight=$(Get-TopPairs $byOffset $Top)"
Write-Output "top_blocks_by_body_weight=$(Get-TopPairs $byBlock $Top)"
Write-Output "top_ppc_by_body_weight=$(Get-TopPairs $byPpc $Top)"
Write-Output "top_bases_by_body_weight=$(Get-TopPairs $byBase $Top)"
Write-Output ""
Write-Output "## Top Rows"
$rows |
    Sort-Object -Property @{ Expression = "body_weight"; Descending = $true },
                          @{ Expression = "block"; Ascending = $true },
                          @{ Expression = "ppc_address"; Ascending = $true },
                          @{ Expression = "kind"; Ascending = $true } |
    Select-Object -First $Top |
    ForEach-Object {
        Write-Output ("row kind={0} block={1} guest={2}-{3} body_ticks={4} entries_delta={5} base={6}/{7} base_disp={8} offset={9} effective={10} size={11} ppc={12}:{13} text=`"{14}`"" -f `
            $_.kind, $_.block, $_.block_guest, $_.block_last_guest, $_.body_ticks_total, $_.entries_delta, $_.base_kind, $_.base_name, (Format-HexSigned $_.base_disp), (Format-HexSigned $_.offset), (Format-HexSigned $_.effective), $_.size, $_.ppc_address, $_.ppc_op, $_.text)
    }
