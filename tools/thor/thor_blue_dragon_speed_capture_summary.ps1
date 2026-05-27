param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [string]$MetaPath = "",
    [string]$PerfPath = "",
    [string]$ScreenshotPath = "",
    [double]$MinFps = 30.0,
    [int]$RequiredSeconds = 180
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

function Get-LastBodyRows {
    param([string]$Text)

    $rows = [ordered]@{}
    $pattern = "A64 speed profile body top\s+[0-9]+:\s+fn\s+(?<fn>[0-9A-Fa-f]+).*?body_ticks_delta=(?<delta>[0-9]+)\s+body_ticks_total=(?<total>[0-9]+)\s+entries_delta=(?<entries>[0-9]+)\s+ticks_per_entry=(?<tpe>[0-9]+)\s+code_size=(?<code>[0-9]+)"
    foreach ($match in [regex]::Matches($Text, $pattern)) {
        $fn = $match.Groups["fn"].Value.ToUpperInvariant()
        $rows[$fn] = [ordered]@{
            delta = $match.Groups["delta"].Value
            total = $match.Groups["total"].Value
            entries = $match.Groups["entries"].Value
            ticks_per_entry = $match.Groups["tpe"].Value
            code_size = $match.Groups["code"].Value
        }
    }
    return $rows
}

function Get-ThreadCpu {
    param([string]$Text)

    $threads = [ordered]@{}
    foreach ($name in @("Main Thread", "GPU Commands", "Draw Thread", "XMA Decoder")) {
        $escaped = [regex]::Escape($name)
        $match = [regex]::Match($Text, "^\s*\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+[RS]\s+(?<cpu>[0-9.]+).*?$escaped", [System.Text.RegularExpressions.RegexOptions]::Multiline)
        if ($match.Success) {
            $threads[$name] = $match.Groups["cpu"].Value
        }
    }
    return $threads
}

function Get-FpsMarkers {
    param([string]$Text)

    $values = New-Object System.Collections.Generic.List[double]
    $patterns = @(
        "(?i)\bfps\s*[:=]\s*(?<fps>[0-9]+(?:\.[0-9]+)?)",
        "(?i)\b(?<fps>[0-9]+(?:\.[0-9]+)?)\s*fps\b"
    )
    foreach ($pattern in $patterns) {
        foreach ($match in [regex]::Matches($Text, $pattern)) {
            $value = 0.0
            if ([double]::TryParse($match.Groups["fps"].Value, [ref]$value)) {
                [void]$values.Add($value)
            }
        }
    }
    return $values
}

function Get-SkippedFrameStats {
    param([string]$Text)

    $count = 0
    $max = 0
    foreach ($match in [regex]::Matches($Text, "Skipped\s+(?<frames>[0-9]+)\s+frames")) {
        $count += 1
        $frames = [int]$match.Groups["frames"].Value
        if ($frames -gt $max) {
            $max = $frames
        }
    }
    return [ordered]@{
        count = $count
        max = $max
    }
}

$LogPath = (Resolve-Path -LiteralPath $LogPath).Path
$MetaPath = Resolve-OptionalCapturePath $MetaPath "-meta.txt"
$PerfPath = Resolve-OptionalCapturePath $PerfPath "-perf-final.txt"
$ScreenshotPath = Resolve-OptionalCapturePath $ScreenshotPath "-screenshot.png"

$logText = Read-OptionalText $LogPath
$metaText = Read-OptionalText $MetaPath
$perfText = Read-OptionalText $PerfPath
$meta = Get-MetaMap $metaText
$fpsMarkers = @(Get-FpsMarkers $logText)
$fatalPattern = "AndroidRuntime|FATAL|fatal|crash|tombstone|signal [0-9]|backtrace|assert|Check failed|segfault|SIGSEGV|SIGABRT"
$fatalExclusionPattern = "Process\s+[0-9]+\s+exited due to signal 9 \(Killed\)|iportPostJsonRequest"
$fatalRawMatches = @()
$fatalMatches = @()
foreach ($line in ($logText -split "\r?\n")) {
    if ($line -match $fatalPattern) {
        $fatalRawMatches += $line
        if ($line -notmatch $fatalExclusionPattern) {
            $fatalMatches += $line
        }
    }
}
$skipped = Get-SkippedFrameStats $logText
$threads = Get-ThreadCpu $perfText
$bodyRows = Get-LastBodyRows $logText

