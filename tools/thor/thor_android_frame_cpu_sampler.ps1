param(
    [string]$DeviceSerial = "",
    [string]$PackageName = "jp.xenia.emulator.github.debug",
    [string]$ProcessId = "",
    [int]$Seconds = 180,
    [int]$IntervalMs = 1000,
    [string]$OutPath = "",
    [string]$SurfaceLayer = "",
    [string]$AutoSurfaceLayer = "true",
    [switch]$IncludeIdleThreads
)

$ErrorActionPreference = "Stop"

function Resolve-AdbPath {
    $candidates = @()
    foreach ($root in @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT)) {
        if ($root) {
            $candidates += (Join-Path $root "platform-tools\adb.exe")
        }
    }
    $candidates += "adb"
    foreach ($candidate in $candidates) {
        $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($cmd) {
            return $cmd.Source
        }
    }
    return "adb"
}

function Invoke-Adb {
    param([string[]]$Arguments)

    $allArgs = @()
    if ($DeviceSerial) {
        $allArgs += @("-s", $DeviceSerial)
    }
    $allArgs += $Arguments
    & $script:AdbPath @allArgs 2>&1
}

function Invoke-AdbShell {
    param([string]$Command)

    Invoke-Adb @("shell", $Command)
}

function Read-DeviceMonotonicNs {
    $uptimeLine = ((Invoke-AdbShell "cat /proc/uptime 2>/dev/null") -join " ").Trim()
    if ($uptimeLine -match "^(?<seconds>[0-9]+(?:\.[0-9]+)?)") {
        [double]$seconds = 0.0
        if ([double]::TryParse($Matches.seconds, [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$seconds)) {
            return [Int64][Math]::Round($seconds * 1000000000.0)
        }
    }
    return ""
}

function Write-Line {
    param([string]$Line)

    $Line
    if ($script:OutPath) {
        $Line | Out-File -Encoding utf8 -Append $script:OutPath
    }
}

function ConvertTo-Bool {
    param([string]$Value)

    if (!$Value) {
        return $false
    }
    $lower = $Value.Trim().ToLowerInvariant()
    return @("1", "true", "yes", "y", "on").Contains($lower)
}

function ConvertTo-ShellSingleQuoted {
    param([string]$Value)

    return "'" + ($Value -replace "'", "'\\''") + "'"
}

function Resolve-TargetPid {
    if ($ProcessId) {
        return $ProcessId
    }

    $pidLine = ((Invoke-Adb @("shell", "pidof", $PackageName)) -join " ").Trim()
    if (!$pidLine) {
        return ""
    }
    return ($pidLine -split "\s+" | Where-Object { $_ } | Select-Object -First 1)
}

function Convert-ProcStat {
    param([string]$Stat)

    $match = [regex]::Match($Stat, "^\s*(?<pid>[0-9]+)\s+\((?<comm>.*)\)\s+(?<rest>.*)$")
    if (!$match.Success) {
        return $null
    }
    $parts = @($match.Groups["rest"].Value -split "\s+" | Where-Object { $_ -ne "" })
    if ($parts.Count -lt 37) {
        return $null
    }
    return [ordered]@{
        comm = $match.Groups["comm"].Value
        state = $parts[0]
        utime = [Int64]$parts[11]
        stime = [Int64]$parts[12]
        processor = $parts[36]
    }
}

function Read-ThreadStats {
    param([string]$TargetPid)

    $rows = New-Object System.Collections.Generic.List[object]
    $command = 'for t in /proc/' + $TargetPid + '/task/*; do tid=${t##*/}; stat=$(cat $t/stat 2>/dev/null | tr [:space:] ,); printf THREAD@%s@x@%s\\n $tid $stat; done'
    foreach ($line in (Invoke-AdbShell $command)) {
        if ($line -notmatch "^THREAD@") {
            continue
        }
        $parts = $line -split "@", 4
        if ($parts.Count -lt 4) {
            continue
        }
        $statText = $parts[3] -replace ",", " "
        $stat = Convert-ProcStat $statText
        if (!$stat) {
            continue
        }
        [void]$rows.Add([ordered]@{
            tid = $parts[1]
            name = $stat.comm
            state = $stat.state
            utime = [Int64]$stat.utime
            stime = [Int64]$stat.stime
            total = [Int64]$stat.utime + [Int64]$stat.stime
            processor = $stat.processor
        })
    }
    return $rows
}

function Read-CpuFreqs {
    $command = 'for c in /sys/devices/system/cpu/cpu[0-9]*; do f="$c/cpufreq/scaling_cur_freq"; [ -f "$f" ] && printf ''%s=%s\n'' "${c##*/}" "$(cat "$f")"; done'
    return ((Invoke-AdbShell $command) -join " ").Trim()
}

function Read-Thermals {
    $command = 'for z in /sys/class/thermal/thermal_zone*; do [ -f "$z/temp" ] || continue; type=$(cat "$z/type" 2>/dev/null); temp=$(cat "$z/temp" 2>/dev/null); printf ''%s=%s\n'' "$type" "$temp"; done'
    return ((Invoke-AdbShell $command) -join " ").Trim()
}

function Read-SurfaceLayers {
    $escapedPackage = $PackageName -replace "'", "'\\''"
    $command = "dumpsys SurfaceFlinger --list 2>/dev/null | grep -i -E 'xenia|emulator|$escapedPackage' | head -80"
    return @(Invoke-AdbShell $command)
}

function Select-SurfaceLatencyLayer {
    param([string[]]$Layers)

    $candidates = @(
        @{ pattern = "SurfaceView\[.*\]\(BLAST\)#"; exclude = "Background|Bounds|InputSink" },
        @{ pattern = "^SurfaceView\[.*\]#"; exclude = "Background|Bounds|InputSink" },
        @{ pattern = [regex]::Escape($PackageName) + ".*#"; exclude = "Background|Bounds|InputSink|ActivityRecordInputSink" }
    )

    foreach ($candidate in $candidates) {
        foreach ($layer in $Layers) {
            $trimmedLayer = $layer.Trim()
            if ($trimmedLayer -match $candidate.pattern -and $trimmedLayer -notmatch $candidate.exclude) {
                return $trimmedLayer
            }
        }
    }
    return ""
}

function Read-GfxInfoBrief {
    return @(Invoke-AdbShell "dumpsys gfxinfo $PackageName 2>/dev/null | grep -E 'Total frames rendered|Janky frames|90th percentile|95th percentile|99th percentile|Number Missed Vsync|Number High input latency|Number Slow UI thread|Number Slow bitmap uploads|Number Slow issue draw commands' | head -80")
}

function Write-SurfaceLatencySummary {
    param([string[]]$Lines)

    $refreshNs = ""
    $actualPresentTimes = New-Object System.Collections.Generic.List[Int64]
    $rejectedRows = 0
    $sentinelLimit = [Int64]::MaxValue - 1024
    foreach ($line in $Lines) {
        $trimmed = $line.Trim()
        if (!$refreshNs -and $trimmed -match "^[0-9]+$") {
            $refreshNs = $trimmed
            continue
        }
        $match = [regex]::Match($trimmed, "^(?<desired>[0-9]+)\s+(?<actual>[0-9]+)\s+(?<ready>[0-9]+)$")
        if (!$match.Success) {
            continue
        }
        $actual = [Int64]$match.Groups["actual"].Value
        if ($actual -le 0 -or $actual -ge $sentinelLimit) {
            $rejectedRows += 1
            continue
        }
        [void]$actualPresentTimes.Add($actual)
    }

    $actualPresentTimes.Sort()
    $intervals = New-Object System.Collections.Generic.List[Int64]
    for ($i = 1; $i -lt $actualPresentTimes.Count; $i += 1) {
        $delta = [Int64]$actualPresentTimes[$i] - [Int64]$actualPresentTimes[$i - 1]
        if ($delta -gt 0) {
            [void]$intervals.Add($delta)
        }
    }

    $over33 = @($intervals | Where-Object { $_ -gt 33333333 }).Count
    $over50 = @($intervals | Where-Object { $_ -gt 50000000 }).Count
    $avgMs = 0.0
    $maxMs = 0.0
    if ($intervals.Count -gt 0) {
        $avgNs = (($intervals | Measure-Object -Average).Average)
        $maxNs = (($intervals | Measure-Object -Maximum).Maximum)
        $avgMs = [double]$avgNs / 1000000.0
        $maxMs = [double]$maxNs / 1000000.0
    }
    $spanMs = 0.0
    if ($actualPresentTimes.Count -gt 1) {
        $spanMs = ([double]([Int64]$actualPresentTimes[$actualPresentTimes.Count - 1] - [Int64]$actualPresentTimes[0])) / 1000000.0
    }

    if ($refreshNs) {
        Write-Line ("surface_latency_refresh_ns={0}" -f $refreshNs)
    }
    Write-Line ("surface_latency_valid_frames={0}" -f $actualPresentTimes.Count)
    Write-Line ("surface_latency_interval_count={0}" -f $intervals.Count)
    Write-Line ("surface_latency_span_ms={0:F3}" -f $spanMs)
    Write-Line ("surface_latency_interval_avg_ms={0:F3}" -f $avgMs)
    Write-Line ("surface_latency_interval_max_ms={0:F3}" -f $maxMs)
    Write-Line ("surface_latency_intervals_over_33ms={0}" -f $over33)
    Write-Line ("surface_latency_intervals_over_50ms={0}" -f $over50)
    Write-Line ("surface_latency_rejected_rows={0}" -f $rejectedRows)
    if ($actualPresentTimes.Count -gt 1 -and $intervals.Count -gt 0) {
        Write-Line "surface_latency_decision=surface_latency_valid"
    } else {
        Write-Line "surface_latency_decision=surface_latency_missing_or_insufficient"
    }
}

$script:AdbPath = Resolve-AdbPath
if ($OutPath) {
    $script:OutPath = $OutPath
    $parent = Split-Path -Parent $OutPath
    if ($parent -and !(Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    "" | Out-File -Encoding utf8 $OutPath
} else {
    $script:OutPath = ""
}

$targetPid = Resolve-TargetPid
Write-Line "android_frame_cpu_sampler_version=1"
Write-Line ("timestamp={0}" -f (Get-Date -Format o))
Write-Line ("device_serial={0}" -f $DeviceSerial)
Write-Line ("package={0}" -f $PackageName)
Write-Line ("pid={0}" -f $targetPid)
Write-Line ("seconds={0}" -f $Seconds)
Write-Line ("interval_ms={0}" -f $IntervalMs)
Write-Line ("auto_surface_layer={0}" -f $AutoSurfaceLayer)
Write-Line ("start_device_monotonic_ns={0}" -f (Read-DeviceMonotonicNs))

if (!$targetPid) {
    Write-Line "decision=process_not_running"
    exit 0
}

$surfaceLayers = @(Read-SurfaceLayers)
Write-Line "surface_layers_begin"
foreach ($line in $surfaceLayers) {
    Write-Line ("surface_layer={0}" -f $line)
}
Write-Line "surface_layers_end"

$latencyLayer = $SurfaceLayer
$latencyLayerSource = "explicit"
if (!$latencyLayer -and (ConvertTo-Bool $AutoSurfaceLayer)) {
    $latencyLayer = Select-SurfaceLatencyLayer $surfaceLayers
    $latencyLayerSource = "auto"
}
if ($latencyLayer) {
    $quotedLayer = ConvertTo-ShellSingleQuoted $latencyLayer
    Invoke-AdbShell "dumpsys SurfaceFlinger --latency-clear $quotedLayer >/dev/null 2>&1" | Out-Null
    Write-Line ("surface_latency_layer={0}" -f $latencyLayer)
    Write-Line ("surface_latency_layer_source={0}" -f $latencyLayerSource)
} else {
    Write-Line "surface_latency_layer="
    Write-Line "surface_latency_layer_source=none"
}

$previousTotals = @{}
$aggregateJiffies = @{}
$aggregateCoreSamples = @{}
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$sampleIndex = 0
while ($stopwatch.Elapsed.TotalSeconds -lt $Seconds) {
    $hostTime = Get-Date -Format o
    $deviceMonotonicNs = Read-DeviceMonotonicNs
    $freqs = Read-CpuFreqs
    $thermals = Read-Thermals
    Write-Line ("sample index={0} elapsed_ms={1} host_time={2} device_monotonic_ns={3}" -f $sampleIndex, [int]$stopwatch.ElapsedMilliseconds, $hostTime, $deviceMonotonicNs)
    Write-Line ("cpu_freq {0}" -f $freqs)
    Write-Line ("thermal {0}" -f $thermals)

    foreach ($row in (Read-ThreadStats $targetPid)) {
        $delta = [Int64]0
        if ($previousTotals.ContainsKey($row.tid)) {
            $delta = [Int64]$row.total - [Int64]$previousTotals[$row.tid]
        }
        $previousTotals[$row.tid] = [Int64]$row.total
        if (!$aggregateJiffies.ContainsKey($row.tid)) {
            $aggregateJiffies[$row.tid] = [ordered]@{
                tid = $row.tid
                name = $row.name
                jiffies = [Int64]0
            }
        }
        $aggregateJiffies[$row.tid].jiffies = [Int64]$aggregateJiffies[$row.tid].jiffies + $delta
        $coreKey = "{0}:{1}" -f $row.tid, $row.processor
        if (!$aggregateCoreSamples.ContainsKey($coreKey)) {
            $aggregateCoreSamples[$coreKey] = [ordered]@{
                tid = $row.tid
                name = $row.name
                core = $row.processor
                samples = 0
            }
        }
        $aggregateCoreSamples[$coreKey].samples += 1

        $interesting = $IncludeIdleThreads -or $delta -gt 0 -or $row.name -match "Main Thread|GPU Commands|Draw Thread|XMA Decoder|Audio Worker|Emulator|RenderThread|Vsync"
        if ($interesting) {
            Write-Line ("thread sample={0} tid={1} name={2} state={3} core={4} jiffies_delta={5} jiffies_total={6}" -f `
                $sampleIndex, $row.tid, $row.name, $row.state, $row.processor, $delta, $row.total)
        }
    }

    $sampleIndex += 1
    $remaining = $Seconds - $stopwatch.Elapsed.TotalSeconds
    if ($remaining -le 0) {
        break
    }
    Start-Sleep -Milliseconds ([Math]::Min($IntervalMs, [int]($remaining * 1000)))
}

Write-Line "summary_thread_jiffies_begin"
foreach ($row in ($aggregateJiffies.Values | Sort-Object -Property @{ Expression = { [Int64]$_.jiffies }; Descending = $true } | Select-Object -First 32)) {
    Write-Line ("summary_thread tid={0} name={1} jiffies={2}" -f $row.tid, $row.name, $row.jiffies)
}
Write-Line "summary_thread_jiffies_end"

Write-Line "summary_core_residency_begin"
foreach ($row in ($aggregateCoreSamples.Values | Sort-Object tid, core)) {
    Write-Line ("summary_core tid={0} name={1} core={2} samples={3}" -f $row.tid, $row.name, $row.core, $row.samples)
}
Write-Line "summary_core_residency_end"

Write-Line "gfxinfo_brief_begin"
foreach ($line in (Read-GfxInfoBrief)) {
    Write-Line ("gfxinfo={0}" -f $line)
}
Write-Line "gfxinfo_brief_end"

if ($latencyLayer) {
    $quotedLayer = ConvertTo-ShellSingleQuoted $latencyLayer
    Write-Line ("surface_latency_capture_device_monotonic_ns={0}" -f (Read-DeviceMonotonicNs))
    Write-Line "surface_latency_begin"
    $latencyLines = @(Invoke-AdbShell "dumpsys SurfaceFlinger --latency $quotedLayer 2>/dev/null | head -260")
    foreach ($line in $latencyLines) {
        Write-Line ("surface_latency={0}" -f $line)
    }
    Write-Line "surface_latency_end"
    Write-SurfaceLatencySummary $latencyLines
} else {
    Write-Line "surface_latency_decision=no_surface_layer"
}

Write-Line "decision=sampler_complete"
