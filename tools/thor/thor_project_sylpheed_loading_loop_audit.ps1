param(
    [string]$PacketDir = "",
    [string]$LogPath = "",
    [string]$StatusPath = "",
    [string]$VisualPath = "",
    [string]$TopThreadPath = "",
    [string]$OutPath = "",
    [int]$LiveVdSwapThreshold = 1000,
    [double]$GuestCpuThreshold = 30.0
)

$ErrorActionPreference = "Stop"

function Resolve-PacketPath {
    param(
        [string]$ExplicitPath,
        [string]$DefaultName,
        [switch]$Required
    )

    if ($ExplicitPath) {
        if (!(Test-Path -LiteralPath $ExplicitPath)) {
            throw "Path not found: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    if ($PacketDir) {
        $candidate = Join-Path $PacketDir $DefaultName
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    if ($Required) {
        throw "Pass -$DefaultName path or -PacketDir containing $DefaultName."
    }
    return ""
}

function Count-Matches {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )
    return @($Lines | Select-String -Pattern $Pattern).Count
}

function Read-KeyValueReport {
    param([string]$Path)

    $values = @{}
    if (!$Path -or !(Test-Path -LiteralPath $Path)) {
        return $values
    }

    foreach ($line in (Get-Content -LiteralPath $Path)) {
        $split = $line.IndexOf("=")
        if ($split -le 0) {
            continue
        }
        $key = $line.Substring(0, $split)
        $value = $line.Substring($split + 1)
        $values[$key] = $value
    }
    return $values
}

function Try-ParseLogcatTimestamp {
    param([string]$Line)

    if ($Line -notmatch '^(?<month>\d{2})-(?<day>\d{2})\s+(?<clock>\d{2}:\d{2}:\d{2}\.\d{3})') {
        return $null
    }

    $year = (Get-Date).Year
    $text = "{0:D4}-{1}-{2} {3}" -f $year, $Matches.month, $Matches.day, $Matches.clock
    $parsed = [DateTime]::MinValue
    if ([DateTime]::TryParseExact(
            $text,
            "yyyy-MM-dd HH:mm:ss.fff",
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::AssumeLocal,
            [ref]$parsed)) {
        return $parsed
    }
    return $null
}

function Measure-LogcatSpan {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )

    $count = 0
    $first = $null
    $last = $null
    $tidSet = @{}

    foreach ($line in $Lines) {
        if ($line -notmatch $Pattern) {
            continue
        }

        ++$count
        $timestamp = Try-ParseLogcatTimestamp $line
        if ($timestamp) {
            if (!$first) {
                $first = $timestamp
            }
            $last = $timestamp
        }

        if ($line -match '^\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3}\s+\d+\s+(?<tid>\d+)\s+') {
            $tidSet[$Matches.tid] = $true
        }
    }

    $spanSeconds = 0.0
    $rate = 0.0
    if ($first -and $last) {
        $spanSeconds = [Math]::Max(0.0, ($last - $first).TotalSeconds)
        if ($spanSeconds -gt 0.0) {
            $rate = [double]$count / $spanSeconds
        }
    }

    $tidList = @($tidSet.Keys | Sort-Object)
    return [pscustomobject][ordered]@{
        Count = $count
        First = $first
        Last = $last
        SpanSeconds = $spanSeconds
        Rate = $rate
        ThreadIds = ($tidList -join ",")
        ThreadIdCount = $tidList.Count
    }
}

function Read-DoubleValue {
    param([string]$Text)

    $value = 0.0
    if ([double]::TryParse(
            $Text,
            [Globalization.NumberStyles]::Float,
            [Globalization.CultureInfo]::InvariantCulture,
            [ref]$value)) {
        return $value
    }
    return 0.0
}