$liveSeconds = 0
if ($meta.ContainsKey("live_capture_seconds")) {
    [void][int]::TryParse($meta["live_capture_seconds"], [ref]$liveSeconds)
}

$fpsMin = ""
$fpsMax = ""
if ($fpsMarkers.Count -gt 0) {
    $fpsMin = ($fpsMarkers | Measure-Object -Minimum).Minimum
    $fpsMax = ($fpsMarkers | Measure-Object -Maximum).Maximum
}

$sustainedProof = $false
$proofReason = "no_fps_markers"
if ($fpsMarkers.Count -gt 0) {
    if ($liveSeconds -lt $RequiredSeconds) {
        $proofReason = "capture_shorter_than_required_window"
    } elseif (($fpsMarkers | Where-Object { $_ -lt $MinFps } | Measure-Object).Count -gt 0) {
        $proofReason = "fps_marker_below_threshold"
    } else {
        $proofReason = "fps_markers_meet_threshold_but_sustained_window_manual_review_required"
    }
}

$mainThreadCpu = if ($threads.Contains("Main Thread")) { [double]$threads["Main Thread"] } else { 0.0 }
$gpuCpu = if ($threads.Contains("GPU Commands")) { [double]$threads["GPU Commands"] } else { 0.0 }
$wall = "unknown"
if ($mainThreadCpu -ge 80.0 -and $gpuCpu -lt 50.0) {
    $wall = "main_thread_a64_generated_code"
} elseif ($gpuCpu -ge $mainThreadCpu) {
    $wall = "gpu_or_renderer_thread"
}

Write-Output "capture_log=$LogPath"
Write-Output "meta=$MetaPath"
Write-Output "perf_final=$PerfPath"
Write-Output "screenshot=$ScreenshotPath"
Write-Output ("screenshot_exists={0}" -f (Test-Path -LiteralPath $ScreenshotPath))
foreach ($key in @("timestamp", "head", "apk_sha256", "target", "live_capture_seconds", "arm64_offset_memory_address_fastpath", "arm64_speed_profile_body_time_filter", "arm64_speed_profile_body_time_after_ms")) {
    if ($meta.ContainsKey($key)) {
        Write-Output ("{0}={1}" -f $key, $meta[$key])
    }
}
Write-Output ("fatal_marker_count={0}" -f $fatalMatches.Count)
if ($fatalRawMatches.Count -ne $fatalMatches.Count) {
    Write-Output ("fatal_marker_raw_count={0}" -f $fatalRawMatches.Count)
    Write-Output ("fatal_marker_excluded_count={0}" -f ($fatalRawMatches.Count - $fatalMatches.Count))
}
Write-Output ("fps_marker_count={0}" -f $fpsMarkers.Count)
if ($fpsMarkers.Count -gt 0) {
    Write-Output ("fps_marker_min={0}" -f $fpsMin)
    Write-Output ("fps_marker_max={0}" -f $fpsMax)
}
Write-Output ("choreographer_skipped_events={0}" -f $skipped.count)
Write-Output ("choreographer_skipped_max_frames={0}" -f $skipped.max)
foreach ($entry in $threads.GetEnumerator()) {
    Write-Output ("thread_cpu {0}={1}" -f ($entry.Key -replace " ", "_").ToLowerInvariant(), $entry.Value)
}
foreach ($entry in $bodyRows.GetEnumerator()) {
    $row = $entry.Value
    Write-Output ("body fn={0} total={1} delta={2} entries_delta={3} ticks_per_entry={4} code_size={5}" -f `
        $entry.Key, $row.total, $row.delta, $row.entries, $row.ticks_per_entry, $row.code_size)
}
Write-Output ("wall={0}" -f $wall)
Write-Output ("sustained_{0}fps_proof={1}" -f ([int]$MinFps), $sustainedProof.ToString().ToLowerInvariant())
Write-Output ("proof_reason={0}" -f $proofReason)
