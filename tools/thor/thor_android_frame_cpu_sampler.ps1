param(
    [string]$DeviceSerial = "",
    [string]$PackageName = "jp.xenia.emulator.github.debug",
    [string]$ProcessId = "",
    [int]$Seconds = 180,
    [int]$IntervalMs = 1000,
    [string]$OutPath = "",
    [string]$SurfaceLayer = "",
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

function Write-Line {
    param([string]$Line)

    $Line
    if ($script:OutPath) {
        $Line | Out-File -Encoding utf8 -Append $script:OutPath
    }
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

function Read-GfxInfoBrief {
    return @(Invoke-AdbShell "dumpsys gfxinfo $PackageName 2>/dev/null | grep -E 'Total frames rendered|Janky frames|90th percentile|95th percentile|99th percentile|Number Missed Vsync|Number High input latency|Number Slow UI thread|Number Slow bitmap uploads|Number Slow issue draw commands' | head -80")
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

if (!$targetPid) {
    Write-Line "decision=process_not_running"
    exit 0
}

Write-Line "surface_layers_begin"
foreach ($line in (Read-SurfaceLayers)) {
    Write-Line ("surface_layer={0}" -f $line)
}
Write-Line "surface_layers_end"

if ($SurfaceLayer) {
    Invoke-AdbShell "dumpsys SurfaceFlinger --latency-clear '$SurfaceLayer' >/dev/null 2>&1" | Out-Null
    Write-Line ("surface_latency_layer={0}" -f $SurfaceLayer)
}

$previousTotals = @{}
$aggregateJiffies = @{}
$aggregateCoreSamples = @{}
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$sampleIndex = 0
while ($stopwatch.Elapsed.TotalSeconds -lt $Seconds) {
    $hostTime = Get-Date -Format o
    $freqs = Read-CpuFreqs
    $thermals = Read-Thermals
    Write-Line ("sample index={0} elapsed_ms={1} host_time={2}" -f $sampleIndex, [int]$stopwatch.ElapsedMilliseconds, $hostTime)
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

if ($SurfaceLayer) {
    Write-Line "surface_latency_begin"
    foreach ($line in (Invoke-AdbShell "dumpsys SurfaceFlinger --latency '$SurfaceLayer' 2>/dev/null | head -260")) {
        Write-Line ("surface_latency={0}" -f $line)
    }
    Write-Line "surface_latency_end"
}

Write-Line "decision=sampler_complete"
