param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [string]$MetaPath = "",
    [string]$PerfPath = "",
    [string]$ProofSummaryPath = "",
    [string]$OutPath = "",
    [int]$Top = 24
)

$ErrorActionPreference = "Stop"

function Resolve-OptionalCapturePath {
    param(
        [string]$ExplicitPath,
        [string]$Suffix
    )

    if ($ExplicitPath) {
        return $ExplicitPath
    }
    if ($LogPath -match "^(?<prefix>.+)-speed-logcat\.txt$") {
        return "$($Matches.prefix)$Suffix"
    }
    return ""
}

function Read-OptionalText {
    param([string]$Path)

    if (!$Path -or !(Test-Path -LiteralPath $Path)) {
        return ""
    }
    return Get-Content -LiteralPath $Path -Raw
}

function Get-MetaMap {
    param([string]$Text)

    $map = @{}
    foreach ($line in ($Text -split "\r?\n")) {
        if ($line -match "^(?<key>[A-Za-z0-9_]+)=(?<value>.*)$") {
            $map[$Matches.key] = $Matches.value
        }
    }
    return $map
}

function Get-Long {
    param([string]$Value)

    [Int64]$parsed = 0
    if ([Int64]::TryParse($Value, [ref]$parsed)) {
        return $parsed
    }
    return [Int64]0
}

function Add-LongField {
    param(
        [hashtable]$Map,
        [string]$Key,
        [Int64]$Value
    )

    if (!$Map.ContainsKey($Key)) {
        $Map[$Key] = [Int64]0
    }
    $Map[$Key] = [Int64]$Map[$Key] + $Value
}

function Get-OrCreateFnRow {
    param(
        [hashtable]$Rows,
        [string]$Function,
        [string]$Name
    )

    $fn = $Function.ToUpperInvariant()
    if (!$Rows.ContainsKey($fn)) {
        $Rows[$fn] = [ordered]@{
            fn = $fn
            name = $Name
            intervals = 0
            top_delta_sum = [Int64]0
            max_delta = [Int64]0
            last_total = [Int64]0
            code_size = [Int64]0
        }
    }
    if ($Name -and !$Rows[$fn].name) {
        $Rows[$fn].name = $Name
    }
    return $Rows[$fn]
}

function Classify-RouteFunction {
    param(
        [string]$Function,
        [string]$Name,
        [Int64]$CodeSize,
        [double]$Share
    )

    $fn = $Function.ToUpperInvariant()
    if (@("82282490", "82281D28", "82287788", "82282388", "82282410") -contains $fn) {
        return "closed_wall_cluster"
    }
    if ($Name -match "^(Rtl|Ke|Kf|Nt|Ob|Ex|Mm|Ki)" -or $fn -match "^8273") {
        return "kernel_hle_churn"
    }
    if ($CodeSize -gt 0 -and $CodeSize -le 512 -and $Share -ge 0.005) {
        return "tiny_hot_leaf_or_helper"
    }
    if ($Share -ge 0.01) {
        return "route_hot_unknown"
    }
    return "background_route_function"
}

function Get-ProofLines {
    param([string]$Path)

    $summaryText = Read-OptionalText $Path
    if ($summaryText) {
        return @($summaryText -split "\r?\n" | Where-Object { $_ })
    }

    $scriptPath = Join-Path $PSScriptRoot "thor_blue_dragon_speed_capture_summary.ps1"
    if (Test-Path -LiteralPath $scriptPath) {
        return @(& powershell -NoProfile -ExecutionPolicy Bypass -File $scriptPath -LogPath $LogPath 2>&1)
    }
    return @("proof_summary_unavailable=true")
}

$LogPath = (Resolve-Path -LiteralPath $LogPath).Path
$MetaPath = Resolve-OptionalCapturePath $MetaPath "-meta.txt"
$PerfPath = Resolve-OptionalCapturePath $PerfPath "-perf-final.txt"
$ProofSummaryPath = Resolve-OptionalCapturePath $ProofSummaryPath "-speed-proof-summary.txt"

$logText = Read-OptionalText $LogPath
$metaText = Read-OptionalText $MetaPath
$perfText = Read-OptionalText $PerfPath
$meta = Get-MetaMap $metaText
$proofLines = @(Get-ProofLines $ProofSummaryPath)

