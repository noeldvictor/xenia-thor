param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$DeviceSerial = "",
    [ValidateSet(
        "DeviceInfo",
        "FindContent",
        "Install",
        "LaunchLauncher",
        "LaunchWindowDemo",
        "LaunchEmulator",
        "LaunchBlueDragon",
        "LaunchBlueDragonLiveCapture",
        "StopNoise",
        "Capture")]
    [string]$Mode = "DeviceInfo",
    [string]$OutDir = "",
    [string]$Target = "",
    [string]$TimeScalar = "",
    [string]$Arm64MiniJit = "true",
    [string]$HidNopConnected = "false",
    [string]$HidNopButtons = "",
    [string]$HidNopButtonsDelayMs = "",
    [string]$HidNopButtonsHoldMs = "",
    [string]$RenderTargetPathVulkan = "",
    [string]$BreakOnDebugbreak = "",
    [string]$DisassembleFunctions = "false",
    [string]$DisassembleFunctionFilter = "",
    [string]$MountCache = "false",
    [string]$MmapAddressHigh = "",
    [string]$ClearMemoryPageState = "false",
    [string]$EmitInlineMmioChecks = "false",
    [string]$GpuInterruptOnRingIdle = "false",
    [string]$GpuInterruptOnSwap = "false",
    [string]$GpuTraceInterrupts = "false",
    [string]$GpuBlueDragonKickWaitToken = "false",
    [string]$GpuTraceSwap = "false",
    [string]$GpuTraceSwapFrontbufferChecksum = "false",
    [string]$GpuTraceSwapRenderTargets = "false",
    [string]$VulkanTraceResolve = "false",
    [string]$VulkanTraceResolveChecksum = "false",
    [string]$VulkanTraceEdramChecksum = "false",
    [string]$VulkanReadbackResolve = "false",
    [string]$VulkanTraceCopyState = "false",
    [string]$VulkanTraceDrawState = "false",
    [string]$VulkanTraceSwapSharedMemoryChecksum = "false",
    [string]$VulkanPresentRecentResolveOnSwap = "false",
    [string]$VulkanPresentScoredResolveOnSwap = "false",
    [string]$VulkanPresentScoredResolveRejectClearLike = "false",
    [string]$VulkanPresentForcedResolveOnSwap = "false",
    [string]$VulkanDebugSolidGuestOutput = "false",
    [string]$VulkanForceSigned2101010UnormFallback = "false",
    [string]$GpuEarlyPrimaryReadPointerWriteback = "false",
    [string]$GpuBlueDragonKickWaitTokenBudget = "",
    [string]$GpuTraceInterruptsBudget = "",
    [string]$GpuTracePacketBudget = "",
    [string]$GpuTraceSwapFrontbufferChecksumBudget = "",
    [string]$GpuTraceSwapRenderTargetsBudget = "",
    [string]$VulkanTraceResolveBudget = "",
    [string]$VulkanTraceResolveChecksumBudget = "",
    [string]$VulkanTraceEdramChecksumBudget = "",
    [string]$VulkanTraceCopyStateBudget = "",
    [string]$VulkanTraceDrawStateBudget = "",
    [string]$VulkanTraceSwapSharedMemoryChecksumBudget = "",
    [string]$VulkanPresentScoredResolveMinWidth = "",
    [string]$VulkanPresentScoredResolveMinHeight = "",
    [string]$VulkanPresentScoredResolveBudget = "",
    [string]$VulkanPresentScoredResolveRequiredFormat = "",
    [string]$VulkanPresentForcedResolveAddress = "",
    [string]$VulkanPresentForcedResolveLength = "",
    [string]$VulkanPresentForcedResolveWidth = "",
    [string]$VulkanPresentForcedResolveHeight = "",
    [string]$VulkanPresentForcedResolvePitch = "",
    [string]$VulkanPresentForcedResolveFormat = "",
    [string]$Arm64MiniJitBlacklist = "",
    [string]$Arm64ForceInterpreterRanges = "",
    [string]$Arm64GuestStoreWatch = "",
    [string]$Arm64GuestStoreWatchBudget = "",
    [string]$Arm64CompiledCallTraceInterval = "",
    [string]$Arm64CompiledCallTraceMinCount = "",
    [string]$Arm64CompiledCallTraceBudget = "",
    [string]$Arm64CompiledCallTraceFunctions = "",
    [string]$Arm64CompiledCallTraceGuestTids = "",
    [string]$Arm64CompiledCallTraceAfterMs = "",
    [string]$Arm64BlueDragonDrawWaitProbe = "false",
    [string]$XboxkrnlThreadWaitTrace = "false",
    [string]$XboxkrnlThreadWaitTraceBudget = "",
    [string]$XboxkrnlThreadWaitTraceAfterMs = "",
    [string]$XboxkrnlThreadWaitTraceGuestTids = "",
    [string]$XboxkrnlEventTrace = "false",
    [string]$XboxkrnlEventTraceBudget = "",
    [string]$XboxkrnlEventTraceObjects = "",
    [string]$XboxkrnlIgnoreGuestDebugBreakpoints = "false",
    [int]$LiveCaptureSeconds = 75,
    [string[]]$NoisePackages = @("net.rpcsx.easy"),
    [string]$LogFilter = "xenia|Vulkan|Adreno|AndroidRuntime|FATAL|crash|tombstone|signal|backtrace"
)

