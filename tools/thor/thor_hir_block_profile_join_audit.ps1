param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,

    [Parameter(Mandatory = $true)]
    [string]$Function,

    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",

    [string]$BlockProfileLog = "",

    [ValidateSet("Body", "Entry")]
    [string]$ProfileKind = "Body",

    [int]$Top = 20
)

$ErrorActionPreference = "Stop"

function Convert-HexAddress {
    param([string]$Value)
    return [Convert]::ToUInt32($Value, 16)
}

function Format-HexAddress {
    param([nullable[uint32]]$Value)
    if ($null -eq $Value) {
        return "-"
    }
    return ("{0:X8}" -f [uint32]$Value)
}

function Find-BeforeOrEqual {
    param(
        [uint32[]]$Addresses,
        [uint32]$Target
    )
    $best = $null
    foreach ($address in $Addresses) {
        if ($address -le $Target) {
            $best = $address
        } else {
            break
        }
    }
    return $best
}

function Find-AfterOrEqual {
    param(
        [uint32[]]$Addresses,
        [uint32]$Target
    )
    foreach ($address in $Addresses) {
        if ($address -ge $Target) {
            return $address
        }
    }
    return $null
}

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}
if ([string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    $BlockProfileLog = $LogPath
}
if (!(Test-Path -LiteralPath $BlockProfileLog)) {
    throw "BlockProfileLog not found: $BlockProfileLog"
}

$resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
$resolvedProfile = (Resolve-Path -LiteralPath $BlockProfileLog).Path
$functionUpper = $Function.ToUpperInvariant()
$functionPattern = [Regex]::Escape($functionUpper)
$phasePattern = [Regex]::Escape($Phase)
$linePattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"

$hirLabels = New-Object System.Collections.Generic.List[object]
$hirLabelAddresses = New-Object System.Collections.Generic.List[uint32]
$hirLabelSet = @{}
$ppcCommentAddresses = New-Object System.Collections.Generic.List[uint32]
$ppcCommentSet = @{}

Get-Content -LiteralPath $resolvedLog | ForEach-Object {
    if ($_ -notmatch $linePattern) {
        return
    }

    $text = $Matches.text
    if ($text -match "^\s*(loc_([0-9A-Fa-f]{8})):\s*$") {
        $addressText = $Matches[2].ToUpperInvariant()
        $address = Convert-HexAddress $addressText
        $hirLabels.Add([pscustomobject][ordered]@{
            index = $hirLabels.Count
            guest = $addressText
            address = $address
        }) | Out-Null
        $hirLabelAddresses.Add($address) | Out-Null
        $hirLabelSet[$addressText] = $true
        return
    }

    if ($text -match "^\s*;\s+([0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+") {
        $addressText = $Matches[1].ToUpperInvariant()
        if (!$ppcCommentSet.ContainsKey($addressText)) {
            $ppcCommentSet[$addressText] = $true
            $ppcCommentAddresses.Add((Convert-HexAddress $addressText)) |
                Out-Null
        }
    }
}

$labelArray = @($hirLabels.ToArray())
$sortedLabels = @($hirLabelAddresses.ToArray() | Sort-Object)
$sortedComments = @($ppcCommentAddresses.ToArray() | Sort-Object)

if ($ProfileKind -eq "Body") {
    $profilePattern = "A64 speed profile block body top \d+: fn $functionPattern .* block=(?<block>\d+) guest=(?<guest>[0-9A-Fa-f]{8}) body_ticks_delta=(?<delta>\d+) body_ticks_total=(?<total>\d+) entries_delta=(?<entries>\d+) ticks_per_entry=(?<tpe>\d+)"
} else {
    $profilePattern = "A64 speed profile block top \d+: fn $functionPattern .* block=(?<block>\d+) guest=(?<guest>[0-9A-Fa-f]{8}) delta=(?<delta>\d+) total=(?<total>\d+)"
}

$profileRows = New-Object System.Collections.Generic.List[object]
Get-Content -LiteralPath $resolvedProfile | ForEach-Object {
    $line = $_
    if ($line -notmatch $profilePattern) {
        return
    }

    $guest = $Matches.guest.ToUpperInvariant()
    $guestAddress = Convert-HexAddress $guest
    $blockOrdinal = [int]$Matches.block
    $ordinalLabel = $null
    if ($blockOrdinal -ge 0 -and $blockOrdinal -lt $labelArray.Count) {
        $ordinalLabel = $labelArray[$blockOrdinal].guest
    }

    $labelMatch = $hirLabelSet.ContainsKey($guest)
    $commentMatch = $ppcCommentSet.ContainsKey($guest)
    $ordinalMatch = ($null -ne $ordinalLabel -and $ordinalLabel -eq $guest)
    $nearestLabelBefore = Find-BeforeOrEqual $sortedLabels $guestAddress
    $nearestLabelAfter = Find-AfterOrEqual $sortedLabels $guestAddress
    $nearestCommentBefore = Find-BeforeOrEqual $sortedComments $guestAddress
    $nearestCommentAfter = Find-AfterOrEqual $sortedComments $guestAddress

    $metadataPresent = $false
    $firstSource = "-"
    $lastSource = "-"
    $firstGuest = "-"
    $lastGuest = "-"
    $firstComment = "-"
    $lastComment = "-"
    $label = "-"
    $hirInstrs = 0
    if ($line -match "first_source=(?<first_source>[0-9A-Fa-f]{8})\s+last_source=(?<last_source>[0-9A-Fa-f]{8})\s+first_guest=(?<first_guest>[0-9A-Fa-f]{8})\s+last_guest=(?<last_guest>[0-9A-Fa-f]{8})\s+first_comment=(?<first_comment>[0-9A-Fa-f]{8})\s+last_comment=(?<last_comment>[0-9A-Fa-f]{8})\s+label=(?<label>[0-9A-Fa-f]{8})\s+hir_instrs=(?<hir_instrs>\d+)") {
        $metadataPresent = $true
        $firstSource = $Matches.first_source.ToUpperInvariant()
        $lastSource = $Matches.last_source.ToUpperInvariant()
        $firstGuest = $Matches.first_guest.ToUpperInvariant()
        $lastGuest = $Matches.last_guest.ToUpperInvariant()
        $firstComment = $Matches.first_comment.ToUpperInvariant()
        $lastComment = $Matches.last_comment.ToUpperInvariant()
        $label = $Matches.label.ToUpperInvariant()
        $hirInstrs = [int]$Matches.hir_instrs
    }

    $metadataProfileSourceMatch = $metadataPresent -and (
        $guest -eq $firstSource -or $guest -eq $firstGuest -or $guest -eq $label)
    $metadataFirstCommentMatch = $metadataPresent -and
        $firstComment -ne "00000000" -and $ppcCommentSet.ContainsKey($firstComment)
    $metadataLabelMatch = $metadataPresent -and
        $label -ne "00000000" -and $hirLabelSet.ContainsKey($label)
    $metadataMappable = $metadataProfileSourceMatch -and
        ($metadataFirstCommentMatch -or $metadataLabelMatch)

    $profileRows.Add([pscustomobject][ordered]@{
        block = $blockOrdinal
        guest = $guest
        delta = [int64]$Matches.delta
        total = [int64]$Matches.total
        entries = if ($ProfileKind -eq "Body") { [int64]$Matches.entries } else { 0 }
        ticks_per_entry = if ($ProfileKind -eq "Body") { [int64]$Matches.tpe } else { 0 }
        hir_label_match = $labelMatch
        ppc_comment_match = $commentMatch
        ordinal_label = if ($null -ne $ordinalLabel) { $ordinalLabel } else { "-" }
        ordinal_label_match = $ordinalMatch
        nearest_hir_label_before = Format-HexAddress $nearestLabelBefore
        nearest_hir_label_after = Format-HexAddress $nearestLabelAfter
        nearest_ppc_comment_before = Format-HexAddress $nearestCommentBefore
        nearest_ppc_comment_after = Format-HexAddress $nearestCommentAfter
        metadata_present = $metadataPresent
        metadata_mappable = $metadataMappable
        first_source = $firstSource
        last_source = $lastSource
        first_guest = $firstGuest
        last_guest = $lastGuest
        first_comment = $firstComment
        last_comment = $lastComment
        label = $label
        hir_instrs = $hirInstrs
    }) | Out-Null
}

$rows = @($profileRows | Sort-Object -Property @{ Expression = "total"; Descending = $true },
                                      @{ Expression = "block"; Ascending = $true },
                                      @{ Expression = "guest"; Ascending = $true })
$labelMatches = @($rows | Where-Object { $_.hir_label_match }).Count
$commentMatches = @($rows | Where-Object { $_.ppc_comment_match }).Count
$ordinalMatches = @($rows | Where-Object { $_.ordinal_label_match }).Count
$unmatchedGuests = @($rows | Where-Object { !$_.hir_label_match -and !$_.ppc_comment_match }).Count
$ordinalFallbackMismatches = @($rows | Where-Object {
    !$_.hir_label_match -and $_.ordinal_label -ne "-" -and !$_.ordinal_label_match
}).Count
$metadataRows = @($rows | Where-Object { $_.metadata_present }).Count
$metadataMappableRows = @($rows | Where-Object { $_.metadata_mappable }).Count

Write-Output "# HIR Block/Profile Join Audit"
Write-Output ""
Write-Output "log=$resolvedLog"
Write-Output "block_profile_log=$resolvedProfile"
Write-Output "function=$functionUpper"
Write-Output "phase=$Phase"
Write-Output "profile_kind=$ProfileKind"
Write-Output "hir_labels=$($labelArray.Count)"
Write-Output "ppc_comments=$($ppcCommentSet.Count)"
Write-Output "profile_rows=$($rows.Count)"
Write-Output "guest_hir_label_matches=$labelMatches"
Write-Output "guest_ppc_comment_matches=$commentMatches"
Write-Output "ordinal_label_matches=$ordinalMatches"
Write-Output "unmatched_profile_guests=$unmatchedGuests"
Write-Output "ordinal_fallback_mismatches=$ordinalFallbackMismatches"
Write-Output "metadata_rows=$metadataRows"
Write-Output "metadata_mappable_rows=$metadataMappableRows"
Write-Output ""
Write-Output "## Top Profile Rows"
if (!$rows) {
    Write-Output "(no profile rows)"
} else {
    $rows | Select-Object -First $Top | ForEach-Object {
        Write-Output ("block={0} guest={1} total={2} delta={3} entries={4} tpe={5} hir_label_match={6} ppc_comment_match={7} ordinal_label={8} ordinal_match={9} nearest_hir_before={10} nearest_hir_after={11} nearest_ppc_before={12} nearest_ppc_after={13} metadata={14} metadata_mappable={15} first_source={16} last_source={17} first_guest={18} last_guest={19} first_comment={20} last_comment={21} label={22} hir_instrs={23}" -f
            $_.block, $_.guest, $_.total, $_.delta, $_.entries,
            $_.ticks_per_entry, [int]$_.hir_label_match,
            [int]$_.ppc_comment_match, $_.ordinal_label,
            [int]$_.ordinal_label_match, $_.nearest_hir_label_before,
            $_.nearest_hir_label_after, $_.nearest_ppc_comment_before,
            $_.nearest_ppc_comment_after, [int]$_.metadata_present,
            [int]$_.metadata_mappable, $_.first_source, $_.last_source,
            $_.first_guest, $_.last_guest, $_.first_comment, $_.last_comment,
            $_.label, $_.hir_instrs)
    }
}

Write-Output ""
Write-Output "## Decision"
if (($unmatchedGuests -gt 0 -or $ordinalFallbackMismatches -gt 0) -and
    $metadataMappableRows -eq 0) {
    Write-Output "join_status=unsafe"
    Write-Output "reason=at least one runtime block profile guest is missing from HIR labels/comments or would map to a different HIR label through ordinal fallback"
    Write-Output "next=log or dump stable block metadata that maps runtime block ordinal/address to the HIR block/source-offset span before using weighted HIR counters for behavior patches"
} elseif ($metadataMappableRows -gt 0) {
    Write-Output "join_status=metadata_required"
    Write-Output "reason=runtime block profile guests need explicit metadata rather than ordinal fallback; metadata maps at least one profile source to a printed HIR comment or label"
    Write-Output "next=use metadata fields for profile-to-HIR joins and keep ordinal fallback disabled unless a separate audit proves it safe"
} else {
    Write-Output "join_status=safe"
    Write-Output "reason=profile guests match HIR labels/comments and ordinal labels"
}