$summaryTotals = @{
    entry_delta = [Int64]0
    h2g_delta = [Int64]0
    g2h_delta = [Int64]0
    direct_delta = [Int64]0
    indirect_delta = [Int64]0
    extern_delta = [Int64]0
    resolves_delta = [Int64]0
    resolve_misses_delta = [Int64]0
}
$summaryIntervals = 0
$summaryPattern = "A64 speed profile summary: funcs=(?<funcs>[0-9]+)\s+entry_delta=(?<entry_delta>[0-9]+)\s+h2g=(?<h2g_delta>[0-9]+)/(?<h2g_total>[0-9]+)\s+g2h=(?<g2h_delta>[0-9]+)/(?<g2h_total>[0-9]+)\s+direct=(?<direct_delta>[0-9]+)/(?<direct_total>[0-9]+)\s+indirect=(?<indirect_delta>[0-9]+)/(?<indirect_total>[0-9]+)\s+extern=(?<extern_delta>[0-9]+)/(?<extern_total>[0-9]+)\s+resolves=(?<resolves_delta>[0-9]+)/(?<resolves_total>[0-9]+)\s+resolve_misses=(?<resolve_misses_delta>[0-9]+)/(?<resolve_misses_total>[0-9]+)"
foreach ($match in [regex]::Matches($logText, $summaryPattern)) {
    $summaryIntervals += 1
    foreach ($key in @("entry_delta", "h2g_delta", "g2h_delta", "direct_delta", "indirect_delta", "extern_delta", "resolves_delta", "resolve_misses_delta")) {
        Add-LongField $summaryTotals $key (Get-Long $match.Groups[$key].Value)
    }
}

$topRows = @{}
$topPattern = "A64 speed profile top\s+(?<rank>[0-9]+):\s+fn\s+(?<fn>[0-9A-Fa-f]+)\s+'(?<name>[^']*)'\s+delta=(?<delta>[0-9]+)\s+total=(?<total>[0-9]+)\s+code_size=(?<code>[0-9]+)"
foreach ($match in [regex]::Matches($logText, $topPattern)) {
    $row = Get-OrCreateFnRow $topRows $match.Groups["fn"].Value $match.Groups["name"].Value
    $delta = Get-Long $match.Groups["delta"].Value
    $row.intervals += 1
    $row.top_delta_sum = [Int64]$row.top_delta_sum + $delta
    if ($delta -gt $row.max_delta) {
        $row.max_delta = $delta
    }
    $row.last_total = Get-Long $match.Groups["total"].Value
    $row.code_size = Get-Long $match.Groups["code"].Value
}

$bodyRows = @{}
$bodyPattern = "A64 speed profile body top\s+(?<rank>[0-9]+):\s+fn\s+(?<fn>[0-9A-Fa-f]+)\s+'(?<name>[^']*)'.*?body_ticks_delta=(?<delta>[0-9]+)\s+body_ticks_total=(?<total>[0-9]+)\s+entries_delta=(?<entries>[0-9]+)\s+ticks_per_entry=(?<tpe>[0-9]+)\s+code_size=(?<code>[0-9]+)"
foreach ($match in [regex]::Matches($logText, $bodyPattern)) {
    $fn = $match.Groups["fn"].Value.ToUpperInvariant()
    $bodyRows[$fn] = [ordered]@{
        fn = $fn
        name = $match.Groups["name"].Value
        delta = Get-Long $match.Groups["delta"].Value
        total = Get-Long $match.Groups["total"].Value
        entries_delta = Get-Long $match.Groups["entries"].Value
        ticks_per_entry = Get-Long $match.Groups["tpe"].Value
        code_size = Get-Long $match.Groups["code"].Value
    }
}

$proofMap = @{}
foreach ($line in $proofLines) {
    if ($line -match "^(?<key>[A-Za-z0-9_]+)=(?<value>.*)$") {
        $proofMap[$Matches.key] = $Matches.value
    }
}

$mainThreadCpu = 0.0
if ($proofMap.ContainsKey("thread_cpu main_thread")) {
    [void][double]::TryParse($proofMap["thread_cpu main_thread"], [ref]$mainThreadCpu)
} elseif ($perfText -match "Main Thread.*?") {
    $match = [regex]::Match($perfText, "^\s*\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+[RS]\s+(?<cpu>[0-9.]+).*?Main Thread", [System.Text.RegularExpressions.RegexOptions]::Multiline)
    if ($match.Success) {
        [void][double]::TryParse($match.Groups["cpu"].Value, [ref]$mainThreadCpu)
    }
}

$entryDeltaTotal = [Int64]$summaryTotals.entry_delta
$rankedTop = @($topRows.Values | Sort-Object -Property @{ Expression = { [Int64]$_.top_delta_sum }; Descending = $true })
$rankedBody = @($bodyRows.Values | Sort-Object -Property @{ Expression = { [Int64]$_.total }; Descending = $true })

$classTotals = @{}
foreach ($row in $rankedTop) {
    $share = 0.0
    if ($entryDeltaTotal -gt 0) {
        $share = [double]$row.top_delta_sum / [double]$entryDeltaTotal
    }
    $class = Classify-RouteFunction $row.fn $row.name $row.code_size $share
    Add-LongField $classTotals $class ([Int64]$row.top_delta_sum)
}

