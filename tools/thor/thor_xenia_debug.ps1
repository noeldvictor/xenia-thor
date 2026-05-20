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
        "LaunchBlueDragonSpeedCapture",
        "StopNoise",
        "Capture")]
    [string]$Mode = "DeviceInfo",
    [string]$OutDir = "",
    [string]$Target = "",
    [string]$TimeScalar = "",
    [string]$Arm64MiniJit = "true",
    [string]$LogLevel = "",
    [string]$XmaTraceContextState = "false",
    [string]$XmaFastSilence = "false",
    [string]$HidNopConnected = "false",
    [string]$HidNopButtons = "",
    [string]$HidNopButtonSequence = "",
    [string]$HidNopButtonsDelayMs = "",
    [string]$HidNopButtonsHoldMs = "",
    [string]$HideAndroidOsd = "false",
    [string]$DumpShaders = "",
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
    [string]$GpuTraceTextureCacheActions = "false",
    [string]$GpuUnknownRegisterLogBudget = "",
    [string]$XboxkrnlNtCreateFileFailLogBudget = "",
    [string]$GpuTraceSwapFrontbufferChecksum = "false",
    [string]$GpuTraceSwapRenderTargets = "false",
    [string]$VulkanTraceResolve = "false",
    [string]$VulkanTraceResolveChecksum = "false",
    [string]$VulkanTraceEdramChecksum = "false",
    [string]$VulkanReadbackResolve = "false",
    [string]$VulkanTraceCopyState = "false",
    [string]$VulkanTraceDrawState = "false",
    [string]$VulkanTraceDrawShaderFilter = "",
    [string]$VulkanTracePipelineCreation = "false",
    [string]$VulkanTraceShaderConstants = "false",
    [string]$VulkanTraceShaderConstantsShaderFilter = "",
    [string]$VulkanTraceTextureSourceChecksum = "false",
    [string]$VulkanTraceTextureSourceShaderFilter = "",
    [string]$VulkanTraceVertexFetchChecksum = "false",
    [string]$VulkanTraceVertexFetchShaderFilter = "",
    [string]$VulkanTraceSwapSharedMemoryChecksum = "false",
    [string]$VulkanPresentRecentResolveOnSwap = "false",
    [string]$VulkanPresentScoredResolveOnSwap = "false",
    [string]$VulkanPresentScoredResolveRejectClearLike = "false",
    [string]$VulkanPresentForcedResolveOnSwap = "false",
    [string]$VulkanDebugSolidGuestOutput = "false",
    [string]$VulkanDebugPixelShaderOutputFilter = "",
    [string]$VulkanDebugPixelShaderOutputMode = "",
    [string]$VulkanDebugPixelShaderOutputSecondaryFilter = "",
    [string]$VulkanDebugPixelShaderOutputSecondaryMode = "",
    [string]$VulkanForce2101010Rgba8Fallback = "false",
    [string]$VulkanForceSigned2101010UnormFallback = "false",
    [string]$VulkanDebugTextureFetchDisableExpAdjust = "false",
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
    [string]$VulkanTraceShaderConstantsBudget = "",
    [string]$VulkanTraceTextureSourceChecksumBudget = "",
    [string]$VulkanTraceVertexFetchChecksumBudget = "",
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
    [string]$Arm64SpeedProfileIntervalMs = "",
    [string]$Arm64SpeedProfileTopFunctions = "",
    [string]$Arm64SpeedProfileMinDelta = "",
    [string]$A64InlineGprLrHelpers = "",
    [string]$A64InlineFprHelpers = "true",
    [string]$A64InlineVmxHelpers = "true",
    [string]$A64InlineFprVmxHelpers = "",
    [string]$A64InlinePpcThreadFieldLeafHelpers = "",
    [string]$Arm64BlueDragonDrawWaitProbe = "false",
    [string]$Arm64BlueDragonDrawWaitProbeStride = "",
    [string]$Arm64BlueDragonDrawWaitInlineTickStep = "",
    [string]$Arm64BlueDragonDrawWaitFastpath = "false",
    [string]$Arm64BlueDragonDrawWaitFastpathHostCounterTime = "false",
    [string]$Arm64BlueDragonDrawWaitFastpathNativeYieldStride = "",
    [string]$Arm64BlueDragonDrawWaitFastpathNativeSleepUs = "",
    [string]$Arm64BlueDragonDrawWaitFastpathTimeoutMs = "",
    [string]$Arm64BlueDragonDrawWaitInlineInCaller = "false",
    [string]$Arm64BlueDragonDrawWaitCallerProfile = "false",
    [string]$Arm64BlueDragonDrawWaitCallerProfileStride = "",
    [string]$Arm64BlueDragonDrawWaitCallerProfileBudget = "",
    [string]$XboxkrnlThreadWaitTrace = "false",
    [string]$XboxkrnlThreadWaitTraceBudget = "",
    [string]$XboxkrnlThreadWaitTraceAfterMs = "",
    [string]$XboxkrnlThreadWaitTraceGuestTids = "",
    [string]$XboxkrnlEventTrace = "false",
    [string]$XboxkrnlEventTraceBudget = "",
    [string]$XboxkrnlEventTraceObjects = "",
    [string]$XboxkrnlIgnoreGuestDebugBreakpoints = "false",
    [int]$LiveCaptureSeconds = 75,
    [string]$PerfSampleSeconds = "60,120",
    [int]$PerfTopThreadCount = 80,
    [string[]]$NoisePackages = @("net.rpcsx.easy"),
    [string]$LogFilter = "xenia|Vulkan|Adreno|AndroidRuntime|FATAL|crash|tombstone|signal|backtrace"
)

