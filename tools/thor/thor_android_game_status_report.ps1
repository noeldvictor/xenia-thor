param(
    [string]$LogPath = "",
    [string]$PacketDir = "",
    [string]$OutPath = ""
)

$ErrorActionPreference = "Stop"

if (!$LogPath -and $PacketDir) {
    $candidate = Join-Path $PacketDir "logcat.txt"
    if (Test-Path -LiteralPath $candidate) {
        $LogPath = $candidate
    }
}

if (!$LogPath) {
    throw "Pass -LogPath or -PacketDir containing logcat.txt."
}

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}

$lines = Get-Content -LiteralPath $LogPath
$text = ($lines -join "`n")
$packagePattern = "jp\.xenia\.emulator"

function First-MatchValue {
    param(
        [string[]]$Lines,
        [string]$Pattern,
        [string]$Group = "value"
    )
    foreach ($line in $Lines) {
        if ($line -match $Pattern) {
            return $Matches[$Group]
        }
    }
    return ""
}

function First-MatchLine {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )
    foreach ($line in $Lines) {
        if ($line -match $Pattern) {
            return $line
        }
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

function Get-LogcatPid {
    param([string]$Line)
    if ($Line -match "^\S+\s+\S+\s+(?<pid>\d+)\s+\d+\s+\S\s+") {
        return [int]$Matches["pid"]
    }
    return $null
}

function Count-ScopedMatches {
    param(
        [string[]]$Lines,
        [string]$Pattern,
        [hashtable]$ScopedPids,
        [string]$PackagePattern
    )
    $count = 0
    foreach ($line in $Lines) {
        if (!($line -match $Pattern)) {
            continue
        }
        $linePid = Get-LogcatPid $line
        if (($linePid -ne $null -and $ScopedPids.ContainsKey($linePid)) -or
                $line -match $PackagePattern) {
            ++$count
        }
    }
    return $count
}

function New-PidSet {
    $set = @{}
    return $set
}

$xeniaPids = New-PidSet
$packagePids = New-PidSet
foreach ($line in $lines) {
    $linePid = Get-LogcatPid $line
    if ($linePid -ne $null -and $line -match "\s+xenia\s+:") {
        $xeniaPids[$linePid] = $true
    }
    if ($line -match "Start proc (?<pid>\d+):$packagePattern" -or
            $line -match "pid:\s*(?<pid>\d+).+>>>\s*$packagePattern") {
        $packagePids[[int]$Matches["pid"]] = $true
    }
    if ($linePid -ne $null -and
            ($line -match "Cmdline:\s*$packagePattern" -or
            $line -match "Process:\s*$packagePattern")) {
        $packagePids[$linePid] = $true
    }
}

$scopedCrashPids = New-PidSet
foreach ($key in $xeniaPids.Keys) {
    $scopedCrashPids[$key] = $true
}
foreach ($key in $packagePids.Keys) {
    $scopedCrashPids[$key] = $true
}

$titleId = First-MatchValue $lines "Title ID:\s*(?<value>[0-9A-Fa-f]{8})"
$mediaId = First-MatchValue $lines "Media ID:\s*(?<value>[0-9A-Fa-f]{8})"
$rtlException = First-MatchValue $lines "RtlRaiseException\((?<value>.*)\)"
$crashPc = First-MatchValue $lines "PC:\s*0x(?<value>[0-9A-Fa-f]{8})"
$crashFunction = First-MatchValue $lines "Function:\s*(?<value>0x[0-9A-Fa-f]{8}-0x[0-9A-Fa-f]{8})"
$nativeAbortMessage = First-MatchValue $lines "Abort message:\s*'(?<value>.*)'"
$crashThread = First-MatchValue $lines "name:\s*(?<value>[^>]+)\s*>>>"
$baseHeapReleaseFirst = First-MatchLine $lines "BaseHeap::Release failed"
$physicalHeapReleaseFirst = First-MatchLine $lines "PhysicalHeap::Release failed"
$mmFreePhysicalFirst = First-MatchLine $lines "MmFreePhysicalMemory failed"
$baseHeapReleaseAddress = First-MatchValue $lines "BaseHeap::Release failed .* address (?<value>[0-9A-Fa-f]{8})"
$physicalHeapReleaseAddress = First-MatchValue $lines "PhysicalHeap::Release failed .*physical_address=(?<value>[0-9A-Fa-f]{8})"
$physicalHeapParentAddress = First-MatchValue $lines "PhysicalHeap::Release failed .*parent_address=(?<value>[0-9A-Fa-f]{8})"
$launchModuleCount = Count-Matches $lines "Launching module"
$androidRuntimeTotalCount = Count-Matches $lines "AndroidRuntime|FATAL EXCEPTION"
$nativeSignalTotalCount = Count-Matches $lines "signal [0-9]+|SIGSEGV|SIGABRT|tombstone|backtrace:"
$androidRuntimeCount = Count-ScopedMatches $lines "AndroidRuntime|FATAL EXCEPTION" $scopedCrashPids $packagePattern
$nativeSignalCount = Count-ScopedMatches $lines "Fatal signal|signal [0-9]+|SIGSEGV|SIGABRT|tombstone|backtrace:|Abort message:|>>> $packagePattern" $scopedCrashPids $packagePattern
$guestCrashCount = Count-Matches $lines "==== CRASH DUMP ====|The guest has crashed|Xenia has now paused itself"
$baseHeapReleaseCount = Count-Matches $lines "BaseHeap::Release failed"
$baseHeapAllocCount = Count-Matches $lines "BaseHeap::Alloc page count too big|BaseHeap::Alloc failed"
$physicalHeapCount = Count-Matches $lines "PhysicalHeap::Release failed|PhysicalHeap::Alloc unable"
$xeniaLineCount = Count-Matches $lines "xenia"
$vulkanSwapchainCount = Count-Matches $lines "VulkanPresenter: Created .* swapchain|swapchain"

$classification = "unknown"
$reason = "no decisive markers"

if ($androidRuntimeCount -gt 0 -or $nativeSignalCount -gt 0) {
    $classification = "android_or_native_process_crash"
    $reason = "AndroidRuntime, signal, tombstone, or native backtrace marker present"
}

if ($guestCrashCount -gt 0 -or $crashPc) {
    $classification = "xenia_guest_crash"
    $reason = "Xenia guest crash dump or guest PC marker present"
}

if ($rtlException -and ($baseHeapReleaseCount -gt 0 -or $baseHeapAllocCount -gt 0 -or $physicalHeapCount -gt 0)) {
    $classification = "guest_heap_rtlraiseexception"
    $reason = "BaseHeap/PhysicalHeap markers align with RtlRaiseException"
}

if ($classification -eq "android_or_native_process_crash" -and
        ($baseHeapReleaseCount -gt 0 -or $baseHeapAllocCount -gt 0 -or $physicalHeapCount -gt 0)) {
    $classification = "guest_heap_native_abort"
    $reason = "BaseHeap/PhysicalHeap failure aligns with native process abort"
}

if ($titleId -eq "535107D4" -and $classification -eq "guest_heap_rtlraiseexception") {
    $classification = "project_sylpheed_guest_heap_rtlraiseexception"
    $reason = "Project Sylpheed title ID plus heap failure and RtlRaiseException"
}

if ($titleId -eq "535107D4" -and $classification -eq "guest_heap_native_abort") {
    $classification = "project_sylpheed_heap_native_abort"
    $reason = "Project Sylpheed title ID plus BaseHeap/PhysicalHeap failure and native abort"
}

if ($titleId -eq "535107D4" -and $classification -eq "unknown" -and
        ($baseHeapReleaseCount -gt 0 -or $baseHeapAllocCount -gt 0 -or $physicalHeapCount -gt 0)) {
    $classification = "project_sylpheed_heap_release_failures_no_crash_marker"
    $reason = "Project Sylpheed heap release failures present, but no Xenia package crash marker was found"
}

if ($classification -eq "unknown" -and $launchModuleCount -gt 0) {
    $classification = "launched_no_crash_marker"
    $reason = "module launched but no crash marker was found"
}

if ($classification -eq "unknown" -and $xeniaLineCount -eq 0) {
    $classification = "no_xenia_runtime_evidence"
    $reason = "no Xenia log lines found"
}

$report = @(
    "classification=$classification",
    "reason=$reason",
    "log_path=$LogPath",
    "title_id=$titleId",
    "media_id=$mediaId",
    "launch_module_count=$launchModuleCount",
    "xenia_pid_count=$($xeniaPids.Count)",
    "package_pid_count=$($packagePids.Count)",
    "android_runtime_count=$androidRuntimeCount",
    "android_runtime_total_count=$androidRuntimeTotalCount",
    "native_signal_count=$nativeSignalCount",
    "native_signal_total_count=$nativeSignalTotalCount",
    "guest_crash_count=$guestCrashCount",
    "rtl_raise_exception=$rtlException",
    "native_abort_message=$nativeAbortMessage",
    "crash_thread=$crashThread",
    "crash_pc=$crashPc",
    "crash_function=$crashFunction",
    "base_heap_release_first=$baseHeapReleaseFirst",
    "base_heap_release_address=$baseHeapReleaseAddress",
    "base_heap_release_count=$baseHeapReleaseCount",
    "base_heap_alloc_count=$baseHeapAllocCount",
    "physical_heap_release_first=$physicalHeapReleaseFirst",
    "physical_heap_release_address=$physicalHeapReleaseAddress",
    "physical_heap_parent_address=$physicalHeapParentAddress",
    "physical_heap_count=$physicalHeapCount",
    "mm_free_physical_first=$mmFreePhysicalFirst",
    "vulkan_swapchain_count=$vulkanSwapchainCount"
)

if ($OutPath) {
    $parent = Split-Path -Parent $OutPath
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $report | Out-File -Encoding utf8 -FilePath $OutPath
}

$report
