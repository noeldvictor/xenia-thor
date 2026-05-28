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
$androidRuntimeCount = Count-Matches $lines "AndroidRuntime|FATAL EXCEPTION"
$nativeSignalCount = Count-Matches $lines "signal [0-9]+|SIGSEGV|SIGABRT|tombstone|backtrace:"
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

if ($titleId -eq "535107D4" -and $classification -eq "guest_heap_rtlraiseexception") {
    $classification = "project_sylpheed_guest_heap_rtlraiseexception"
    $reason = "Project Sylpheed title ID plus heap failure and RtlRaiseException"
}

if ($titleId -eq "535107D4" -and $classification -eq "android_or_native_process_crash" -and
        ($baseHeapReleaseCount -gt 0 -or $baseHeapAllocCount -gt 0 -or $physicalHeapCount -gt 0)) {
    $classification = "project_sylpheed_heap_native_abort"
    $reason = "Project Sylpheed title ID plus BaseHeap/PhysicalHeap failure and native abort"
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
    "android_runtime_count=$androidRuntimeCount",
    "native_signal_count=$nativeSignalCount",
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