$ErrorActionPreference = "Stop"

$script:AdbEvents = New-Object System.Collections.Generic.List[string]
$script:LastAdbExitCode = 0
$script:ActiveDumpShadersPath = ""

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

function Set-ActiveShaderDumpPath {
    param([string]$Stamp = "")

    $script:ActiveDumpShadersPath = ""
    if (!$DumpShaders) {
        return
    }

    if ($DumpShaders -eq "auto") {
        if (!$Stamp) {
            $Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        }
        $script:ActiveDumpShadersPath = "/sdcard/Android/data/$PackageName/files/shader-dumps/$Stamp"
    } else {
        $script:ActiveDumpShadersPath = $DumpShaders
    }

    Invoke-Adb @("shell", "rm", "-rf", $script:ActiveDumpShadersPath) | Out-Null
    Invoke-Adb @("shell", "mkdir", "-p", $script:ActiveDumpShadersPath) | Out-Null
    Add-AdbEvent "shader dump path: $script:ActiveDumpShadersPath"
}

function Pull-ActiveShaderDumps {
    param(
        [string]$Stamp,
        [string]$OutDir
    )

    if (!$script:ActiveDumpShadersPath) {
        return ""
    }

    $localDumpPath = Join-Path $OutDir "$Stamp-shader-dumps"
    if (Test-Path $localDumpPath) {
        Remove-Item -LiteralPath $localDumpPath -Recurse -Force
    }

    Ensure-AdbDevice
    $adbArguments = @()
    if ($DeviceSerial) {
        $adbArguments += @("-s", $DeviceSerial)
    }
    $adbArguments += @("pull", $script:ActiveDumpShadersPath, $localDumpPath)
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $pullOutput = & adb @adbArguments 2>&1
        $script:LastAdbExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($script:LastAdbExitCode -ne 0) {
        throw "adb pull shader dumps failed: $($pullOutput -join "`n")"
    }
    Add-AdbEvent "shader dumps pulled to: $localDumpPath"
    return $localDumpPath
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
    if ($DumpShaders -and !$script:ActiveDumpShadersPath) {
        Set-ActiveShaderDumpPath
    }
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
        "--ez xma_trace_context_state $(ConvertTo-BooleanText $XmaTraceContextState)",
        "--ez xma_fast_silence $(ConvertTo-BooleanText $XmaFastSilence)",
        "--es hid nop",
        "--ez arm64_enable_mini_jit $(ConvertTo-BooleanText $Arm64MiniJit)",
        "--ez hid_nop_connected $(ConvertTo-BooleanText $HidNopConnected)",
        "--ez android_hide_osd $(ConvertTo-BooleanText $HideAndroidOsd)",
        "--ez disassemble_functions $(ConvertTo-BooleanText $DisassembleFunctions)",
        "--ez mount_cache $(ConvertTo-BooleanText $MountCache)",
        "--ez clear_memory_page_state $(ConvertTo-BooleanText $ClearMemoryPageState)",
        "--ez emit_inline_mmio_checks $(ConvertTo-BooleanText $EmitInlineMmioChecks)",
        "--ez gpu_interrupt_on_ring_idle $(ConvertTo-BooleanText $GpuInterruptOnRingIdle)",
        "--ez gpu_interrupt_on_swap $(ConvertTo-BooleanText $GpuInterruptOnSwap)",
        "--ez gpu_trace_interrupts $(ConvertTo-BooleanText $GpuTraceInterrupts)",
        "--ez gpu_blue_dragon_kick_wait_token $(ConvertTo-BooleanText $GpuBlueDragonKickWaitToken)",
        "--ez gpu_trace_swap $(ConvertTo-BooleanText $GpuTraceSwap)",
        "--ez gpu_trace_texture_cache_actions $(ConvertTo-BooleanText $GpuTraceTextureCacheActions)",
        "--ez gpu_trace_swap_frontbuffer_checksum $(ConvertTo-BooleanText $GpuTraceSwapFrontbufferChecksum)",
        "--ez gpu_trace_swap_render_targets $(ConvertTo-BooleanText $GpuTraceSwapRenderTargets)",
        "--ez vulkan_trace_resolve $(ConvertTo-BooleanText $VulkanTraceResolve)",
        "--ez vulkan_trace_resolve_checksum $(ConvertTo-BooleanText $VulkanTraceResolveChecksum)",
        "--ez vulkan_trace_edram_checksum $(ConvertTo-BooleanText $VulkanTraceEdramChecksum)",
        "--ez vulkan_readback_resolve $(ConvertTo-BooleanText $VulkanReadbackResolve)",
        "--ez vulkan_trace_copy_state $(ConvertTo-BooleanText $VulkanTraceCopyState)",
        "--ez vulkan_trace_draw_state $(ConvertTo-BooleanText $VulkanTraceDrawState)",
        "--ez vulkan_trace_pipeline_creation $(ConvertTo-BooleanText $VulkanTracePipelineCreation)",
        "--ez vulkan_trace_shader_constants $(ConvertTo-BooleanText $VulkanTraceShaderConstants)",
        "--ez vulkan_trace_texture_source_checksum $(ConvertTo-BooleanText $VulkanTraceTextureSourceChecksum)",
        "--ez vulkan_trace_vertex_fetch_checksum $(ConvertTo-BooleanText $VulkanTraceVertexFetchChecksum)",
        "--ez vulkan_trace_swap_shared_memory_checksum $(ConvertTo-BooleanText $VulkanTraceSwapSharedMemoryChecksum)",
        "--ez vulkan_present_recent_resolve_on_swap $(ConvertTo-BooleanText $VulkanPresentRecentResolveOnSwap)",
        "--ez vulkan_present_scored_resolve_on_swap $(ConvertTo-BooleanText $VulkanPresentScoredResolveOnSwap)",
        "--ez vulkan_present_scored_resolve_reject_clear_like $(ConvertTo-BooleanText $VulkanPresentScoredResolveRejectClearLike)",
        "--ez vulkan_present_forced_resolve_on_swap $(ConvertTo-BooleanText $VulkanPresentForcedResolveOnSwap)",
        "--ez vulkan_debug_solid_guest_output $(ConvertTo-BooleanText $VulkanDebugSolidGuestOutput)",
        "--ez vulkan_force_2101010_rgba8_fallback $(ConvertTo-BooleanText $VulkanForce2101010Rgba8Fallback)",
        "--ez vulkan_force_signed_2101010_unorm_fallback $(ConvertTo-BooleanText $VulkanForceSigned2101010UnormFallback)",
        "--ez vulkan_debug_texture_fetch_disable_exp_adjust $(ConvertTo-BooleanText $VulkanDebugTextureFetchDisableExpAdjust)",
        "--ez gpu_early_primary_read_pointer_writeback $(ConvertTo-BooleanText $GpuEarlyPrimaryReadPointerWriteback)",
        "--ez discord false")
    if ($TimeScalar) {
        $parts += "--es time_scalar $(ConvertTo-AdbShellSingleQuote $TimeScalar)"
    }
    if ($LogLevel) {
        $parts += "--ei log_level $(ConvertTo-AdbIntText $LogLevel)"
    }
    if ($RenderTargetPathVulkan) {
        $parts += "--es render_target_path_vulkan $(ConvertTo-AdbShellSingleQuote $RenderTargetPathVulkan)"
    }
    if ($script:ActiveDumpShadersPath) {
        $parts += "--es dump_shaders $(ConvertTo-AdbShellSingleQuote $script:ActiveDumpShadersPath)"
    }
    if ($VulkanTraceDrawShaderFilter) {
        $parts += "--es vulkan_trace_draw_shader_filter $(ConvertTo-AdbShellSingleQuote $VulkanTraceDrawShaderFilter)"
    }
    if ($VulkanTraceShaderConstantsShaderFilter) {
        $parts += "--es vulkan_trace_shader_constants_shader_filter $(ConvertTo-AdbShellSingleQuote $VulkanTraceShaderConstantsShaderFilter)"
    }
    if ($VulkanTraceTextureSourceShaderFilter) {
        $parts += "--es vulkan_trace_texture_source_shader_filter $(ConvertTo-AdbShellSingleQuote $VulkanTraceTextureSourceShaderFilter)"
    }
    if ($VulkanTraceVertexFetchShaderFilter) {
        $parts += "--es vulkan_trace_vertex_fetch_shader_filter $(ConvertTo-AdbShellSingleQuote $VulkanTraceVertexFetchShaderFilter)"
    }
    if ($VulkanDebugPixelShaderOutputFilter) {
        $parts += "--es vulkan_debug_pixel_shader_output_filter $(ConvertTo-AdbShellSingleQuote $VulkanDebugPixelShaderOutputFilter)"
    }
    if ($VulkanDebugPixelShaderOutputSecondaryFilter) {
        $parts += "--es vulkan_debug_pixel_shader_output_secondary_filter $(ConvertTo-AdbShellSingleQuote $VulkanDebugPixelShaderOutputSecondaryFilter)"
    }
    if ($BreakOnDebugbreak -ne "") {
        $parts += "--ez break_on_debugbreak $(ConvertTo-BooleanText $BreakOnDebugbreak)"
    }
    if ($GpuBlueDragonKickWaitTokenBudget) {
        $parts += "--ei gpu_blue_dragon_kick_wait_token_budget $GpuBlueDragonKickWaitTokenBudget"
    }
    if ($GpuUnknownRegisterLogBudget) {
        $parts += "--ei gpu_unknown_register_log_budget $GpuUnknownRegisterLogBudget"
    }
    if ($XboxkrnlNtCreateFileFailLogBudget) {
        $parts += "--ei xboxkrnl_nt_create_file_fail_log_budget $XboxkrnlNtCreateFileFailLogBudget"
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
    if ($VulkanTraceShaderConstantsBudget) {
        $parts += "--ei vulkan_trace_shader_constants_budget $VulkanTraceShaderConstantsBudget"
    }
    if ($VulkanTraceTextureSourceChecksumBudget) {
        $parts += "--ei vulkan_trace_texture_source_checksum_budget $VulkanTraceTextureSourceChecksumBudget"
    }
    if ($VulkanTraceVertexFetchChecksumBudget) {
        $parts += "--ei vulkan_trace_vertex_fetch_checksum_budget $VulkanTraceVertexFetchChecksumBudget"
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
    if ($VulkanDebugPixelShaderOutputMode) {
        $parts += "--ei vulkan_debug_pixel_shader_output_mode $(ConvertTo-AdbIntText $VulkanDebugPixelShaderOutputMode)"
    }
    if ($VulkanDebugPixelShaderOutputSecondaryMode) {
        $parts += "--ei vulkan_debug_pixel_shader_output_secondary_mode $(ConvertTo-AdbIntText $VulkanDebugPixelShaderOutputSecondaryMode)"
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
    if ($HidNopButtonSequence) {
        $parts += "--es hid_nop_button_sequence $(ConvertTo-AdbShellSingleQuote $HidNopButtonSequence)"
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
    if ($Arm64SpeedProfileIntervalMs) {
        $parts += "--ei arm64_speed_profile_interval_ms $Arm64SpeedProfileIntervalMs"
    }
    if ($Arm64SpeedProfileTopFunctions) {
        $parts += "--ei arm64_speed_profile_top_functions $Arm64SpeedProfileTopFunctions"
    }
    if ($Arm64SpeedProfileMinDelta) {
        $parts += "--ei arm64_speed_profile_min_delta $Arm64SpeedProfileMinDelta"
    }
    if ($A64InlineGprLrHelpers) {
        $parts += "--ez a64_inline_gprlr_helpers $(ConvertTo-BooleanText $A64InlineGprLrHelpers)"
    }
    $effectiveA64InlineFprHelpers = $A64InlineFprHelpers
    $effectiveA64InlineVmxHelpers = $A64InlineVmxHelpers
    if ($A64InlineFprVmxHelpers) {
        if (-not $effectiveA64InlineFprHelpers) {
            $effectiveA64InlineFprHelpers = $A64InlineFprVmxHelpers
        }
        if (-not $effectiveA64InlineVmxHelpers) {
            $effectiveA64InlineVmxHelpers = $A64InlineFprVmxHelpers
        }
    }
    if ($effectiveA64InlineFprHelpers) {
        $parts += "--ez a64_inline_fpr_helpers $(ConvertTo-BooleanText $effectiveA64InlineFprHelpers)"
    }
    if ($effectiveA64InlineVmxHelpers) {
        $parts += "--ez a64_inline_vmx_helpers $(ConvertTo-BooleanText $effectiveA64InlineVmxHelpers)"
    }
    if ($A64InlinePpcThreadFieldLeafHelpers) {
        $parts += "--ez a64_inline_ppc_thread_field_leaf_helpers $(ConvertTo-BooleanText $A64InlinePpcThreadFieldLeafHelpers)"
    }
    if ($Arm64BlueDragonDrawWaitProbe) {
        $parts += "--ez arm64_blue_dragon_draw_wait_probe $(ConvertTo-BooleanText $Arm64BlueDragonDrawWaitProbe)"
    }
    if ($Arm64BlueDragonDrawWaitProbeStride) {
        $parts += "--ei arm64_blue_dragon_draw_wait_probe_stride $Arm64BlueDragonDrawWaitProbeStride"
    }
    if ($Arm64BlueDragonDrawWaitInlineTickStep) {
        $parts += "--ei arm64_blue_dragon_draw_wait_inline_tick_step $Arm64BlueDragonDrawWaitInlineTickStep"
    }
    if ($Arm64BlueDragonDrawWaitFastpath) {
        $parts += "--ez arm64_blue_dragon_draw_wait_fastpath $(ConvertTo-BooleanText $Arm64BlueDragonDrawWaitFastpath)"
    }
    if ($Arm64BlueDragonDrawWaitFastpathHostCounterTime) {
        $parts += "--ez arm64_blue_dragon_draw_wait_fastpath_host_counter_time $(ConvertTo-BooleanText $Arm64BlueDragonDrawWaitFastpathHostCounterTime)"
    }
    if ($Arm64BlueDragonDrawWaitFastpathNativeYieldStride) {
        $parts += "--ei arm64_blue_dragon_draw_wait_fastpath_native_yield_stride $Arm64BlueDragonDrawWaitFastpathNativeYieldStride"
    }
    if ($Arm64BlueDragonDrawWaitFastpathNativeSleepUs) {
        $parts += "--ei arm64_blue_dragon_draw_wait_fastpath_native_sleep_us $Arm64BlueDragonDrawWaitFastpathNativeSleepUs"
    }
    if ($Arm64BlueDragonDrawWaitFastpathTimeoutMs) {
        $parts += "--ei arm64_blue_dragon_draw_wait_fastpath_timeout_ms $Arm64BlueDragonDrawWaitFastpathTimeoutMs"
    }
    if ($Arm64BlueDragonDrawWaitInlineInCaller) {
        $parts += "--ez arm64_blue_dragon_draw_wait_inline_in_caller $(ConvertTo-BooleanText $Arm64BlueDragonDrawWaitInlineInCaller)"
    }
    if ($Arm64BlueDragonDrawWaitCallerProfile) {
        $parts += "--ez arm64_blue_dragon_draw_wait_caller_profile $(ConvertTo-BooleanText $Arm64BlueDragonDrawWaitCallerProfile)"
    }
    if ($Arm64BlueDragonDrawWaitCallerProfileStride) {
        $parts += "--ei arm64_blue_dragon_draw_wait_caller_profile_stride $Arm64BlueDragonDrawWaitCallerProfileStride"
    }
    if ($Arm64BlueDragonDrawWaitCallerProfileBudget) {
        $parts += "--ei arm64_blue_dragon_draw_wait_caller_profile_budget $Arm64BlueDragonDrawWaitCallerProfileBudget"
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
    $effectiveA64InlineFprHelpers = $A64InlineFprHelpers
    $effectiveA64InlineVmxHelpers = $A64InlineVmxHelpers
    if ($A64InlineFprVmxHelpers) {
        if (-not $effectiveA64InlineFprHelpers) {
            $effectiveA64InlineFprHelpers = $A64InlineFprVmxHelpers
        }
        if (-not $effectiveA64InlineVmxHelpers) {
            $effectiveA64InlineVmxHelpers = $A64InlineFprVmxHelpers
        }
    }
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
        "shader_dump_device_path=$script:ActiveDumpShadersPath",
        "arm64_speed_profile_interval_ms=$Arm64SpeedProfileIntervalMs",
        "arm64_speed_profile_top_functions=$Arm64SpeedProfileTopFunctions",
        "arm64_speed_profile_min_delta=$Arm64SpeedProfileMinDelta",
        "a64_inline_gprlr_helpers=$A64InlineGprLrHelpers",
        "a64_inline_fpr_helpers=$effectiveA64InlineFprHelpers",
        "a64_inline_vmx_helpers=$effectiveA64InlineVmxHelpers",
        "a64_inline_fpr_vmx_helpers=$A64InlineFprVmxHelpers",
        "a64_inline_ppc_thread_field_leaf_helpers=$A64InlinePpcThreadFieldLeafHelpers",
        "arm64_blue_dragon_draw_wait_probe=$Arm64BlueDragonDrawWaitProbe",
        "arm64_blue_dragon_draw_wait_probe_stride=$Arm64BlueDragonDrawWaitProbeStride",
        "arm64_blue_dragon_draw_wait_inline_tick_step=$Arm64BlueDragonDrawWaitInlineTickStep",
        "arm64_blue_dragon_draw_wait_fastpath=$Arm64BlueDragonDrawWaitFastpath",
        "arm64_blue_dragon_draw_wait_fastpath_host_counter_time=$Arm64BlueDragonDrawWaitFastpathHostCounterTime",
        "arm64_blue_dragon_draw_wait_fastpath_native_yield_stride=$Arm64BlueDragonDrawWaitFastpathNativeYieldStride",
        "arm64_blue_dragon_draw_wait_fastpath_native_sleep_us=$Arm64BlueDragonDrawWaitFastpathNativeSleepUs",
        "arm64_blue_dragon_draw_wait_fastpath_timeout_ms=$Arm64BlueDragonDrawWaitFastpathTimeoutMs",
        "arm64_blue_dragon_draw_wait_inline_in_caller=$Arm64BlueDragonDrawWaitInlineInCaller",
        "arm64_blue_dragon_draw_wait_caller_profile=$Arm64BlueDragonDrawWaitCallerProfile",
        "arm64_blue_dragon_draw_wait_caller_profile_stride=$Arm64BlueDragonDrawWaitCallerProfileStride",
        "arm64_blue_dragon_draw_wait_caller_profile_budget=$Arm64BlueDragonDrawWaitCallerProfileBudget",
        "xma_fast_silence=$XmaFastSilence",
        "log_level=$LogLevel",
        "gpu_unknown_register_log_budget=$GpuUnknownRegisterLogBudget",
        "xboxkrnl_nt_create_file_fail_log_budget=$XboxkrnlNtCreateFileFailLogBudget",
        "hide_android_osd=$HideAndroidOsd",
        "hid_nop_connected=$HidNopConnected",
        "hid_nop_button_sequence=$HidNopButtonSequence",
        "vulkan_force_signed_2101010_unorm_fallback=$VulkanForceSigned2101010UnormFallback",
        "vulkan_force_2101010_rgba8_fallback=$VulkanForce2101010Rgba8Fallback",
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

function Add-PerfSection {
    param(
        [string]$Path,
        [string]$Title,
        [object[]]$Lines
    )

    "" | Out-File -Encoding utf8 -Append $Path
    "## $Title" | Out-File -Encoding utf8 -Append $Path
    if ($Lines -and $Lines.Count -gt 0) {
        $Lines | Out-File -Encoding utf8 -Append $Path
    } else {
        "(no output)" | Out-File -Encoding utf8 -Append $Path
    }
}

function Get-PerfSampleSecondValues {
    param(
        [string]$Value,
        [int]$MaxSeconds
    )

    $samples = @()
    foreach ($rawSample in ($Value -split ",")) {
        $trimmed = $rawSample.Trim()
        if (!$trimmed) {
            continue
        }
        [int]$parsed = 0
        if ([int]::TryParse($trimmed, [ref]$parsed) -and $parsed -gt 0 -and $parsed -le $MaxSeconds) {
            $samples += $parsed
        }
    }
    return $samples | Sort-Object -Unique
}

function Write-PerfSnapshot {
    param(
        [string]$Stamp,
        [string]$Label,
        [string]$OutDir
    )

    $path = Join-Path $OutDir "$Stamp-perf-$Label.txt"
    $pidLine = ((Invoke-Adb @("shell", "pidof", $PackageName)) -join " ").Trim()
    $packagePid = ""
    if ($pidLine) {
        $packagePid = ($pidLine -split "\s+" | Where-Object { $_ } | Select-Object -First 1)
    }

    @(
        "timestamp=$(Get-Date -Format o)",
        "label=$Label",
        "package=$PackageName",
        "pid=$pidLine",
        "perf_top_thread_count=$PerfTopThreadCount"
    ) | Out-File -Encoding utf8 $path

    if (!$packagePid) {
        Add-PerfSection $path "process" @("process_not_running=true")
        return $path
    }

    Add-PerfSection $path "top_threads" `
        (Invoke-AdbShellCommand "top -H -b -n 1 -p $packagePid | head -$PerfTopThreadCount")
    Add-PerfSection $path "cpuinfo" `
        (Invoke-AdbShellCommand "dumpsys cpuinfo | grep -E 'TOTAL|$PackageName|surfaceflinger|composer|android.hardware.graphics' | head -80")
    Add-PerfSection $path "proc_status" `
        (Invoke-AdbShellCommand "cat /proc/$packagePid/status 2>/dev/null | head -100")
    Add-PerfSection $path "thread_names" `
        (Invoke-AdbShellCommand "for t in /proc/$packagePid/task/*/comm; do cat `$t 2>/dev/null; done | sort | uniq -c | sort -nr | head -80")
    Add-PerfSection $path "meminfo" `
        (Invoke-AdbShellCommand "dumpsys meminfo $packagePid 2>/dev/null | head -120")
    Add-PerfSection $path "surfaceflinger_layers" `
        (Invoke-AdbShellCommand "dumpsys SurfaceFlinger --list 2>/dev/null | grep -i -E 'xenia|emulator|$PackageName' | head -80")
    Add-PerfSection $path "gfxinfo" `
        (Invoke-AdbShellCommand "dumpsys gfxinfo $PackageName 2>/dev/null | head -120")
    Add-PerfSection $path "thermal" `
        (Invoke-AdbShellCommand "dumpsys thermalservice 2>/dev/null | head -120")
    return $path
}

function Use-BlueDragonSpeedDefaults {
    $script:HideAndroidOsd = "true"
    $script:HidNopConnected = "true"
    if (!$script:HidNopButtonSequence) {
        $script:HidNopButtonSequence = "start@45000:3000;a@68000:3000;a@86000:3000;a@106000:3000"
    }

    $script:DumpShaders = ""
    $script:RenderTargetPathVulkan = ""
    $script:BreakOnDebugbreak = "false"
    $script:DisassembleFunctions = "false"
    $script:XmaTraceContextState = "false"
    $script:ClearMemoryPageState = "false"
    $script:LogLevel = "0"
    if ($script:Arm64SpeedProfileIntervalMs) {
        $script:LogLevel = "1"
    }
    $script:VulkanForceSigned2101010UnormFallback = "true"
    $script:VulkanForce2101010Rgba8Fallback = "false"
    if (!$script:GpuUnknownRegisterLogBudget) {
        $script:GpuUnknownRegisterLogBudget = "0"
    }
    if (!$script:XboxkrnlNtCreateFileFailLogBudget) {
        $script:XboxkrnlNtCreateFileFailLogBudget = "0"
    }

    foreach ($name in @(
        "EmitInlineMmioChecks",
        "GpuInterruptOnRingIdle",
        "GpuInterruptOnSwap",
        "GpuTraceInterrupts",
        "GpuBlueDragonKickWaitToken",
        "GpuTraceSwap",
        "GpuTraceTextureCacheActions",
        "GpuTraceSwapFrontbufferChecksum",
        "GpuTraceSwapRenderTargets",
        "VulkanTraceResolve",
        "VulkanTraceResolveChecksum",
        "VulkanTraceEdramChecksum",
        "VulkanReadbackResolve",
        "VulkanTraceCopyState",
        "VulkanTraceDrawState",
        "VulkanTracePipelineCreation",
        "VulkanTraceShaderConstants",
        "VulkanTraceTextureSourceChecksum",
        "VulkanTraceVertexFetchChecksum",
        "VulkanTraceSwapSharedMemoryChecksum",
        "VulkanPresentRecentResolveOnSwap",
        "VulkanPresentScoredResolveOnSwap",
        "VulkanPresentScoredResolveRejectClearLike",
        "VulkanPresentForcedResolveOnSwap",
        "VulkanDebugSolidGuestOutput",
        "VulkanDebugTextureFetchDisableExpAdjust",
        "GpuEarlyPrimaryReadPointerWriteback",
        "XboxkrnlThreadWaitTrace",
        "XboxkrnlEventTrace"
    )) {
        Set-Variable -Name $name -Scope Script -Value "false"
    }

    foreach ($name in @(
        "GpuBlueDragonKickWaitTokenBudget",
        "GpuTraceInterruptsBudget",
        "GpuTracePacketBudget",
        "GpuTraceSwapFrontbufferChecksumBudget",
        "GpuTraceSwapRenderTargetsBudget",
        "VulkanTraceResolveBudget",
        "VulkanTraceResolveChecksumBudget",
        "VulkanTraceEdramChecksumBudget",
        "VulkanTraceCopyStateBudget",
        "VulkanTraceDrawStateBudget",
        "VulkanTraceShaderConstantsBudget",
        "VulkanTraceTextureSourceChecksumBudget",
        "VulkanTraceVertexFetchChecksumBudget",
        "VulkanTraceSwapSharedMemoryChecksumBudget",
        "VulkanTraceDrawShaderFilter",
        "VulkanTraceShaderConstantsShaderFilter",
        "VulkanTraceTextureSourceShaderFilter",
        "VulkanTraceVertexFetchShaderFilter",
        "VulkanDebugPixelShaderOutputFilter",
        "VulkanDebugPixelShaderOutputMode",
        "VulkanDebugPixelShaderOutputSecondaryFilter",
        "VulkanDebugPixelShaderOutputSecondaryMode",
        "VulkanPresentScoredResolveMinWidth",
        "VulkanPresentScoredResolveMinHeight",
        "VulkanPresentScoredResolveBudget",
        "VulkanPresentScoredResolveRequiredFormat",
        "VulkanPresentForcedResolveAddress",
        "VulkanPresentForcedResolveLength",
        "VulkanPresentForcedResolveWidth",
        "VulkanPresentForcedResolveHeight",
        "VulkanPresentForcedResolvePitch",
        "VulkanPresentForcedResolveFormat",
        "Arm64ForceInterpreterRanges",
        "Arm64GuestStoreWatch",
        "Arm64GuestStoreWatchBudget",
        "Arm64CompiledCallTraceInterval",
        "Arm64CompiledCallTraceMinCount",
        "Arm64CompiledCallTraceBudget",
        "Arm64CompiledCallTraceFunctions",
        "Arm64CompiledCallTraceGuestTids",
        "Arm64CompiledCallTraceAfterMs",
        "XboxkrnlThreadWaitTraceBudget",
        "XboxkrnlThreadWaitTraceAfterMs",
        "XboxkrnlThreadWaitTraceGuestTids",
        "XboxkrnlEventTraceBudget",
        "XboxkrnlEventTraceObjects"
    )) {
        Set-Variable -Name $name -Scope Script -Value ""
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
        Set-ActiveShaderDumpPath $Stamp

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
        $ShaderDumpPath = Pull-ActiveShaderDumps $Stamp $OutDir
        Write-CaptureMetadata $Stamp $MetaPath $launchTarget
        Invoke-AdbExecOutToFile "screencap -p" $ScreenshotPath
        Write-Output "Log: $LogPath"
        Write-Output "Filtered log: $FilteredLogPath"
        Write-Output "Log errors: $LogErrorPath"
        Write-Output "Meta: $MetaPath"
        Write-Output "Screenshot: $ScreenshotPath"
        if ($ShaderDumpPath) {
            Write-Output "Shader dumps: $ShaderDumpPath"
        }
    }
    "LaunchBlueDragonSpeedCapture" {
        $Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $LogPath = Join-Path $OutDir "$Stamp-speed-logcat.txt"
        $FilteredLogPath = Join-Path $OutDir "$Stamp-speed-logcat-filtered.txt"
        $MetaPath = Join-Path $OutDir "$Stamp-meta.txt"
        $ScreenshotPath = Join-Path $OutDir "$Stamp-screenshot.png"
        $launchTarget = Resolve-BlueDragonLaunchTarget
        $sampleSeconds = @(Get-PerfSampleSecondValues $PerfSampleSeconds $LiveCaptureSeconds)
        $perfPaths = @()
        $elapsedSeconds = 0

        Use-BlueDragonSpeedDefaults
        Write-Output "Launching target: $launchTarget"
        Write-Output "Speed sample seconds: $($sampleSeconds -join ',')"
        if ($Arm64SpeedProfileIntervalMs) {
            Write-Output "A64 speed profile interval: ${Arm64SpeedProfileIntervalMs}ms"
        }
        Invoke-Adb @("shell", "am", "force-stop", $PackageName) | Out-Null
        Invoke-Adb @("logcat", "-c") | Out-Null
        Start-XeniaEmulator $launchTarget -SkipForceStop -SkipLogcatClear

        foreach ($sampleSecond in $sampleSeconds) {
            if ($sampleSecond -gt $elapsedSeconds) {
                Start-Sleep -Seconds ($sampleSecond - $elapsedSeconds)
                $elapsedSeconds = $sampleSecond
            }
            $perfPaths += Write-PerfSnapshot $Stamp "${sampleSecond}s" $OutDir
        }
        if ($LiveCaptureSeconds -gt $elapsedSeconds) {
            Start-Sleep -Seconds ($LiveCaptureSeconds - $elapsedSeconds)
        }
        $perfPaths += Write-PerfSnapshot $Stamp "final" $OutDir

        $logcat = Invoke-Adb @("logcat", "-d", "-v", "time")
        $logcat | Out-File -Encoding utf8 $LogPath
        Write-FilteredLog $LogPath $FilteredLogPath
        Write-CaptureMetadata $Stamp $MetaPath $launchTarget
        Invoke-AdbExecOutToFile "screencap -p" $ScreenshotPath
        Write-Output "Log: $LogPath"
        Write-Output "Filtered log: $FilteredLogPath"
        Write-Output "Meta: $MetaPath"
        Write-Output "Screenshot: $ScreenshotPath"
        foreach ($perfPath in $perfPaths) {
            Write-Output "Perf: $perfPath"
        }
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
