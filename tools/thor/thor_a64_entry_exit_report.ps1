param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath
)

$ErrorActionPreference = "Stop"

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}

$entryPattern = "A64 speed profile entry/exit top \d+: fn (?<fn>[0-9A-Fa-f]+) '(?<name>[^']*)' prolog_ticks_delta=(?<prologDelta>\d+) prolog_ticks_total=(?<prologTotal>\d+) epilog_ticks_delta=(?<epilogDelta>\d+) epilog_ticks_total=(?<epilogTotal>\d+) entries_delta=(?<entries>\d+)"
$bodyPattern = "A64 speed profile body top \d+: fn (?<fn>[0-9A-Fa-f]+) '(?<name>[^']*)' body_ticks_delta=(?<bodyDelta>\d+) body_ticks_total=(?<bodyTotal>\d+) entries_delta=(?<entries>\d+) ticks_per_entry=(?<ticksPerEntry>\d+) code_size=(?<codeSize>\d+)"

$rows = @{}

function Get-Row {
    param([string]$Function, [string]$Name)

    if (!$rows.ContainsKey($Function)) {
        $rows[$Function] = [ordered]@{
            function = $Function.ToUpperInvariant()
            name = $Name
            entry_rows = 0
            entry_entries = [uint64]0
            prolog_total = [uint64]0
            epilog_total = [uint64]0
            body_rows = 0
            body_entries = [uint64]0
            body_total = [uint64]0
            body_ticks_per_entry_last = [uint64]0
            code_size = [uint64]0
        }
    }
    return $rows[$Function]
}

Select-String -LiteralPath $LogPath -Pattern $entryPattern | ForEach-Object {
    $match = $_.Matches[0]
    $row = Get-Row $match.Groups["fn"].Value $match.Groups["name"].Value
    $row.entry_rows++
    $row.entry_entries += [uint64]$match.Groups["entries"].Value
    $row.prolog_total = [uint64]$match.Groups["prologTotal"].Value
    $row.epilog_total = [uint64]$match.Groups["epilogTotal"].Value
}

Select-String -LiteralPath $LogPath -Pattern $bodyPattern | ForEach-Object {
    $match = $_.Matches[0]
    $row = Get-Row $match.Groups["fn"].Value $match.Groups["name"].Value
    $row.body_rows++
    $row.body_entries += [uint64]$match.Groups["entries"].Value
    $row.body_total = [uint64]$match.Groups["bodyTotal"].Value
    $row.body_ticks_per_entry_last = [uint64]$match.Groups["ticksPerEntry"].Value
    $row.code_size = [uint64]$match.Groups["codeSize"].Value
}

$rows.Values |
    Where-Object { $_.entry_rows -gt 0 } |
    ForEach-Object {
        $combined = $_.prolog_total + $_.epilog_total
        $entryTicksPerCall = if ($_.entry_entries) {
            [math]::Round($combined / [double]$_.entry_entries, 3)
        } else {
            0
        }
        $bodyTicksPerCall = if ($_.body_entries) {
            [math]::Round($_.body_total / [double]$_.body_entries, 3)
        } else {
            0
        }
        $entryExitOfBodyPct = if ($_.body_total) {
            [math]::Round(($combined * 100.0) / [double]$_.body_total, 2)
        } else {
            0
        }
        [pscustomobject]@{
            function = $_.function
            name = $_.name
            entry_rows = $_.entry_rows
            prolog_total = $_.prolog_total
            epilog_total = $_.epilog_total
            combined_total = $combined
            entry_entries = $_.entry_entries
            entry_exit_ticks_per_call = $entryTicksPerCall
            body_total = $_.body_total
            body_entries = $_.body_entries
            body_ticks_per_call = $bodyTicksPerCall
            entry_exit_of_body_pct = $entryExitOfBodyPct
            body_ticks_per_entry_last = $_.body_ticks_per_entry_last
            code_size = $_.code_size
        }
    } |
    Sort-Object combined_total -Descending |
    Format-List