$kernelShare = 0.0
if ($entryDeltaTotal -gt 0 -and $classTotals.ContainsKey("kernel_hle_churn")) {
    $kernelShare = [double]$classTotals["kernel_hle_churn"] / [double]$entryDeltaTotal
}
$tinyHelperShare = 0.0
if ($entryDeltaTotal -gt 0 -and $classTotals.ContainsKey("tiny_hot_leaf_or_helper")) {
    $tinyHelperShare = [double]$classTotals["tiny_hot_leaf_or_helper"] / [double]$entryDeltaTotal
}

$decision = "need_live_frame_cpu_residency_sampler_before_next_behavior_lane"
if ($kernelShare -ge 0.05) {
    $decision = "investigate_kernel_hle_churn_with_frame_cpu_residency_sampler"
} elseif ($tinyHelperShare -ge 0.05) {
    $decision = "investigate_tiny_hot_helper_or_leaf_cluster"
} elseif ($mainThreadCpu -ge 80.0) {
    $decision = "main_thread_route_engine_wall_needs_time_series_not_more_micro_audit"
}

$lines = New-Object System.Collections.Generic.List[string]
[void]$lines.Add("route_engine_reset_report_version=1")
[void]$lines.Add("log=$LogPath")
[void]$lines.Add("meta=$MetaPath")
[void]$lines.Add("perf_final=$PerfPath")
[void]$lines.Add("proof_summary=$ProofSummaryPath")
foreach ($key in @("head", "apk_sha256", "target", "live_capture_seconds", "arm64_offset_memory_address_fastpath")) {
    if ($meta.ContainsKey($key)) {
        [void]$lines.Add(("{0}={1}" -f $key, $meta[$key]))
    }
}
foreach ($key in @("sustained_30fps_proof", "proof_reason", "fps_marker_count", "fatal_marker_count", "choreographer_skipped_max_frames", "wall")) {
    if ($proofMap.ContainsKey($key)) {
        [void]$lines.Add(("{0}={1}" -f $key, $proofMap[$key]))
    }
}
[void]$lines.Add(("summary_intervals={0}" -f $summaryIntervals))
foreach ($key in @("entry_delta", "direct_delta", "indirect_delta", "extern_delta", "h2g_delta", "g2h_delta", "resolves_delta", "resolve_misses_delta")) {
    [void]$lines.Add(("dispatch {0}={1}" -f $key, $summaryTotals[$key]))
}
foreach ($entry in ($classTotals.GetEnumerator() | Sort-Object -Property Value -Descending)) {
    $share = 0.0
    if ($entryDeltaTotal -gt 0) {
        $share = [double]$entry.Value / [double]$entryDeltaTotal
    }
    [void]$lines.Add(("class class={0} top_delta_sum={1} share={2:N4}" -f $entry.Key, $entry.Value, $share))
}

$i = 0
foreach ($row in $rankedTop | Select-Object -First $Top) {
    $i += 1
    $share = 0.0
    if ($entryDeltaTotal -gt 0) {
        $share = [double]$row.top_delta_sum / [double]$entryDeltaTotal
    }
    $class = Classify-RouteFunction $row.fn $row.name $row.code_size $share
    [void]$lines.Add(("top_candidate rank={0} fn={1} name={2} class={3} intervals={4} top_delta_sum={5} share={6:N4} max_delta={7} last_total={8} code_size={9}" -f `
        $i, $row.fn, $row.name, $class, $row.intervals, $row.top_delta_sum, $share, $row.max_delta, $row.last_total, $row.code_size))
}

$i = 0
foreach ($row in $rankedBody | Select-Object -First $Top) {
    $i += 1
    [void]$lines.Add(("body_wall rank={0} fn={1} name={2} total={3} delta={4} entries_delta={5} ticks_per_entry={6} code_size={7}" -f `
        $i, $row.fn, $row.name, $row.total, $row.delta, $row.entries_delta, $row.ticks_per_entry, $row.code_size))
}

[void]$lines.Add(("kernel_hle_churn_share={0:N4}" -f $kernelShare))
[void]$lines.Add(("tiny_helper_share={0:N4}" -f $tinyHelperShare))
[void]$lines.Add("route_engine_gap=missing_time_series_frame_present_cpu_core_frequency_thermal_join")
[void]$lines.Add("closed_repeat_guard=do_not_rerun_same_quiet_capture_or_closed_a64_micro_lanes")
[void]$lines.Add(("decision={0}" -f $decision))

if ($OutPath) {
    $parent = Split-Path -Parent $OutPath
    if ($parent -and !(Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    $lines | Out-File -Encoding utf8 $OutPath
}

$lines