function Parse-TopThreads {
    param([string]$Path)

    $result = [ordered]@{
        Path = $Path
        TotalThreads = 0
        RunningThreads = 0
        SleepingThreads = 0
        ParsedRows = 0
        GuestThreadRows = 0
        GuestRunningRows = 0
        GuestCpuSum = 0.0
        GuestMaxCpu = 0.0
        GuestTopThread = ""
        GpuCpuSum = 0.0
        AudioCpuSum = 0.0
        MainXThreadCpu = 0.0
        TopRows = @()
    }

    if (!$Path -or !(Test-Path -LiteralPath $Path)) {
        return [pscustomobject]$result
    }

    $lines = Get-Content -LiteralPath $Path
    foreach ($line in $lines) {
        if ($line -match '^Threads:\s+(?<total>\d+)\s+total,\s+(?<running>\d+)\s+running,\s+(?<sleeping>\d+)\s+sleeping') {
            $result.TotalThreads = [int]$Matches.total
            $result.RunningThreads = [int]$Matches.running
            $result.SleepingThreads = [int]$Matches.sleeping
            continue
        }

        if ($line -notmatch '^\s*(?<tid>\d+)\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(?<state>[A-Z])\s+(?<cpu>\d+(?:\.\d+)?)\s+\S+\s+(?<time>\S+)\s+(?<thread>.+?)\s+jp\.xenia\.emulator') {
            continue
        }

        $cpu = Read-DoubleValue $Matches.cpu
        $threadName = $Matches.thread.Trim()
        $row = [pscustomobject][ordered]@{
            Tid = $Matches.tid
            State = $Matches.state
            Cpu = $cpu
            Time = $Matches.time
            Thread = $threadName
        }

        ++$result.ParsedRows
        if ($result.TopRows.Count -lt 8) {
            $result.TopRows += $row
        }

        if ($threadName -match 'XThread') {
            ++$result.GuestThreadRows
            $result.GuestCpuSum += $cpu
            if ($row.State -eq "R") {
                ++$result.GuestRunningRows
            }
            if ($cpu -gt $result.GuestMaxCpu) {
                $result.GuestMaxCpu = $cpu
                $result.GuestTopThread = $threadName
            }
            if ($threadName -match '^Main XThread') {
                $result.MainXThreadCpu += $cpu
            }
        } elseif ($threadName -match 'GPU') {
            $result.GpuCpuSum += $cpu
        } elseif ($threadName -match 'Audio|XMA') {
            $result.AudioCpuSum += $cpu
        }
    }

    return [pscustomobject]$result
}

if ($PacketDir) {
    $PacketDir = (Resolve-Path -LiteralPath $PacketDir).Path
}

$log = Resolve-PacketPath $LogPath "logcat.txt" -Required
$status = Resolve-PacketPath $StatusPath "status-report.txt"
$visual = Resolve-PacketPath $VisualPath "visual-status.txt"
$topThreads = Resolve-PacketPath $TopThreadPath "top-threads-live.txt"

$logLines = Get-Content -LiteralPath $log
$statusValues = Read-KeyValueReport $status
$visualValues = Read-KeyValueReport $visual
$top = Parse-TopThreads $topThreads

$vdSwap = Measure-LogcatSpan $logLines "\bVdSwap\("
$waitTrace = Measure-LogcatSpan $logLines "Xboxkrnl wait trace"
$longjmpReenterCount = Count-Matches $logLines "longjmp_reenter\s+1"
$throwsExceptionCount = Count-Matches $logLines "throws_exception\s+1"
$heapReleaseCount = Count-Matches $logLines "BaseHeap::Release failed|PhysicalHeap::Release failed|MmFreePhysicalMemory failed"
$nativeAbortCount = Count-Matches $logLines "Abort message:|Fatal signal|SIGABRT|AndroidRuntime|FATAL EXCEPTION"
$invalidPthreadJoinCount = Count-Matches $logLines "invalid pthread_t.*pthread_join"

$visualClassification = ""
if ($visualValues.ContainsKey("classification")) {
    $visualClassification = $visualValues["classification"]
}
$statusClassification = ""
if ($statusValues.ContainsKey("classification")) {
    $statusClassification = $statusValues["classification"]
}

$classification = "project_sylpheed_loading_loop_needs_more_evidence"
$reason = "packet does not yet join enough presentation, wait, and guest CPU evidence"
$guestExecutionEvidence = "missing_top_thread_snapshot"
if ($top.GuestCpuSum -ge $GuestCpuThreshold -or $top.GuestMaxCpu -ge $GuestCpuThreshold) {
    $guestExecutionEvidence = "guest_xthreads_consuming_cpu"
}
if ($top.ParsedRows -gt 0 -and $top.GuestCpuSum -lt $GuestCpuThreshold -and $top.GuestMaxCpu -lt $GuestCpuThreshold) {
    $guestExecutionEvidence = "guest_xthreads_not_cpu_dominant"
}