$ErrorActionPreference = "Stop"

$script:AdbEvents = New-Object System.Collections.Generic.List[string]
$script:LastAdbExitCode = 0

function Add-AdbEvent {
    param([string]$Message)
    $script:AdbEvents.Add("$(Get-Date -Format o) $Message")
}

function Invoke-AdbRaw {
    param(
        [string[]]$Arguments,
        [switch]$UseSerial
    )

    $adbArguments = @()
    if ($UseSerial -and $DeviceSerial) {
        $adbArguments += @("-s", $DeviceSerial)
    }
    $adbArguments += $Arguments

    $output = & adb @adbArguments 2>&1
    $script:LastAdbExitCode = $LASTEXITCODE
    return $output
}

function Get-AdbDeviceState {
    if (!$DeviceSerial) {
        return ""
    }

    $escapedSerial = [regex]::Escape($DeviceSerial)
    $devices = Invoke-AdbRaw @("devices", "-l")
    foreach ($line in $devices) {
        if ($line -match "^\s*$escapedSerial\s+(\S+)") {
            return $Matches[1]
        }
    }
    return "missing"
}

function Wait-AdbDeviceState {
    param(
        [string]$ExpectedState = "device",
        [int]$TimeoutSeconds = 20
    )

    if (!$DeviceSerial) {
        return $true
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $state = Get-AdbDeviceState
        if ($state -eq $ExpectedState) {
            return $true
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    return $false
}

function Repair-AdbDevice {
    param([string]$Reason)

    if (!$DeviceSerial) {
        return
    }

    Add-AdbEvent "ADB repair requested: $Reason"
    $state = Get-AdbDeviceState
    Add-AdbEvent "ADB state before repair: $state"

    if ($state -eq "unauthorized") {
        throw "ADB device $DeviceSerial is unauthorized. Check the authorization prompt on the Thor."
    }

    if ($state -eq "offline") {
        Invoke-AdbRaw @("reconnect", "offline") | Out-Null
        if (Wait-AdbDeviceState -TimeoutSeconds 15) {
            Add-AdbEvent "ADB recovered via reconnect offline"
            return
        }
    }

    Invoke-AdbRaw @("reconnect", "device") | Out-Null
    if (Wait-AdbDeviceState -TimeoutSeconds 15) {
        Add-AdbEvent "ADB recovered via reconnect device"
        return
    }

    Invoke-AdbRaw @("kill-server") | Out-Null
    Invoke-AdbRaw @("start-server") | Out-Null
    if (Wait-AdbDeviceState -TimeoutSeconds 25) {
        Add-AdbEvent "ADB recovered via kill-server/start-server"
        return
    }

    $finalState = Get-AdbDeviceState
    Add-AdbEvent "ADB repair failed, final state: $finalState"
}

function Test-AdbTransportFailure {
    param([object[]]$Output)

    $text = ($Output | Out-String)
    return $text -match "(?i)(device .*offline|device .*not found|no devices/emulators found|more than one device/emulator|failed to get feature set|protocol fault)"
}

function Ensure-AdbDevice {
    if (!$DeviceSerial) {
        return
    }

    $state = Get-AdbDeviceState
    if ($state -eq "device") {
        return
    }
    Repair-AdbDevice "pre-command state was $state"
}

function Invoke-Adb {
    param([string[]]$Arguments)

    $command = if ($Arguments.Count -gt 0) { $Arguments[0] } else { "" }
    $skipEnsure = $command -in @("devices", "help", "version", "kill-server", "start-server", "reconnect")
    if (!$skipEnsure) {
        Ensure-AdbDevice
    }

    $output = Invoke-AdbRaw -UseSerial:$(!$skipEnsure) $Arguments
    if (Test-AdbTransportFailure $output) {
        Repair-AdbDevice "command failed: adb $($Arguments -join ' ')"
        $output = Invoke-AdbRaw -UseSerial:$(!$skipEnsure) $Arguments
    }
    return $output
}

function Invoke-AdbExecOutToFile {
    param(
        [string]$Command,
        [string]$OutputPath
    )

    Ensure-AdbDevice
    $serialPart = ""
    if ($DeviceSerial) {
        $serialPart = "-s $DeviceSerial "
    }

    $cmdLine = "adb ${serialPart}exec-out $Command > `"$OutputPath`""
    cmd /c $cmdLine
    if ($LASTEXITCODE -ne 0) {
        Repair-AdbDevice "exec-out failed: $Command"
        cmd /c $cmdLine
    }
    $script:LastAdbExitCode = $LASTEXITCODE
}

function ConvertTo-AdbShellSingleQuote {
    param([string]$Value)
    return "'" + ($Value -replace "'", "'\\''") + "'"
}

function ConvertTo-BooleanText {
    param([string]$Value)
    if ($Value -match "^(?i:false|0|no|off)$") {
        return "false"
    }
    return "true"
}

function ConvertTo-AdbIntText {
    param([string]$Value)
    $trimmed = $Value.Trim()
    if ($trimmed -match "^(?i)0x([0-9a-f]+)$") {
        return [Convert]::ToUInt32($Matches[1], 16).ToString()
    }
    return $trimmed
}

function Invoke-AdbShellCommand {
    param([string]$Command)
    Invoke-Adb @("shell", $Command)
}

function Set-LastLaunchTarget {
    param([string]$Value)
    $Value | Out-File -Encoding utf8 $LastTargetPath
}

function Find-BlueDragonTarget {
    $findCommand = @"
for root in /sdcard/roms/xbox360 /storage/*/roms/xbox360; do
  [ -d "`$root" ] || continue
  find "`$root" -maxdepth 4 -type f \( -iname '*Blue*Dragon*Disc*1*.iso' -o -iname '*Blue*Dragon*.iso' -o -iname '*default.xex' \) 2>/dev/null
done | head -20
"@
    $candidates = Invoke-AdbShellCommand $findCommand |
        Where-Object { $_ -and $_.Trim().Length -gt 0 }
    if ($candidates) {
        return ($candidates | Select-Object -First 1).Trim()
    }
    return ""
}

function Start-XeniaEmulator {
    param(
        [string]$LaunchTarget,
        [switch]$SkipForceStop,
        [switch]$SkipLogcatClear
    )
    if (!$SkipForceStop) {
        Invoke-Adb @("shell", "am", "force-stop", $PackageName) | Out-Null
    }
    if (!$SkipLogcatClear) {
        Invoke-Adb @("logcat", "-c")
    }
    Set-LastLaunchTarget $LaunchTarget
    $component = "$PackageName/$EmulatorActivity"
    $parts = @(
        "am start",
        "-n $(ConvertTo-AdbShellSingleQuote $component)",
        "--es gpu vulkan",
        "--es cpu arm64",
        "--es apu nop",
        "--es hid nop",
        "--ez arm64_enable_mini_jit $(ConvertTo-BooleanText $Arm64MiniJit)",
        "--ez hid_nop_connected $(ConvertTo-BooleanText $HidNopConnected)",
        "--ez disassemble_functions $(ConvertTo-BooleanText $DisassembleFunctions)",
        "--ez mount_cache $(ConvertTo-BooleanText $MountCache)",
        "--ez clear_memory_page_state $(ConvertTo-BooleanText $ClearMemoryPageState)",
        "--ez emit_inline_mmio_checks $(ConvertTo-BooleanText $EmitInlineMmioChecks)",
        "--ez gpu_interrupt_on_ring_idle $(ConvertTo-BooleanText $GpuInterruptOnRingIdle)",
        "--ez gpu_interrupt_on_swap $(ConvertTo-BooleanText $GpuInterruptOnSwap)",
        "--ez gpu_trace_interrupts $(ConvertTo-BooleanText $GpuTraceInterrupts)",
        "--ez gpu_blue_dragon_kick_wait_token $(ConvertTo-BooleanText $GpuBlueDragonKickWaitToken)",
        "--ez gpu_trace_swap $(ConvertTo-BooleanText $GpuTraceSwap)",
        "--ez gpu_trace_swap_frontbuffer_checksum $(ConvertTo-BooleanText $GpuTraceSwapFrontbufferChecksum)",
        "--ez gpu_trace_swap_render_targets $(ConvertTo-BooleanText $GpuTraceSwapRenderTargets)",
        "--ez vulkan_trace_resolve $(ConvertTo-BooleanText $VulkanTraceResolve)",
        "--ez vulkan_trace_resolve_checksum $(ConvertTo-BooleanText $VulkanTraceResolveChecksum)",
        "--ez vulkan_trace_edram_checksum $(ConvertTo-BooleanText $VulkanTraceEdramChecksum)",
        "--ez vulkan_readback_resolve $(ConvertTo-BooleanText $VulkanReadbackResolve)",
        "--ez vulkan_trace_copy_state $(ConvertTo-BooleanText $VulkanTraceCopyState)",
        "--ez vulkan_trace_draw_state $(ConvertTo-BooleanText $VulkanTraceDrawState)",
        "--ez vulkan_trace_swap_shared_memory_checksum $(ConvertTo-BooleanText $VulkanTraceSwapSharedMemoryChecksum)",
        "--ez vulkan_present_recent_resolve_on_swap $(ConvertTo-BooleanText $VulkanPresentRecentResolveOnSwap)",
        "--ez vulkan_present_scored_resolve_on_swap $(ConvertTo-BooleanText $VulkanPresentScoredResolveOnSwap)",
        "--ez vulkan_present_scored_resolve_reject_clear_like $(ConvertTo-BooleanText $VulkanPresentScoredResolveRejectClearLike)",
        "--ez vulkan_present_forced_resolve_on_swap $(ConvertTo-BooleanText $VulkanPresentForcedResolveOnSwap)",
        "--ez vulkan_debug_solid_guest_output $(ConvertTo-BooleanText $VulkanDebugSolidGuestOutput)",
        "--ez vulkan_force_signed_2101010_unorm_fallback $(ConvertTo-BooleanText $VulkanForceSigned2101010UnormFallback)",
        "--ez gpu_early_primary_read_pointer_writeback $(ConvertTo-BooleanText $GpuEarlyPrimaryReadPointerWriteback)",
        "--ez discord false")
    if ($TimeScalar) {
        $parts += "--es time_scalar $(ConvertTo-AdbShellSingleQuote $TimeScalar)"
    }
    if ($RenderTargetPathVulkan) {
        $parts += "--es render_target_path_vulkan $(ConvertTo-AdbShellSingleQuote $RenderTargetPathVulkan)"
    }
    if ($BreakOnDebugbreak -ne "") {
        $parts += "--ez break_on_debugbreak $(ConvertTo-BooleanText $BreakOnDebugbreak)"
    }
    if ($GpuBlueDragonKickWaitTokenBudget) {
        $parts += "--ei gpu_blue_dragon_kick_wait_token_budget $GpuBlueDragonKickWaitTokenBudget"
    }
    if ($GpuTraceInterruptsBudget) {
        $parts += "--ei gpu_trace_interrupts_budget $GpuTraceInterruptsBudget"
    }
    if ($GpuTracePacketBudget) {
        $parts += "--ei gpu_trace_packet_budget $GpuTracePacketBudget"
    }
    if ($GpuTraceSwapFrontbufferChecksumBudget) {
        $parts += "--ei gpu_trace_swap_frontbuffer_checksum_budget $GpuTraceSwapFrontbufferChecksumBudget"
    }
    if ($GpuTraceSwapRenderTargetsBudget) {
        $parts += "--ei gpu_trace_swap_render_targets_budget $GpuTraceSwapRenderTargetsBudget"
    }
    if ($VulkanTraceResolveBudget) {
        $parts += "--ei vulkan_trace_resolve_budget $VulkanTraceResolveBudget"
    }
    if ($VulkanTraceResolveChecksumBudget) {
        $parts += "--ei vulkan_trace_resolve_checksum_budget $VulkanTraceResolveChecksumBudget"
    }
    if ($VulkanTraceEdramChecksumBudget) {
        $parts += "--ei vulkan_trace_edram_checksum_budget $VulkanTraceEdramChecksumBudget"
    }
    if ($VulkanTraceCopyStateBudget) {
        $parts += "--ei vulkan_trace_copy_state_budget $VulkanTraceCopyStateBudget"
    }
    if ($VulkanTraceDrawStateBudget) {
        $parts += "--ei vulkan_trace_draw_state_budget $VulkanTraceDrawStateBudget"
    }
    if ($VulkanTraceSwapSharedMemoryChecksumBudget) {
        $parts += "--ei vulkan_trace_swap_shared_memory_checksum_budget $VulkanTraceSwapSharedMemoryChecksumBudget"
    }
    if ($VulkanPresentScoredResolveMinWidth) {
        $parts += "--ei vulkan_present_scored_resolve_min_width $(ConvertTo-AdbIntText $VulkanPresentScoredResolveMinWidth)"
    }
    if ($VulkanPresentScoredResolveMinHeight) {
        $parts += "--ei vulkan_present_scored_resolve_min_height $(ConvertTo-AdbIntText $VulkanPresentScoredResolveMinHeight)"
    }
    if ($VulkanPresentScoredResolveBudget) {
        $parts += "--ei vulkan_present_scored_resolve_budget $(ConvertTo-AdbIntText $VulkanPresentScoredResolveBudget)"
    }
    if ($VulkanPresentScoredResolveRequiredFormat) {
        $parts += "--ei vulkan_present_scored_resolve_required_format $(ConvertTo-AdbIntText $VulkanPresentScoredResolveRequiredFormat)"
    }
    if ($VulkanPresentForcedResolveAddress) {
        $parts += "--ei vulkan_present_forced_resolve_address $(ConvertTo-AdbIntText $VulkanPresentForcedResolveAddress)"
    }
    if ($VulkanPresentForcedResolveLength) {
        $parts += "--ei vulkan_present_forced_resolve_length $(ConvertTo-AdbIntText $VulkanPresentForcedResolveLength)"
    }
    if ($VulkanPresentForcedResolveWidth) {
        $parts += "--ei vulkan_present_forced_resolve_width $(ConvertTo-AdbIntText $VulkanPresentForcedResolveWidth)"
    }
    if ($VulkanPresentForcedResolveHeight) {
        $parts += "--ei vulkan_present_forced_resolve_height $(ConvertTo-AdbIntText $VulkanPresentForcedResolveHeight)"
    }
    if ($VulkanPresentForcedResolvePitch) {
        $parts += "--ei vulkan_present_forced_resolve_pitch $(ConvertTo-AdbIntText $VulkanPresentForcedResolvePitch)"
    }
    if ($VulkanPresentForcedResolveFormat) {
        $parts += "--ei vulkan_present_forced_resolve_format $(ConvertTo-AdbIntText $VulkanPresentForcedResolveFormat)"
    }
    if ($MmapAddressHigh) {
        $parts += "--ei mmap_address_high $MmapAddressHigh"
    }
    if ($Arm64MiniJitBlacklist) {
        $parts += "--es arm64_mini_jit_blacklist $(ConvertTo-AdbShellSingleQuote $Arm64MiniJitBlacklist)"
    }
    if ($DisassembleFunctionFilter) {
        $parts += "--es disassemble_function_filter $(ConvertTo-AdbShellSingleQuote $DisassembleFunctionFilter)"
    }
    if ($HidNopButtons) {
        $parts += "--es hid_nop_buttons $(ConvertTo-AdbShellSingleQuote $HidNopButtons)"
    }
    if ($HidNopButtonsDelayMs) {
        $parts += "--ei hid_nop_buttons_delay_ms $HidNopButtonsDelayMs"
    }
    if ($HidNopButtonsHoldMs) {
        $parts += "--ei hid_nop_buttons_hold_ms $HidNopButtonsHoldMs"
    }
    if ($Arm64ForceInterpreterRanges) {
        $parts += "--es arm64_force_interpreter_guest_ranges $(ConvertTo-AdbShellSingleQuote $Arm64ForceInterpreterRanges)"
    }
    if ($Arm64GuestStoreWatch) {
        $parts += "--es arm64_guest_store_watch $(ConvertTo-AdbShellSingleQuote $Arm64GuestStoreWatch)"
    }
    if ($Arm64GuestStoreWatchBudget) {
        $parts += "--ei arm64_guest_store_watch_budget $Arm64GuestStoreWatchBudget"
    }
    if ($Arm64CompiledCallTraceInterval) {
        $parts += "--ei arm64_compiled_call_trace_interval $Arm64CompiledCallTraceInterval"
    }
    if ($Arm64CompiledCallTraceMinCount) {
        $parts += "--ei arm64_compiled_call_trace_min_count $Arm64CompiledCallTraceMinCount"
    }
    if ($Arm64CompiledCallTraceBudget) {
        $parts += "--ei arm64_compiled_call_trace_budget $Arm64CompiledCallTraceBudget"
    }
    if ($Arm64CompiledCallTraceFunctions) {
        $parts += "--es arm64_compiled_call_trace_functions $(ConvertTo-AdbShellSingleQuote $Arm64CompiledCallTraceFunctions)"
    }
    if ($Arm64CompiledCallTraceGuestTids) {
        $parts += "--es arm64_compiled_call_trace_guest_tids $(ConvertTo-AdbShellSingleQuote $Arm64CompiledCallTraceGuestTids)"
    }
    if ($Arm64CompiledCallTraceAfterMs) {
        $parts += "--ei arm64_compiled_call_trace_after_ms $Arm64CompiledCallTraceAfterMs"
    }
    if ($Arm64BlueDragonDrawWaitProbe) {
        $parts += "--ez arm64_blue_dragon_draw_wait_probe $(ConvertTo-BooleanText $Arm64BlueDragonDrawWaitProbe)"
    }
    if ($XboxkrnlThreadWaitTrace) {
        $parts += "--ez xboxkrnl_thread_wait_trace $(ConvertTo-BooleanText $XboxkrnlThreadWaitTrace)"
    }
    if ($XboxkrnlThreadWaitTraceBudget) {
        $parts += "--ei xboxkrnl_thread_wait_trace_budget $XboxkrnlThreadWaitTraceBudget"
    }
    if ($XboxkrnlThreadWaitTraceAfterMs) {
        $parts += "--ei xboxkrnl_thread_wait_trace_after_ms $XboxkrnlThreadWaitTraceAfterMs"
    }
    if ($XboxkrnlThreadWaitTraceGuestTids) {
        $parts += "--es xboxkrnl_thread_wait_trace_guest_tids $(ConvertTo-AdbShellSingleQuote $XboxkrnlThreadWaitTraceGuestTids)"
    }
    if ($XboxkrnlEventTrace) {
        $parts += "--ez xboxkrnl_event_trace $(ConvertTo-BooleanText $XboxkrnlEventTrace)"
    }
    if ($XboxkrnlEventTraceBudget) {
        $parts += "--ei xboxkrnl_event_trace_budget $XboxkrnlEventTraceBudget"
    }
    if ($XboxkrnlEventTraceObjects) {
        $parts += "--es xboxkrnl_event_trace_objects $(ConvertTo-AdbShellSingleQuote $XboxkrnlEventTraceObjects)"
    }
    if ($XboxkrnlIgnoreGuestDebugBreakpoints) {
        $parts += "--ez xboxkrnl_ignore_guest_debug_breakpoints $(ConvertTo-BooleanText $XboxkrnlIgnoreGuestDebugBreakpoints)"
    }
    if ($LaunchTarget) {
        $parts += "--es target $(ConvertTo-AdbShellSingleQuote $LaunchTarget)"
    }
    Invoke-AdbShellCommand ($parts -join " ")
    Start-Sleep -Seconds 5
    Invoke-Adb @("shell", "pidof", $PackageName)
}

function Resolve-BlueDragonLaunchTarget {
    $launchTarget = $Target
    if (!$launchTarget) {
        $launchTarget = Find-BlueDragonTarget
    }
    if (!$launchTarget) {
        $launchTarget = $BlueDragonDisc1
    }
    return $launchTarget
}

function Write-CaptureMetadata {
    param(
        [string]$Stamp,
        [string]$MetaPath,
        [string]$CaptureTarget
    )

    $branch = (& git -C $RepoRoot branch --show-current) 2>$null
    $head = (& git -C $RepoRoot rev-parse --short HEAD) 2>$null
    $apkHash = ""
    if (Test-Path $ApkPath) {
        $apkHash = (Get-FileHash -Algorithm SHA256 $ApkPath).Hash
    }
    if (!$CaptureTarget -and (Test-Path $LastTargetPath)) {
        $CaptureTarget = (Get-Content -Raw $LastTargetPath).Trim()
    }
    $deviceState = Get-AdbDeviceState
    $packagePid = (Invoke-Adb @("shell", "pidof", $PackageName)) -join " "
    $focused = (Invoke-AdbShellCommand "dumpsys activity activities | grep -E 'mFocusedApp|mResumedActivity|$PackageName' | head -40") -join "`n"
    @(
        "timestamp=$Stamp",
        "branch=$branch",
        "head=$head",
        "adb_serial=$DeviceSerial",
        "adb_state=$deviceState",
        "package=$PackageName",
        "pid=$packagePid",
        "apk=$ApkPath",
        "apk_sha256=$apkHash",
        "target=$CaptureTarget",
        "live_capture_seconds=$LiveCaptureSeconds",
        "",
        "adb_events:",
        ($script:AdbEvents -join "`n"),
        "",
        "activity:",
        $focused
    ) | Out-File -Encoding utf8 $MetaPath
}

function Write-FilteredLog {
    param(
        [string]$LogPath,
        [string]$FilteredLogPath
    )

    if (!$LogFilter -or !(Test-Path $LogPath)) {
        return
    }
    Get-Content -Path $LogPath |
        Select-String -Pattern $LogFilter |
        Out-File -Encoding utf8 $FilteredLogPath
}

function Stop-LiveLogcat {
    param([object]$Process)

    if ($Process -and !$Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $Process.Id -Timeout 5 -ErrorAction SilentlyContinue
    }
}

if (!$OutDir) {
    $OutDir = Join-Path $RepoRoot "scratch\thor-debug"
}
New-Item -ItemType Directory -Force $OutDir | Out-Null

$PackageName = "jp.xenia.emulator.github.debug"
$LauncherActivity = "jp.xenia.emulator.LauncherActivity"
$EmulatorActivity = "jp.xenia.emulator.EmulatorActivity"
$WindowDemoActivity = "jp.xenia.emulator.WindowDemoActivity"
$BlueDragonDisc1 = "/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso"
$ApkPath = Join-Path $RepoRoot "android\android_studio_project\app\build\outputs\apk\github\debug\app-github-debug.apk"
$LastTargetPath = Join-Path $OutDir "last-target.txt"

switch ($Mode) {
    "DeviceInfo" {
        Invoke-Adb @("devices")
        Invoke-Adb @("shell", "getprop", "ro.product.manufacturer")
        Invoke-Adb @("shell", "getprop", "ro.product.model")
        Invoke-Adb @("shell", "getprop", "ro.product.device")
        Invoke-Adb @("shell", "getprop", "ro.build.version.release")
        Invoke-Adb @("shell", "getprop", "ro.build.version.sdk")
        Invoke-Adb @("shell", "getprop", "ro.board.platform")
        Invoke-Adb @("shell", "wm", "size")
        Invoke-Adb @("shell", "wm", "density")
        Invoke-Adb @("shell", "pm", "list", "features")
    }
    "FindContent" {
        Invoke-AdbShellCommand @"
echo 'Xbox 360 roots:'
for root in /sdcard/roms/xbox360 /storage/*/roms/xbox360; do
  [ -d "`$root" ] && echo "`$root"
done
echo 'Blue Dragon candidates:'
for root in /sdcard/roms/xbox360 /storage/*/roms/xbox360; do
  [ -d "`$root" ] || continue
  find "`$root" -maxdepth 4 -type f \( -iname '*Blue*Dragon*.iso' -o -iname '*default.xex' \) 2>/dev/null
done | head -50
"@
    }
    "Install" {
        if (!(Test-Path $ApkPath)) {
            throw "APK not found: $ApkPath"
        }
        Invoke-Adb @("install", "-r", "-d", $ApkPath)
    }
    "LaunchLauncher" {
        Invoke-Adb @("logcat", "-c")
        Set-LastLaunchTarget "LauncherActivity"
        Invoke-Adb @("shell", "am", "start", "-n", "$PackageName/$LauncherActivity")
        Start-Sleep -Seconds 2
        Invoke-Adb @("shell", "pidof", $PackageName)
    }
    "LaunchWindowDemo" {
        Invoke-Adb @("logcat", "-c")
        Set-LastLaunchTarget "WindowDemoActivity"
        Invoke-Adb @("shell", "am", "start", "-n", "$PackageName/$WindowDemoActivity")
        Start-Sleep -Seconds 5
        Invoke-Adb @("shell", "pidof", $PackageName)
    }
    "LaunchEmulator" {
        Start-XeniaEmulator $Target
    }
    "LaunchBlueDragon" {
        $launchTarget = Resolve-BlueDragonLaunchTarget
        Write-Output "Launching target: $launchTarget"
        Start-XeniaEmulator $launchTarget
    }
    "LaunchBlueDragonLiveCapture" {
        $Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $LogPath = Join-Path $OutDir "$Stamp-live-logcat.txt"
        $LogErrorPath = Join-Path $OutDir "$Stamp-live-logcat.err.txt"
        $FilteredLogPath = Join-Path $OutDir "$Stamp-live-logcat-filtered.txt"
        $MetaPath = Join-Path $OutDir "$Stamp-meta.txt"
        $ScreenshotPath = Join-Path $OutDir "$Stamp-screenshot.png"
        $launchTarget = Resolve-BlueDragonLaunchTarget
        Write-Output "Launching target: $launchTarget"
        Invoke-Adb @("shell", "am", "force-stop", $PackageName) | Out-Null
        Invoke-Adb @("logcat", "-c") | Out-Null

        $adbPath = (Get-Command adb).Source
        $adbArguments = @()
        if ($DeviceSerial) {
            $adbArguments += @("-s", $DeviceSerial)
        }
        $adbArguments += @("logcat", "-v", "time")
        $logcatProcess = Start-Process -FilePath $adbPath -ArgumentList $adbArguments `
            -RedirectStandardOutput $LogPath -RedirectStandardError $LogErrorPath `
            -WindowStyle Hidden -PassThru
        try {
            Start-XeniaEmulator $launchTarget -SkipForceStop -SkipLogcatClear
            Start-Sleep -Seconds $LiveCaptureSeconds
        } finally {
            Stop-LiveLogcat $logcatProcess
        }

        Write-FilteredLog $LogPath $FilteredLogPath
        Write-CaptureMetadata $Stamp $MetaPath $launchTarget
        Invoke-AdbExecOutToFile "screencap -p" $ScreenshotPath
        Write-Output "Log: $LogPath"
        Write-Output "Filtered log: $FilteredLogPath"
        Write-Output "Log errors: $LogErrorPath"
        Write-Output "Meta: $MetaPath"
        Write-Output "Screenshot: $ScreenshotPath"
    }
    "StopNoise" {
        foreach ($package in $NoisePackages) {
            if ($package) {
                Invoke-Adb @("shell", "am", "force-stop", $package)
            }
        }
    }
    "Capture" {
        $Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $LogPath = Join-Path $OutDir "$Stamp-logcat.txt"
        $FilteredLogPath = Join-Path $OutDir "$Stamp-logcat-filtered.txt"
        $MetaPath = Join-Path $OutDir "$Stamp-meta.txt"
        $ScreenshotPath = Join-Path $OutDir "$Stamp-screenshot.png"
        $logcat = Invoke-Adb @("logcat", "-d", "-v", "time")
        $logcat | Out-File -Encoding utf8 $LogPath
        $captureTarget = $Target
        Write-FilteredLog $LogPath $FilteredLogPath
        Write-CaptureMetadata $Stamp $MetaPath $captureTarget
        Invoke-AdbExecOutToFile "screencap -p" $ScreenshotPath
        Write-Output "Log: $LogPath"
        if ($LogFilter) {
            Write-Output "Filtered log: $FilteredLogPath"
        }
        Write-Output "Meta: $MetaPath"
        Write-Output "Screenshot: $ScreenshotPath"
    }
}
