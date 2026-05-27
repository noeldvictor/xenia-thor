param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [string]$FrameCpuSamplerPath = "",
    [string]$OutPath = "",
    [int]$LongIntervalMs = 50,
    [int]$TopGaps = 16
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

function Format-DoubleInvariant {
    param(
        [double]$Value,
        [string]$Format = "F3"
    )

    return $Value.ToString($Format, [System.Globalization.CultureInfo]::InvariantCulture)
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

function Classify-ThreadName {
    param([string]$Name)

    if ($Name -match "Main Thread") {
        return "main_thread"
    }
    if ($Name -match "XMA Decoder") {
        return "xma_decoder"
    }
    if ($Name -match "GPU Commands") {
        return "gpu_commands"
    }
    if ($Name -match "Draw Thread") {
        return "draw_thread"
    }
    if ($Name -match "Audio Worker") {
        return "audio_worker"
    }
    if ($Name -match "^XThread|Main XThread") {
        return "xthread"
    }
    return "other"
}

function Classify-SpeedFunction {
    param(
        [string]$Function,
        [string]$Name
    )

    $fn = $Function.ToUpperInvariant()
    if (@("82282490", "82281D28", "82287788", "82282388", "82282410") -contains $fn) {
        return "wall_cluster"
    }
    if ($Name -match "^(Rtl|Ke|Kf|Nt|Ob|Ex|Mm|Ki)" -or $fn -match "^8273") {
        return "kernel_hle"
    }
    return "other"
}

function Get-SurfaceLatencyIntervals {
    param([string]$SamplerText)

    $actualTimes = New-Object System.Collections.Generic.List[Int64]
    $sentinelLimit = [Int64]::MaxValue - 1024
    $rejectedRows = 0

    foreach ($match in [regex]::Matches($SamplerText, "^surface_latency=(?<value>.*)\r?$", [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        $line = $match.Groups["value"].Value.Trim()
        if ($line -notmatch "^(?<desired>[0-9]+)\s+(?<actual>[0-9]+)\s+(?<ready>[0-9]+)$") {
            continue
        }

        [Int64]$actual = 0
        if (![Int64]::TryParse($Matches.actual, [ref]$actual)) {
            $rejectedRows += 1
            continue
        }
        if ($actual -le 0 -or $actual -ge $sentinelLimit) {
            $rejectedRows += 1
            continue
        }
        [void]$actualTimes.Add($actual)
    }

    $actualTimes.Sort()
    $intervals = New-Object System.Collections.Generic.List[object]
    for ($i = 1; $i -lt $actualTimes.Count; $i += 1) {
        $startNs = [Int64]$actualTimes[$i - 1]
        $endNs = [Int64]$actualTimes[$i]
        $deltaNs = $endNs - $startNs
        if ($deltaNs -le 0) {
            continue
        }
        [void]$intervals.Add([pscustomobject][ordered]@{
            index = $i - 1
            start_ns = $startNs
            end_ns = $endNs
            delta_ns = $deltaNs
            delta_ms = [double]$deltaNs / 1000000.0
        })
    }

    return [pscustomobject][ordered]@{
        valid_frames = $actualTimes.Count
        interval_count = $intervals.Count
        rejected_rows = $rejectedRows
        intervals = $intervals
    }
}

function Get-SamplerSamples {
    param([string]$SamplerText)

    $samples = New-Object System.Collections.Generic.List[object]
    foreach ($match in [regex]::Matches($SamplerText, "^sample index=(?<index>[0-9]+)\s+elapsed_ms=(?<elapsed>[0-9]+)\s+host_time=(?<host>\S+)(?:\s+device_monotonic_ns=(?<mono>[0-9]+))?\r?$", [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        $mono = [Int64]-1
        if ($match.Groups["mono"].Success -and $match.Groups["mono"].Value) {
            [void][Int64]::TryParse($match.Groups["mono"].Value, [ref]$mono)
        }
        [void]$samples.Add([pscustomobject][ordered]@{
            index = [int]$match.Groups["index"].Value
            elapsed_ms = [Int64]$match.Groups["elapsed"].Value
            host_time = $match.Groups["host"].Value
            device_monotonic_ns = $mono
        })
    }
    return $samples
}

function Get-SamplerThreadDeltas {
    param([string]$SamplerText)

    $samples = @{}
    foreach ($match in [regex]::Matches($SamplerText, "^thread sample=(?<sample>[0-9]+)\s+tid=(?<tid>[0-9]+)\s+name=(?<name>.*?)\s+state=(?<state>\S+)\s+core=(?<core>\S+)\s+jiffies_delta=(?<delta>[0-9]+)\s+jiffies_total=(?<total>[0-9]+)\r?$", [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        $sample = [int]$match.Groups["sample"].Value
        if (!$samples.ContainsKey($sample)) {
            $samples[$sample] = @{}
        }
        $class = Classify-ThreadName $match.Groups["name"].Value
        Add-LongField $samples[$sample] $class ([Int64]$match.Groups["delta"].Value)
    }
    return $samples
}

function Convert-LogcatDate {
    param(
        [string]$DateText,
        [string]$TimeText,
        [System.DateTimeOffset]$ReferenceHostTime
    )

    $month = [int]$DateText.Substring(0, 2)
    $day = [int]$DateText.Substring(3, 2)
    $parts = $TimeText -split "[:.]"
    return [System.DateTimeOffset]::new(
        $ReferenceHostTime.Year,
        $month,
        $day,
        [int]$parts[0],
        [int]$parts[1],
        [int]$parts[2],
        [int]$parts[3],
        $ReferenceHostTime.Offset)
}

function Get-SpeedProfileBuckets {
    param(
        [string]$LogText,
        [System.DateTimeOffset]$ReferenceHostTime
    )

    $buckets = @{}
    $pattern = "^(?<date>[0-9]{2}-[0-9]{2})\s+(?<time>[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}).*A64 speed profile (?<kind>body top|top)\s+[0-9]+:\s+fn\s+(?<fn>[0-9A-Fa-f]+)\s+'(?<name>[^']*)'\s+(?:body_ticks_)?delta=(?<delta>[0-9]+)"
    foreach ($match in [regex]::Matches($LogText, $pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        $hostTime = Convert-LogcatDate $match.Groups["date"].Value $match.Groups["time"].Value $ReferenceHostTime
        $key = $hostTime.ToString("o")
        if (!$buckets.ContainsKey($key)) {
            $buckets[$key] = [ordered]@{
                host_time = $hostTime
                kernel_hle_delta = [Int64]0
                wall_cluster_delta = [Int64]0
                other_delta = [Int64]0
            }
        }
        $class = Classify-SpeedFunction $match.Groups["fn"].Value $match.Groups["name"].Value
        $delta = [Int64]$match.Groups["delta"].Value
        if ($class -eq "kernel_hle") {
            $buckets[$key].kernel_hle_delta = [Int64]$buckets[$key].kernel_hle_delta + $delta
        } elseif ($class -eq "wall_cluster") {
            $buckets[$key].wall_cluster_delta = [Int64]$buckets[$key].wall_cluster_delta + $delta
        } else {
            $buckets[$key].other_delta = [Int64]$buckets[$key].other_delta + $delta
        }
    }

    return @($buckets.Values | Sort-Object host_time)
}

function Find-NearestSample {
    param(
        [object[]]$Samples,
        [Int64]$DeviceMonotonicNs
    )

    $best = $null
    $bestDistance = [Int64]::MaxValue
    foreach ($sample in $Samples) {
        if ($sample.device_monotonic_ns -lt 0) {
            continue
        }
        $distance = [Math]::Abs([Int64]$sample.device_monotonic_ns - $DeviceMonotonicNs)
        if ($distance -lt $bestDistance) {
            $best = $sample
            $bestDistance = $distance
        }
    }
    return [pscustomobject][ordered]@{
        sample = $best
        distance_ns = $bestDistance
    }
}

function Estimate-HostTime {
    param(
        [object]$Sample,
        [Int64]$EventDeviceMonotonicNs
    )

    $hostTime = [System.DateTimeOffset]::Parse($Sample.host_time)
    $offsetMs = ([double]($EventDeviceMonotonicNs - [Int64]$Sample.device_monotonic_ns)) / 1000000.0
    return $hostTime.AddMilliseconds($offsetMs)
}

function Find-NearestSpeedBucket {
    param(
        [object[]]$Buckets,
        [System.DateTimeOffset]$HostTime
    )

    $best = $null
    $bestDistanceMs = [double]::MaxValue
    foreach ($bucket in $Buckets) {
        $distanceMs = [Math]::Abs(($bucket.host_time - $HostTime).TotalMilliseconds)
        if ($distanceMs -lt $bestDistanceMs) {
            $best = $bucket
            $bestDistanceMs = $distanceMs
        }
    }
    return [pscustomobject][ordered]@{
        bucket = $best
        distance_ms = $bestDistanceMs
    }
}

$FrameCpuSamplerPath = Resolve-OptionalCapturePath $FrameCpuSamplerPath "-frame-cpu-sampler.txt"
$samplerText = Read-OptionalText $FrameCpuSamplerPath
$logText = Read-OptionalText $LogPath

$lines = New-Object System.Collections.Generic.List[string]
[void]$lines.Add("present_jank_attribution_version=1")
[void]$lines.Add(("log={0}" -f $LogPath))
[void]$lines.Add(("frame_cpu_sampler={0}" -f $FrameCpuSamplerPath))

if (!$samplerText) {
    [void]$lines.Add("attribution_status=missing_sampler")
    [void]$lines.Add("decision=rerun_capture_with_frame_cpu_sampler")
} else {
    $samples = @(Get-SamplerSamples $samplerText)
    $monotonicSamples = @($samples | Where-Object { $_.device_monotonic_ns -ge 0 })
    $threadDeltas = Get-SamplerThreadDeltas $samplerText
    $latency = Get-SurfaceLatencyIntervals $samplerText
    $longIntervals = @($latency.intervals | Where-Object { $_.delta_ms -gt $LongIntervalMs })

    [void]$lines.Add(("sample_count={0}" -f $samples.Count))
    [void]$lines.Add(("device_monotonic_sample_count={0}" -f $monotonicSamples.Count))
    [void]$lines.Add(("surface_latency_valid_frames={0}" -f $latency.valid_frames))
    [void]$lines.Add(("surface_latency_interval_count={0}" -f $latency.interval_count))
    [void]$lines.Add(("surface_latency_rejected_rows={0}" -f $latency.rejected_rows))
    [void]$lines.Add(("long_present_interval_threshold_ms={0}" -f $LongIntervalMs))
    [void]$lines.Add(("long_present_interval_count={0}" -f $longIntervals.Count))

    if ($monotonicSamples.Count -eq 0) {
        [void]$lines.Add("attribution_status=missing_device_monotonic_samples")
        [void]$lines.Add("blocker=surface_latency_actual_present_ns_cannot_be_aligned_to_sampler_or_logcat_time")
        [void]$lines.Add("decision=rerun_capture_with_device_monotonic_sampler")
    } elseif ($latency.interval_count -eq 0) {
        [void]$lines.Add("attribution_status=missing_surface_latency_intervals")
        [void]$lines.Add("decision=capture_surface_latency_or_frametimeline")
    } else {
        $referenceHostTime = [System.DateTimeOffset]::Parse($samples[0].host_time)
        $speedBuckets = @(Get-SpeedProfileBuckets $logText $referenceHostTime)
        $sampleBuckets = @{}
        foreach ($interval in $longIntervals) {
            $nearest = Find-NearestSample $monotonicSamples ([Int64]$interval.end_ns)
            if ($null -eq $nearest.sample) {
                continue
            }
            $sample = $nearest.sample
            $key = [string]$sample.index
            if (!$sampleBuckets.ContainsKey($key)) {
                $deltas = @{}
                if ($threadDeltas.ContainsKey($sample.index)) {
                    $deltas = $threadDeltas[$sample.index]
                }
                $sampleBuckets[$key] = [ordered]@{
                    sample = $sample
                    present_intervals = 0
                    max_gap_ms = 0.0
                    total_gap_ms = 0.0
                    main_thread_jiffies = if ($deltas.ContainsKey("main_thread")) { [Int64]$deltas["main_thread"] } else { [Int64]0 }
                    xma_decoder_jiffies = if ($deltas.ContainsKey("xma_decoder")) { [Int64]$deltas["xma_decoder"] } else { [Int64]0 }
                    gpu_commands_jiffies = if ($deltas.ContainsKey("gpu_commands")) { [Int64]$deltas["gpu_commands"] } else { [Int64]0 }
                    draw_thread_jiffies = if ($deltas.ContainsKey("draw_thread")) { [Int64]$deltas["draw_thread"] } else { [Int64]0 }
                    xthread_jiffies = if ($deltas.ContainsKey("xthread")) { [Int64]$deltas["xthread"] } else { [Int64]0 }
                    nearest_profile_time = ""
                    nearest_profile_distance_ms = 0.0
                    kernel_hle_delta = [Int64]0
                    wall_cluster_delta = [Int64]0
                }
            }
            $bucket = $sampleBuckets[$key]
            $bucket.present_intervals += 1
            $bucket.total_gap_ms = [double]$bucket.total_gap_ms + [double]$interval.delta_ms
            if ([double]$interval.delta_ms -gt [double]$bucket.max_gap_ms) {
                $bucket.max_gap_ms = [double]$interval.delta_ms
            }
            if ($speedBuckets.Count -gt 0) {
                $estimatedHostTime = Estimate-HostTime $sample ([Int64]$interval.end_ns)
                $nearestSpeed = Find-NearestSpeedBucket $speedBuckets $estimatedHostTime
                if ($null -ne $nearestSpeed.bucket) {
                    $bucket.nearest_profile_time = $nearestSpeed.bucket.host_time.ToString("o")
                    $bucket.nearest_profile_distance_ms = [double]$nearestSpeed.distance_ms
                    $bucket.kernel_hle_delta = [Int64]$nearestSpeed.bucket.kernel_hle_delta
                    $bucket.wall_cluster_delta = [Int64]$nearestSpeed.bucket.wall_cluster_delta
                }
            }
        }

        [void]$lines.Add(("speed_profile_bucket_count={0}" -f $speedBuckets.Count))
        [void]$lines.Add(("attributed_sample_bucket_count={0}" -f $sampleBuckets.Count))
        [void]$lines.Add("attribution_status=time_aligned")

        $rank = 0
        foreach ($bucket in ($sampleBuckets.Values | Sort-Object -Property @{ Expression = { [double]$_.max_gap_ms }; Descending = $true } | Select-Object -First $TopGaps)) {
            $rank += 1
            $avgGapMs = 0.0
            if ($bucket.present_intervals -gt 0) {
                $avgGapMs = [double]$bucket.total_gap_ms / [double]$bucket.present_intervals
            }
            [void]$lines.Add(("present_bucket rank={0} sample={1} host_time={2} device_monotonic_ns={3} present_intervals={4} max_gap_ms={5} avg_gap_ms={6} main_thread_jiffies={7} xma_decoder_jiffies={8} gpu_commands_jiffies={9} draw_thread_jiffies={10} xthread_jiffies={11} nearest_profile_time={12} nearest_profile_distance_ms={13} kernel_hle_delta={14} wall_cluster_delta={15}" -f `
                $rank,
                $bucket.sample.index,
                $bucket.sample.host_time,
                $bucket.sample.device_monotonic_ns,
                $bucket.present_intervals,
                (Format-DoubleInvariant $bucket.max_gap_ms),
                (Format-DoubleInvariant $avgGapMs),
                $bucket.main_thread_jiffies,
                $bucket.xma_decoder_jiffies,
                $bucket.gpu_commands_jiffies,
                $bucket.draw_thread_jiffies,
                $bucket.xthread_jiffies,
                $bucket.nearest_profile_time,
                (Format-DoubleInvariant $bucket.nearest_profile_distance_ms),
                $bucket.kernel_hle_delta,
                $bucket.wall_cluster_delta))
        }
        [void]$lines.Add("decision=review_present_buckets_before_kernel_hle_or_vulkan_behavior")
    }
}

if ($OutPath) {
    $parent = Split-Path -Parent $OutPath
    if ($parent -and !(Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    $lines | Set-Content -LiteralPath $OutPath -Encoding utf8
}

$lines