$kernelWaitEvidence = "missing_xboxkrnl_thread_wait_trace"
if ($waitTrace.Count -gt 0) {
    $kernelWaitEvidence = "xboxkrnl_thread_wait_trace_present"
}

$presentationEvidence = "missing_vdswap"
if ($vdSwap.Count -ge $LiveVdSwapThreshold) {
    $presentationEvidence = "vdswap_continuing"
}

if ($presentationEvidence -eq "vdswap_continuing" -and
        $guestExecutionEvidence -eq "guest_xthreads_consuming_cpu" -and
        $nativeAbortCount -eq 0 -and
        $invalidPthreadJoinCount -eq 0 -and
        $longjmpReenterCount -eq 0 -and
        $heapReleaseCount -eq 0) {
    $classification = "project_sylpheed_live_loading_guest_cpu_vdswap_no_crash"
    $reason = "near-black loading loop still has active guest XThread CPU and ongoing VdSwap with no crash, reenter, pthread, or heap-release markers"
}

if ($presentationEvidence -eq "vdswap_continuing" -and
        $guestExecutionEvidence -eq "guest_xthreads_not_cpu_dominant" -and
        $nativeAbortCount -eq 0) {
    $classification = "project_sylpheed_live_loading_presentation_no_guest_cpu"
    $reason = "near-black loading loop continues VdSwap but top snapshot does not show guest XThreads consuming material CPU"
}

$decision = "capture_with_xboxkrnl_thread_wait_trace_and_guest_pc_sampler"
if ($kernelWaitEvidence -eq "xboxkrnl_thread_wait_trace_present") {
    $decision = "join_wait_trace_with_guest_pc_or_presentation_next"
}
if ($classification -eq "project_sylpheed_live_loading_guest_cpu_vdswap_no_crash") {
    $decision = "add_guest_pc_or_progress_counter_for_cpu_spinning_loading_loop"
}

$report = @(
    "classification=$classification",
    "reason=$reason",
    "decision=$decision",
    "packet_dir=$PacketDir",
    "log_path=$log",
    "status_path=$status",
    "visual_path=$visual",
    "top_thread_path=$topThreads",
    "status_classification=$statusClassification",
    "visual_classification=$visualClassification",
    "presentation_evidence=$presentationEvidence",
    "guest_execution_evidence=$guestExecutionEvidence",
    "kernel_wait_evidence=$kernelWaitEvidence",
    "vdswap_count=$($vdSwap.Count)",
    "vdswap_span_seconds=$($vdSwap.SpanSeconds.ToString('0.000'))",
    "vdswap_rate_per_second=$($vdSwap.Rate.ToString('0.000'))",
    "vdswap_thread_ids=$($vdSwap.ThreadIds)",
    "wait_trace_count=$($waitTrace.Count)",
    "wait_trace_span_seconds=$($waitTrace.SpanSeconds.ToString('0.000'))",
    "top_total_threads=$($top.TotalThreads)",
    "top_running_threads=$($top.RunningThreads)",
    "top_sleeping_threads=$($top.SleepingThreads)",
    "top_parsed_rows=$($top.ParsedRows)",
    "guest_thread_rows=$($top.GuestThreadRows)",
    "guest_running_rows=$($top.GuestRunningRows)",
    "guest_cpu_sum=$($top.GuestCpuSum.ToString('0.0'))",
    "guest_max_cpu=$($top.GuestMaxCpu.ToString('0.0'))",
    "guest_top_thread=$($top.GuestTopThread)",
    "main_xthread_cpu=$($top.MainXThreadCpu.ToString('0.0'))",
    "gpu_cpu_sum=$($top.GpuCpuSum.ToString('0.0'))",
    "audio_cpu_sum=$($top.AudioCpuSum.ToString('0.0'))",
    "longjmp_reenter_count=$longjmpReenterCount",
    "throws_exception_count=$throwsExceptionCount",
    "heap_release_count=$heapReleaseCount",
    "native_abort_marker_count=$nativeAbortCount",
    "invalid_pthread_join_count=$invalidPthreadJoinCount"
)

$topIndex = 0
foreach ($row in $top.TopRows) {
    ++$topIndex
    $report += "top_row_$($topIndex)=$($row.Tid),$($row.State),$($row.Cpu.ToString('0.0')),$($row.Thread)"
}

if ($OutPath) {
    $parent = Split-Path -Parent $OutPath
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $report | Out-File -Encoding utf8 -FilePath $OutPath
}

$report
