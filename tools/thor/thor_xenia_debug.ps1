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
        "LaunchBlueDragonTitleCapture",
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
    [ValidateSet("android", "any", "nop", "sdl", "winkey", "xinput")]
    [string]$HidDriver = "android",
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
    [string]$MountCache = "true",
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
    [string]$VulkanTracePerfCounters = "false",
    [string]$VulkanTracePerfCountersLogInterval = "",
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
    [string]$Arm64GlobalReservationHelpers = "",
    [string]$Arm64OffsetMemoryAddressFastpath = "",
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
    [string]$A64InlineKernelHighFrequencyExports = "true",
    [string]$A64InlineKernelSpinlockExports = "true",
    [string]$A64LseKernelLockFastpaths = "true",
    [string]$A64EnableHostGuestStackSynchronization = "",
    [string]$Arm64VmxDotF32Fastpath = "false",
    [string]$Arm64PermuteI32ZipFastpath = "true",
    [string]$A64RtlEnterFreeFirst = "false",
    [string]$A64InlineRtlLeaveFinalUnlock = "true",
    [string]$A64RtlLeaveFastpathAudit = "false",
    [string]$A64InlineKfLowerIrql = "false",
    [string]$A64InlineKfLowerIrqlApcGuard = "false",
    [string]$A64KfLowerIrqlApcGuardAudit = "false",
    [string]$A64KfLowerIrqlApcGuardNativePollInterval = "",
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
    [string]$Arm64BlueDragonMemcpyFastpath = "false",
    [string]$Arm64BlueDragonStricmpFastpath = "false",
    [string]$Arm64BlueDragonStricmpDeferredCrFastpath = "false",
    [string]$Arm64BlueDragonStricmpReturnProfile = "false",
    [string]$Arm64BlueDragonStricmpReturnProfileStride = "",
    [string]$Arm64BlueDragonStricmpReturnProfileBudget = "",
    [string]$Arm64BlueDragonJumpTableFastpath = "false",
    [string]$Arm64BlueDragonJumpTableInlineInCaller = "false",
    [string]$Arm64BlueDragonVmxCopyLoopFastpath = "false",
    [string]$Arm64BlueDragonWordCopyLoopFastpath = "false",
    [string]$Arm64BlueDragonStvewxStackLaneFastpath = "false",
    [string]$Arm64BlueDragonStvewxStackLaneAudit = "false",
    [string]$Arm64BlueDragonMulAddV128Fastpath = "false",
    [string]$Arm64BlueDragonMulAddV128Audit = "false",
    [string]$Arm64BlueDragonCallBoundaryStateAudit = "false",
    [string]$Arm64BlueDragonCallBoundaryStateSuppressDeadStores = "false",
    [string]$Arm64BlueDragonF1CarrierAudit = "false",
    [string]$Arm64BlueDragonF1CarrierFastpath = "false",
    [string]$Arm64BlueDragonStateCarrierDesignAudit = "false",
    [string]$Arm64BlueDragonEdgeVariantAudit = "false",
    [string]$Arm64BlueDragonEdgePayloadStorageAudit = "false",
    [string]$Arm64BlueDragonFpscrCfgWritebackAudit = "false",
    [string]$Arm64AddSubImmAudit = "false",
    [string]$Arm64AddSubImmAuditFunction = "",
    [string]$Arm64AddSubImmAuditBudget = "",
    [string]$Arm64AddI64WrappedImmFastpath = "true",
    [string]$Arm64AddI64WrappedImmFastpathFunction = "",
    [string]$Arm64ImmediateLoweringAudit = "false",
    [string]$Arm64ImmediateLoweringAuditFunction = "",
    [string]$Arm64ImmediateLoweringAuditBudget = "",
    [string]$Arm64ContextValueCache = "",
    [string]$Arm64ContextValueCacheFallthrough = "",
    [string]$Arm64ContextValueCachePreserveBarrier = "",
    [string]$Arm64ContextPinnedGprR1 = "",
    [string]$Arm64ContextPinnedGprR1Fallthrough = "",
    [string]$Arm64ContextPinnedGprR1Function = "",
    [string]$Arm64ContextPinnedGprR1Audit = "",
    [string]$Arm64ContextPromotionGprLocalSlots = "",
    [string]$Arm64ContextPromotionGprLocalSlotsFunction = "",
    [string]$Arm64ContextPromotionGprLocalSlotsAudit = "",
    [string]$Arm64ContextPromotionGprLiveInR1 = "",
    [string]$Arm64ContextPromotionGprLiveInR1Function = "",
    [string]$Arm64ContextPromotionGprLiveInR1PreserveBarrier = "",
    [string]$Arm64ContextPromotionGprLiveInR1Audit = "",
    [string]$Arm64GuestStateRegisterCacheAudit = "",
    [string]$Arm64GuestStateRegisterCacheAuditFunction = "",
    [string]$Arm64GuestStateRegisterCacheResidualAudit = "",
    [string]$Arm64GuestStateRegisterCacheResidualAuditFunction = "",
    [string]$Arm64RegisterAllocationAudit = "",
    [string]$Arm64RegisterAllocationAuditFunction = "",
    [string]$Arm64GuestCallFastEntryAudit = "",
    [string]$Arm64GuestCallFastEntryAuditFunction = "",
    [string]$Arm64GuestCallFastEntryAuditBudget = "",
    [string]$Arm64GuestStackArgHandoffAudit = "",
    [string]$Arm64GuestStackArgHandoffAuditFunction = "",
    [string]$Arm64GuestStackArgHandoffAuditBudget = "",
    [string]$Arm64CrCompareBranchAcrossContextBarrier = "",
    [string]$Arm64CrStoreElideForFusedBranch = "",
    [string]$Arm64CrStoreElideForFusedBranchFunction = "",
    [string]$Arm64ContextTrafficAudit = "false",
    [string]$Arm64ContextTrafficAuditFunction = "",
    [string]$Arm64ContextTrafficAuditBudget = "",
    [string]$Arm64SpeedProfileBodyTimeFilter = "",
    [string]$Arm64SpeedProfileEntryExitTimeFilter = "",
    [string]$Arm64SpeedProfileBodyTimeAfterMs = "",
    [string]$Arm64SpeedProfileBlockFilter = "",
    [string]$Arm64SpeedProfileBlockBodyTime = "false",
    [string]$Arm64SpeedProfileCallEdgeFilter = "",
    [string]$Arm64SpeedProfileCallEdgeAuditOnly = "false",
    [string]$Arm64SpeedProfileThreadSnapshot = "false",
    [string]$Arm64SpeedProfileThreadSnapshotOnIdle = "false",
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
    [string]$TitleScreenshotSeconds = "0",
    [int]$PerfTopThreadCount = 80,
    [string]$Simpleperf = "false",
    [int]$SimpleperfStartSecond = 70,
    [int]$SimpleperfSeconds = 12,
    [int]$SimpleperfFrequency = 1000,
    [string]$SimpleperfEvent = "cpu-cycles:u",
    [string]$SimpleperfCallGraph = "fp",
    [string]$SimpleperfPercentLimit = "0.5",
    [string[]]$NoisePackages = @("net.rpcsx.easy"),
    [string]$StopAppAfterCapture = "true",
    [string]$LogFilter = "xenia|Vulkan|Adreno|AndroidRuntime|FATAL|crash|tombstone|signal|backtrace"
)

$ErrorActionPreference = "Stop"

$script:RootBoundParameters = @{} + $PSBoundParameters
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

function Invoke-AdbPullToFile {
    param(
        [string]$DevicePath,
        [string]$OutputPath
    )

    Ensure-AdbDevice
    $serialPart = ""
    if ($DeviceSerial) {
        $serialPart = "-s $DeviceSerial "
    }

    $cmdLine = "adb ${serialPart}pull `"$DevicePath`" `"$OutputPath`""
    $output = cmd /c "$cmdLine 2>&1"
    $script:LastAdbExitCode = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) {
        Repair-AdbDevice "pull failed: $DevicePath"
        $output = cmd /c "$cmdLine 2>&1"
        $script:LastAdbExitCode = $LASTEXITCODE
    }
    return $output
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

function Test-TrueLikeText {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $false
    }
    return (ConvertTo-BooleanText $Value) -eq "true"
}

function ConvertTo-AdbIntText {
    param([string]$Value)
    $trimmed = $Value.Trim()
    if ($trimmed -match "^(?i)0x([0-9a-f]+)$") {
        $u32 = [Convert]::ToUInt32($Matches[1], 16)
        return [BitConverter]::ToInt32([BitConverter]::GetBytes($u32), 0).ToString()
    }
    if ($trimmed -match "^[0-9]+$") {
        $u32 = [Convert]::ToUInt64($trimmed, 10)
        if ($u32 -le [uint32]::MaxValue -and $u32 -gt [int32]::MaxValue) {
            return [BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]$u32), 0).ToString()
        }
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
        "--es hid $(ConvertTo-AdbShellSingleQuote $HidDriver)",
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
        "--ez vulkan_trace_perf_counters $(ConvertTo-BooleanText $VulkanTracePerfCounters)",
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
    if ($VulkanTracePerfCountersLogInterval) {
        $parts += "--ei vulkan_trace_perf_counters_log_interval $(ConvertTo-AdbIntText $VulkanTracePerfCountersLogInterval)"
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
    if ($Arm64GlobalReservationHelpers) {
        $parts += "--ez arm64_global_reservation_helpers $(ConvertTo-BooleanText $Arm64GlobalReservationHelpers)"
    }
    if ($Arm64OffsetMemoryAddressFastpath) {
        $parts += "--ez arm64_offset_memory_address_fastpath $(ConvertTo-BooleanText $Arm64OffsetMemoryAddressFastpath)"
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
    if ($A64InlineKernelHighFrequencyExports) {
        $parts += "--ez a64_inline_kernel_high_frequency_exports $(ConvertTo-BooleanText $A64InlineKernelHighFrequencyExports)"
    }
    if ($A64InlineKernelSpinlockExports) {
        $parts += "--ez a64_inline_kernel_spinlock_exports $(ConvertTo-BooleanText $A64InlineKernelSpinlockExports)"
    }
    if ($A64LseKernelLockFastpaths) {
        $parts += "--ez a64_lse_kernel_lock_fastpaths $(ConvertTo-BooleanText $A64LseKernelLockFastpaths)"
    }
    if ($Arm64VmxDotF32Fastpath) {
        $parts += "--ez arm64_vmx_dot_f32_fastpath $(ConvertTo-BooleanText $Arm64VmxDotF32Fastpath)"
    }
    if ($Arm64PermuteI32ZipFastpath) {
        $parts += "--ez arm64_permute_i32_zip_fastpath $(ConvertTo-BooleanText $Arm64PermuteI32ZipFastpath)"
    }
    if ($A64RtlEnterFreeFirst) {
        $parts += "--ez a64_rtl_enter_free_first $(ConvertTo-BooleanText $A64RtlEnterFreeFirst)"
    }
    if ($A64InlineRtlLeaveFinalUnlock) {
        $parts += "--ez a64_inline_rtl_leave_final_unlock $(ConvertTo-BooleanText $A64InlineRtlLeaveFinalUnlock)"
    }
    if ($A64RtlLeaveFastpathAudit) {
        $parts += "--ez a64_rtl_leave_fastpath_audit $(ConvertTo-BooleanText $A64RtlLeaveFastpathAudit)"
    }
    if ($A64InlineKfLowerIrql) {
        $parts += "--ez a64_inline_kf_lower_irql $(ConvertTo-BooleanText $A64InlineKfLowerIrql)"
    }
    if ($A64InlineKfLowerIrqlApcGuard) {
        $parts += "--ez a64_inline_kf_lower_irql_apc_guard $(ConvertTo-BooleanText $A64InlineKfLowerIrqlApcGuard)"
    }
    if ($A64KfLowerIrqlApcGuardAudit) {
        $parts += "--ez a64_kf_lower_irql_apc_guard_audit $(ConvertTo-BooleanText $A64KfLowerIrqlApcGuardAudit)"
    }
    if ($A64KfLowerIrqlApcGuardNativePollInterval) {
        $parts += "--ei a64_kf_lower_irql_apc_guard_native_poll_interval $A64KfLowerIrqlApcGuardNativePollInterval"
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
    if ($Arm64BlueDragonMemcpyFastpath) {
        $parts += "--ez arm64_blue_dragon_memcpy_fastpath $(ConvertTo-BooleanText $Arm64BlueDragonMemcpyFastpath)"
    }
    if ($Arm64BlueDragonStricmpFastpath) {
        $parts += "--ez arm64_blue_dragon_stricmp_fastpath $(ConvertTo-BooleanText $Arm64BlueDragonStricmpFastpath)"
    }
    if ($Arm64BlueDragonStricmpDeferredCrFastpath) {
        $parts += "--ez arm64_blue_dragon_stricmp_deferred_cr_fastpath $(ConvertTo-BooleanText $Arm64BlueDragonStricmpDeferredCrFastpath)"
    }
    if ($Arm64BlueDragonStricmpReturnProfile) {
        $parts += "--ez arm64_blue_dragon_stricmp_return_profile $(ConvertTo-BooleanText $Arm64BlueDragonStricmpReturnProfile)"
    }
    if ($Arm64BlueDragonStricmpReturnProfileStride) {
        $parts += "--ei arm64_blue_dragon_stricmp_return_profile_stride $Arm64BlueDragonStricmpReturnProfileStride"
    }
    if ($Arm64BlueDragonStricmpReturnProfileBudget) {
        $parts += "--ei arm64_blue_dragon_stricmp_return_profile_budget $Arm64BlueDragonStricmpReturnProfileBudget"
    }
    if ($Arm64BlueDragonJumpTableFastpath) {
        $parts += "--ez arm64_blue_dragon_jump_table_fastpath $(ConvertTo-BooleanText $Arm64BlueDragonJumpTableFastpath)"
    }
    if ($Arm64BlueDragonJumpTableInlineInCaller) {
        $parts += "--ez arm64_blue_dragon_jump_table_inline_in_caller $(ConvertTo-BooleanText $Arm64BlueDragonJumpTableInlineInCaller)"
    }
    if ($Arm64BlueDragonVmxCopyLoopFastpath) {
        $parts += "--ez arm64_blue_dragon_vmx_copy_loop_fastpath $(ConvertTo-BooleanText $Arm64BlueDragonVmxCopyLoopFastpath)"
    }
    if ($Arm64BlueDragonWordCopyLoopFastpath) {
        $parts += "--ez arm64_blue_dragon_word_copy_loop_fastpath $(ConvertTo-BooleanText $Arm64BlueDragonWordCopyLoopFastpath)"
    }
    if ($Arm64BlueDragonStvewxStackLaneFastpath) {
        $parts += "--ez arm64_blue_dragon_stvewx_stack_lane_fastpath $(ConvertTo-BooleanText $Arm64BlueDragonStvewxStackLaneFastpath)"
    }
    if ($Arm64BlueDragonStvewxStackLaneAudit) {
        $parts += "--ez arm64_blue_dragon_stvewx_stack_lane_audit $(ConvertTo-BooleanText $Arm64BlueDragonStvewxStackLaneAudit)"
    }
    if ($Arm64BlueDragonMulAddV128Fastpath) {
        $parts += "--ez arm64_blue_dragon_mul_add_v128_fastpath $(ConvertTo-BooleanText $Arm64BlueDragonMulAddV128Fastpath)"
    }
    if ($Arm64BlueDragonMulAddV128Audit) {
        $parts += "--ez arm64_blue_dragon_mul_add_v128_audit $(ConvertTo-BooleanText $Arm64BlueDragonMulAddV128Audit)"
    }
    if ($Arm64BlueDragonCallBoundaryStateAudit) {
        $parts += "--ez arm64_blue_dragon_call_boundary_state_audit $(ConvertTo-BooleanText $Arm64BlueDragonCallBoundaryStateAudit)"
    }
    if ($Arm64BlueDragonCallBoundaryStateSuppressDeadStores) {
        $parts += "--ez arm64_blue_dragon_call_boundary_state_suppress_dead_stores $(ConvertTo-BooleanText $Arm64BlueDragonCallBoundaryStateSuppressDeadStores)"
    }
    if ($Arm64BlueDragonF1CarrierAudit) {
        $parts += "--ez arm64_blue_dragon_f1_carrier_audit $(ConvertTo-BooleanText $Arm64BlueDragonF1CarrierAudit)"
    }
    if ($Arm64BlueDragonF1CarrierFastpath) {
        $parts += "--ez arm64_blue_dragon_f1_carrier_fastpath $(ConvertTo-BooleanText $Arm64BlueDragonF1CarrierFastpath)"
    }
    if ($Arm64BlueDragonStateCarrierDesignAudit) {
        $parts += "--ez arm64_blue_dragon_state_carrier_design_audit $(ConvertTo-BooleanText $Arm64BlueDragonStateCarrierDesignAudit)"
    }
    if ($Arm64BlueDragonEdgeVariantAudit) {
        $parts += "--ez arm64_blue_dragon_edge_variant_audit $(ConvertTo-BooleanText $Arm64BlueDragonEdgeVariantAudit)"
    }
    if ($Arm64BlueDragonEdgePayloadStorageAudit) {
        $parts += "--ez arm64_blue_dragon_edge_payload_storage_audit $(ConvertTo-BooleanText $Arm64BlueDragonEdgePayloadStorageAudit)"
    }
    if ($Arm64BlueDragonFpscrCfgWritebackAudit) {
        $parts += "--ez arm64_blue_dragon_fpscr_cfg_writeback_audit $(ConvertTo-BooleanText $Arm64BlueDragonFpscrCfgWritebackAudit)"
    }
    if ($A64EnableHostGuestStackSynchronization) {
        $parts += "--ez a64_enable_host_guest_stack_synchronization $(ConvertTo-BooleanText $A64EnableHostGuestStackSynchronization)"
    }
    if ($Arm64AddSubImmAudit) {
        $parts += "--ez arm64_add_sub_imm_audit $(ConvertTo-BooleanText $Arm64AddSubImmAudit)"
    }
    if ($Arm64AddSubImmAuditFunction) {
        $parts += "--ei arm64_add_sub_imm_audit_function $(ConvertTo-AdbIntText $Arm64AddSubImmAuditFunction)"
    }
    if ($Arm64AddSubImmAuditBudget) {
        $parts += "--ei arm64_add_sub_imm_audit_budget $(ConvertTo-AdbIntText $Arm64AddSubImmAuditBudget)"
    }
    if ($Arm64AddI64WrappedImmFastpath) {
        $parts += "--ez arm64_add_i64_wrapped_imm_fastpath $(ConvertTo-BooleanText $Arm64AddI64WrappedImmFastpath)"
    }
    if ($Arm64AddI64WrappedImmFastpathFunction) {
        $parts += "--ei arm64_add_i64_wrapped_imm_fastpath_function $(ConvertTo-AdbIntText $Arm64AddI64WrappedImmFastpathFunction)"
    }
    if ($Arm64ImmediateLoweringAudit) {
        $parts += "--ez arm64_immediate_lowering_audit $(ConvertTo-BooleanText $Arm64ImmediateLoweringAudit)"
    }
    if ($Arm64ImmediateLoweringAuditFunction) {
        $parts += "--ei arm64_immediate_lowering_audit_function $(ConvertTo-AdbIntText $Arm64ImmediateLoweringAuditFunction)"
    }
    if ($Arm64ImmediateLoweringAuditBudget) {
        $parts += "--ei arm64_immediate_lowering_audit_budget $(ConvertTo-AdbIntText $Arm64ImmediateLoweringAuditBudget)"
    }
    if ($Arm64ContextValueCache) {
        $parts += "--ez arm64_context_value_cache $(ConvertTo-BooleanText $Arm64ContextValueCache)"
    }
    if ($Arm64ContextValueCacheFallthrough) {
        $parts += "--ez arm64_context_value_cache_fallthrough $(ConvertTo-BooleanText $Arm64ContextValueCacheFallthrough)"
    }
    if ($Arm64ContextValueCachePreserveBarrier) {
        $parts += "--ez arm64_context_value_cache_preserve_barrier $(ConvertTo-BooleanText $Arm64ContextValueCachePreserveBarrier)"
    }
    if ($Arm64ContextPinnedGprR1) {
        $parts += "--ez arm64_context_pinned_gpr_r1 $(ConvertTo-BooleanText $Arm64ContextPinnedGprR1)"
    }
    if ($Arm64ContextPinnedGprR1Fallthrough) {
        $parts += "--ez arm64_context_pinned_gpr_r1_fallthrough $(ConvertTo-BooleanText $Arm64ContextPinnedGprR1Fallthrough)"
    }
    if ($Arm64ContextPinnedGprR1Function) {
        $parts += "--ei arm64_context_pinned_gpr_r1_function $(ConvertTo-AdbIntText $Arm64ContextPinnedGprR1Function)"
    }
    if ($Arm64ContextPinnedGprR1Audit) {
        $parts += "--ez arm64_context_pinned_gpr_r1_audit $(ConvertTo-BooleanText $Arm64ContextPinnedGprR1Audit)"
    }
    if ($Arm64ContextPromotionGprLocalSlots) {
        $parts += "--ez arm64_context_promotion_gpr_local_slots $(ConvertTo-BooleanText $Arm64ContextPromotionGprLocalSlots)"
    }
    if ($Arm64ContextPromotionGprLocalSlotsFunction) {
        $parts += "--ei arm64_context_promotion_gpr_local_slots_function $(ConvertTo-AdbIntText $Arm64ContextPromotionGprLocalSlotsFunction)"
    }
    if ($Arm64ContextPromotionGprLocalSlotsAudit) {
        $parts += "--ez arm64_context_promotion_gpr_local_slots_audit $(ConvertTo-BooleanText $Arm64ContextPromotionGprLocalSlotsAudit)"
    }
    if ($Arm64ContextPromotionGprLiveInR1) {
        $parts += "--ez arm64_context_promotion_gpr_livein_r1 $(ConvertTo-BooleanText $Arm64ContextPromotionGprLiveInR1)"
    }
    if ($Arm64ContextPromotionGprLiveInR1Function) {
        $parts += "--ei arm64_context_promotion_gpr_livein_r1_function $(ConvertTo-AdbIntText $Arm64ContextPromotionGprLiveInR1Function)"
    }
    if ($Arm64ContextPromotionGprLiveInR1PreserveBarrier) {
        $parts += "--ez arm64_context_promotion_gpr_livein_r1_preserve_barrier $(ConvertTo-BooleanText $Arm64ContextPromotionGprLiveInR1PreserveBarrier)"
    }
    if ($Arm64ContextPromotionGprLiveInR1Audit) {
        $parts += "--ez arm64_context_promotion_gpr_livein_r1_audit $(ConvertTo-BooleanText $Arm64ContextPromotionGprLiveInR1Audit)"
    }
    if ($Arm64GuestStateRegisterCacheAudit) {
        $parts += "--ez arm64_guest_state_register_cache_audit $(ConvertTo-BooleanText $Arm64GuestStateRegisterCacheAudit)"
    }
    if ($Arm64GuestStateRegisterCacheAuditFunction) {
        $parts += "--ei arm64_guest_state_register_cache_audit_function $(ConvertTo-AdbIntText $Arm64GuestStateRegisterCacheAuditFunction)"
    }
    if ($Arm64GuestStateRegisterCacheResidualAudit) {
        $parts += "--ez arm64_guest_state_register_cache_residual_audit $(ConvertTo-BooleanText $Arm64GuestStateRegisterCacheResidualAudit)"
    }
    if ($Arm64GuestStateRegisterCacheResidualAuditFunction) {
        $parts += "--ei arm64_guest_state_register_cache_residual_audit_function $(ConvertTo-AdbIntText $Arm64GuestStateRegisterCacheResidualAuditFunction)"
    }
    if ($Arm64RegisterAllocationAudit) {
        $parts += "--ez arm64_register_allocation_audit $(ConvertTo-BooleanText $Arm64RegisterAllocationAudit)"
    }
    if ($Arm64RegisterAllocationAuditFunction) {
        $parts += "--ei arm64_register_allocation_audit_function $(ConvertTo-AdbIntText $Arm64RegisterAllocationAuditFunction)"
    }
    if ($Arm64GuestCallFastEntryAudit) {
        $parts += "--ez arm64_guest_call_fast_entry_audit $(ConvertTo-BooleanText $Arm64GuestCallFastEntryAudit)"
    }
    if ($Arm64GuestCallFastEntryAuditFunction) {
        $parts += "--ei arm64_guest_call_fast_entry_audit_function $(ConvertTo-AdbIntText $Arm64GuestCallFastEntryAuditFunction)"
    }
    if ($Arm64GuestCallFastEntryAuditBudget) {
        $parts += "--ei arm64_guest_call_fast_entry_audit_budget $(ConvertTo-AdbIntText $Arm64GuestCallFastEntryAuditBudget)"
    }
    if ($Arm64GuestStackArgHandoffAudit) {
        $parts += "--ez arm64_guest_stack_arg_handoff_audit $(ConvertTo-BooleanText $Arm64GuestStackArgHandoffAudit)"
    }
    if ($Arm64GuestStackArgHandoffAuditFunction) {
        $parts += "--ei arm64_guest_stack_arg_handoff_audit_function $(ConvertTo-AdbIntText $Arm64GuestStackArgHandoffAuditFunction)"
    }
    if ($Arm64GuestStackArgHandoffAuditBudget) {
        $parts += "--ei arm64_guest_stack_arg_handoff_audit_budget $(ConvertTo-AdbIntText $Arm64GuestStackArgHandoffAuditBudget)"
    }
    if ($Arm64CrCompareBranchAcrossContextBarrier) {
        $parts += "--ez arm64_cr_compare_branch_across_context_barrier $(ConvertTo-BooleanText $Arm64CrCompareBranchAcrossContextBarrier)"
    }
    if ($Arm64CrStoreElideForFusedBranch) {
        $parts += "--ez arm64_cr_store_elide_for_fused_branch $(ConvertTo-BooleanText $Arm64CrStoreElideForFusedBranch)"
    }
    if ($Arm64CrStoreElideForFusedBranchFunction) {
        $parts += "--ei arm64_cr_store_elide_for_fused_branch_function $(ConvertTo-AdbIntText $Arm64CrStoreElideForFusedBranchFunction)"
    }
    if ($Arm64ContextTrafficAudit) {
        $parts += "--ez arm64_context_traffic_audit $(ConvertTo-BooleanText $Arm64ContextTrafficAudit)"
    }
    if ($Arm64ContextTrafficAuditFunction) {
        $parts += "--ei arm64_context_traffic_audit_function $(ConvertTo-AdbIntText $Arm64ContextTrafficAuditFunction)"
    }
    if ($Arm64ContextTrafficAuditBudget) {
        $parts += "--ei arm64_context_traffic_audit_budget $(ConvertTo-AdbIntText $Arm64ContextTrafficAuditBudget)"
    }
    if ($Arm64SpeedProfileBodyTimeFilter) {
        $parts += "--es arm64_speed_profile_body_time_filter $(ConvertTo-AdbShellSingleQuote $Arm64SpeedProfileBodyTimeFilter)"
    }
    if ($Arm64SpeedProfileEntryExitTimeFilter) {
        $parts += "--es arm64_speed_profile_entry_exit_time_filter $(ConvertTo-AdbShellSingleQuote $Arm64SpeedProfileEntryExitTimeFilter)"
    }
    if ($Arm64SpeedProfileBodyTimeAfterMs) {
        $parts += "--ei arm64_speed_profile_body_time_after_ms $Arm64SpeedProfileBodyTimeAfterMs"
    }
    if ($Arm64SpeedProfileBlockFilter) {
        $parts += "--es arm64_speed_profile_block_filter $(ConvertTo-AdbShellSingleQuote $Arm64SpeedProfileBlockFilter)"
    }
    if ($Arm64SpeedProfileBlockBodyTime) {
        $parts += "--ez arm64_speed_profile_block_body_time $(ConvertTo-BooleanText $Arm64SpeedProfileBlockBodyTime)"
    }
    if ($Arm64SpeedProfileCallEdgeFilter) {
        $parts += "--es arm64_speed_profile_call_edge_filter $(ConvertTo-AdbShellSingleQuote $Arm64SpeedProfileCallEdgeFilter)"
    }
    if ($Arm64SpeedProfileCallEdgeAuditOnly) {
        $parts += "--ez arm64_speed_profile_call_edge_audit_only $(ConvertTo-BooleanText $Arm64SpeedProfileCallEdgeAuditOnly)"
    }
    if ($Arm64SpeedProfileThreadSnapshot) {
        $parts += "--ez arm64_speed_profile_thread_snapshot $(ConvertTo-BooleanText $Arm64SpeedProfileThreadSnapshot)"
    }
    if ($Arm64SpeedProfileThreadSnapshotOnIdle) {
        $parts += "--ez arm64_speed_profile_thread_snapshot_on_idle $(ConvertTo-BooleanText $Arm64SpeedProfileThreadSnapshotOnIdle)"
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
        "title_screenshot_seconds=$TitleScreenshotSeconds",
        "stop_app_after_capture=$StopAppAfterCapture",
        "simpleperf=$Simpleperf",
        "simpleperf_start_second=$SimpleperfStartSecond",
        "simpleperf_seconds=$SimpleperfSeconds",
        "simpleperf_frequency=$SimpleperfFrequency",
        "simpleperf_event=$SimpleperfEvent",
        "simpleperf_call_graph=$SimpleperfCallGraph",
        "simpleperf_percent_limit=$SimpleperfPercentLimit",
        "shader_dump_device_path=$script:ActiveDumpShadersPath",
        "disassemble_functions=$DisassembleFunctions",
        "disassemble_function_filter=$DisassembleFunctionFilter",
        "arm64_speed_profile_interval_ms=$Arm64SpeedProfileIntervalMs",
        "arm64_speed_profile_top_functions=$Arm64SpeedProfileTopFunctions",
        "arm64_speed_profile_min_delta=$Arm64SpeedProfileMinDelta",
        "a64_inline_gprlr_helpers=$A64InlineGprLrHelpers",
        "a64_inline_fpr_helpers=$effectiveA64InlineFprHelpers",
        "a64_inline_vmx_helpers=$effectiveA64InlineVmxHelpers",
        "a64_inline_fpr_vmx_helpers=$A64InlineFprVmxHelpers",
        "a64_inline_ppc_thread_field_leaf_helpers=$A64InlinePpcThreadFieldLeafHelpers",
        "a64_inline_kernel_high_frequency_exports=$A64InlineKernelHighFrequencyExports",
        "a64_inline_kernel_spinlock_exports=$A64InlineKernelSpinlockExports",
        "a64_lse_kernel_lock_fastpaths=$A64LseKernelLockFastpaths",
        "a64_enable_host_guest_stack_synchronization=$A64EnableHostGuestStackSynchronization",
        "arm64_vmx_dot_f32_fastpath=$Arm64VmxDotF32Fastpath",
        "arm64_permute_i32_zip_fastpath=$Arm64PermuteI32ZipFastpath",
        "a64_rtl_enter_free_first=$A64RtlEnterFreeFirst",
        "a64_inline_rtl_leave_final_unlock=$A64InlineRtlLeaveFinalUnlock",
        "a64_rtl_leave_fastpath_audit=$A64RtlLeaveFastpathAudit",
        "a64_inline_kf_lower_irql=$A64InlineKfLowerIrql",
        "a64_inline_kf_lower_irql_apc_guard=$A64InlineKfLowerIrqlApcGuard",
        "a64_kf_lower_irql_apc_guard_audit=$A64KfLowerIrqlApcGuardAudit",
        "a64_kf_lower_irql_apc_guard_native_poll_interval=$A64KfLowerIrqlApcGuardNativePollInterval",
        "arm64_global_reservation_helpers=$Arm64GlobalReservationHelpers",
        "arm64_offset_memory_address_fastpath=$Arm64OffsetMemoryAddressFastpath",
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
        "arm64_blue_dragon_memcpy_fastpath=$Arm64BlueDragonMemcpyFastpath",
        "arm64_blue_dragon_stricmp_fastpath=$Arm64BlueDragonStricmpFastpath",
        "arm64_blue_dragon_stricmp_deferred_cr_fastpath=$Arm64BlueDragonStricmpDeferredCrFastpath",
        "arm64_blue_dragon_stricmp_return_profile=$Arm64BlueDragonStricmpReturnProfile",
        "arm64_blue_dragon_stricmp_return_profile_stride=$Arm64BlueDragonStricmpReturnProfileStride",
        "arm64_blue_dragon_stricmp_return_profile_budget=$Arm64BlueDragonStricmpReturnProfileBudget",
        "arm64_blue_dragon_jump_table_fastpath=$Arm64BlueDragonJumpTableFastpath",
        "arm64_blue_dragon_jump_table_inline_in_caller=$Arm64BlueDragonJumpTableInlineInCaller",
        "arm64_blue_dragon_vmx_copy_loop_fastpath=$Arm64BlueDragonVmxCopyLoopFastpath",
        "arm64_blue_dragon_word_copy_loop_fastpath=$Arm64BlueDragonWordCopyLoopFastpath",
        "arm64_blue_dragon_stvewx_stack_lane_fastpath=$Arm64BlueDragonStvewxStackLaneFastpath",
        "arm64_blue_dragon_stvewx_stack_lane_audit=$Arm64BlueDragonStvewxStackLaneAudit",
        "arm64_blue_dragon_mul_add_v128_fastpath=$Arm64BlueDragonMulAddV128Fastpath",
        "arm64_blue_dragon_mul_add_v128_audit=$Arm64BlueDragonMulAddV128Audit",
        "arm64_blue_dragon_call_boundary_state_audit=$Arm64BlueDragonCallBoundaryStateAudit",
        "arm64_blue_dragon_call_boundary_state_suppress_dead_stores=$Arm64BlueDragonCallBoundaryStateSuppressDeadStores",
        "arm64_blue_dragon_f1_carrier_audit=$Arm64BlueDragonF1CarrierAudit",
        "arm64_blue_dragon_f1_carrier_fastpath=$Arm64BlueDragonF1CarrierFastpath",
        "arm64_blue_dragon_state_carrier_design_audit=$Arm64BlueDragonStateCarrierDesignAudit",
        "arm64_blue_dragon_edge_variant_audit=$Arm64BlueDragonEdgeVariantAudit",
        "arm64_blue_dragon_edge_payload_storage_audit=$Arm64BlueDragonEdgePayloadStorageAudit",
        "arm64_blue_dragon_fpscr_cfg_writeback_audit=$Arm64BlueDragonFpscrCfgWritebackAudit",
        "arm64_add_sub_imm_audit=$Arm64AddSubImmAudit",
        "arm64_add_sub_imm_audit_function=$Arm64AddSubImmAuditFunction",
        "arm64_add_sub_imm_audit_budget=$Arm64AddSubImmAuditBudget",
        "arm64_add_i64_wrapped_imm_fastpath=$Arm64AddI64WrappedImmFastpath",
        "arm64_add_i64_wrapped_imm_fastpath_function=$Arm64AddI64WrappedImmFastpathFunction",
        "arm64_immediate_lowering_audit=$Arm64ImmediateLoweringAudit",
        "arm64_immediate_lowering_audit_function=$Arm64ImmediateLoweringAuditFunction",
        "arm64_immediate_lowering_audit_budget=$Arm64ImmediateLoweringAuditBudget",
        "arm64_context_value_cache=$Arm64ContextValueCache",
        "arm64_context_value_cache_fallthrough=$Arm64ContextValueCacheFallthrough",
        "arm64_context_value_cache_preserve_barrier=$Arm64ContextValueCachePreserveBarrier",
        "arm64_context_pinned_gpr_r1=$Arm64ContextPinnedGprR1",
        "arm64_context_pinned_gpr_r1_fallthrough=$Arm64ContextPinnedGprR1Fallthrough",
        "arm64_context_pinned_gpr_r1_function=$Arm64ContextPinnedGprR1Function",
        "arm64_context_pinned_gpr_r1_audit=$Arm64ContextPinnedGprR1Audit",
        "arm64_context_promotion_gpr_local_slots=$Arm64ContextPromotionGprLocalSlots",
        "arm64_context_promotion_gpr_local_slots_function=$Arm64ContextPromotionGprLocalSlotsFunction",
        "arm64_context_promotion_gpr_local_slots_audit=$Arm64ContextPromotionGprLocalSlotsAudit",
        "arm64_context_promotion_gpr_livein_r1=$Arm64ContextPromotionGprLiveInR1",
        "arm64_context_promotion_gpr_livein_r1_function=$Arm64ContextPromotionGprLiveInR1Function",
        "arm64_context_promotion_gpr_livein_r1_preserve_barrier=$Arm64ContextPromotionGprLiveInR1PreserveBarrier",
        "arm64_context_promotion_gpr_livein_r1_audit=$Arm64ContextPromotionGprLiveInR1Audit",
        "arm64_guest_state_register_cache_audit=$Arm64GuestStateRegisterCacheAudit",
        "arm64_guest_state_register_cache_audit_function=$Arm64GuestStateRegisterCacheAuditFunction",
        "arm64_guest_state_register_cache_residual_audit=$Arm64GuestStateRegisterCacheResidualAudit",
        "arm64_guest_state_register_cache_residual_audit_function=$Arm64GuestStateRegisterCacheResidualAuditFunction",
        "arm64_register_allocation_audit=$Arm64RegisterAllocationAudit",
        "arm64_register_allocation_audit_function=$Arm64RegisterAllocationAuditFunction",
        "arm64_guest_call_fast_entry_audit=$Arm64GuestCallFastEntryAudit",
        "arm64_guest_call_fast_entry_audit_function=$Arm64GuestCallFastEntryAuditFunction",
        "arm64_guest_call_fast_entry_audit_budget=$Arm64GuestCallFastEntryAuditBudget",
        "arm64_guest_stack_arg_handoff_audit=$Arm64GuestStackArgHandoffAudit",
        "arm64_guest_stack_arg_handoff_audit_function=$Arm64GuestStackArgHandoffAuditFunction",
        "arm64_guest_stack_arg_handoff_audit_budget=$Arm64GuestStackArgHandoffAuditBudget",
        "arm64_cr_compare_branch_across_context_barrier=$Arm64CrCompareBranchAcrossContextBarrier",
        "arm64_cr_store_elide_for_fused_branch=$Arm64CrStoreElideForFusedBranch",
        "arm64_cr_store_elide_for_fused_branch_function=$Arm64CrStoreElideForFusedBranchFunction",
        "arm64_context_traffic_audit=$Arm64ContextTrafficAudit",
        "arm64_context_traffic_audit_function=$Arm64ContextTrafficAuditFunction",
        "arm64_context_traffic_audit_budget=$Arm64ContextTrafficAuditBudget",
        "arm64_speed_profile_body_time_filter=$Arm64SpeedProfileBodyTimeFilter",
        "arm64_speed_profile_entry_exit_time_filter=$Arm64SpeedProfileEntryExitTimeFilter",
        "arm64_speed_profile_body_time_after_ms=$Arm64SpeedProfileBodyTimeAfterMs",
        "arm64_speed_profile_block_filter=$Arm64SpeedProfileBlockFilter",
        "arm64_speed_profile_block_body_time=$Arm64SpeedProfileBlockBodyTime",
        "arm64_speed_profile_call_edge_filter=$Arm64SpeedProfileCallEdgeFilter",
        "arm64_speed_profile_call_edge_audit_only=$Arm64SpeedProfileCallEdgeAuditOnly",
        "arm64_speed_profile_thread_snapshot=$Arm64SpeedProfileThreadSnapshot",
        "arm64_speed_profile_thread_snapshot_on_idle=$Arm64SpeedProfileThreadSnapshotOnIdle",
        "xma_fast_silence=$XmaFastSilence",
        "log_level=$LogLevel",
        "gpu_unknown_register_log_budget=$GpuUnknownRegisterLogBudget",
        "xboxkrnl_nt_create_file_fail_log_budget=$XboxkrnlNtCreateFileFailLogBudget",
        "hide_android_osd=$HideAndroidOsd",
        "hid_driver=$HidDriver",
        "hid_nop_connected=$HidNopConnected",
        "hid_nop_button_sequence=$HidNopButtonSequence",
        "vulkan_force_signed_2101010_unorm_fallback=$VulkanForceSigned2101010UnormFallback",
        "vulkan_force_2101010_rgba8_fallback=$VulkanForce2101010Rgba8Fallback",
        "vulkan_trace_perf_counters=$VulkanTracePerfCounters",
        "vulkan_trace_perf_counters_log_interval=$VulkanTracePerfCountersLogInterval",
        "",
        "adb_events:",
        ($script:AdbEvents -join "`n"),
        "",
        "activity:",
        $focused
    ) | Out-File -Encoding utf8 $MetaPath
}

function Stop-XeniaAfterCapture {
    param(
        [string]$Reason,
        [string]$MetaPath
    )

    if ((ConvertTo-BooleanText $StopAppAfterCapture) -ne "true") {
        return
    }

    $beforePid = (Invoke-Adb @("shell", "pidof", $PackageName)) -join " "
    Invoke-Adb @("shell", "am", "force-stop", $PackageName) | Out-Null
    Start-Sleep -Milliseconds 250
    $afterPid = (Invoke-Adb @("shell", "pidof", $PackageName)) -join " "
    Add-AdbEvent "post-capture force-stop reason=$Reason before_pid=$beforePid after_pid=$afterPid"

    if ($MetaPath) {
        @(
            "",
            "post_capture_cleanup:",
            "stop_app_after_capture=$StopAppAfterCapture",
            "reason=$Reason",
            "before_pid=$beforePid",
            "after_pid=$afterPid"
        ) | Out-File -Encoding utf8 -Append $MetaPath
    }

    Write-Output "Post-capture force-stop: before_pid=$beforePid after_pid=$afterPid"
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

function Resolve-SimpleperfHostPath {
    $candidates = @()
    foreach ($root in @($env:ANDROID_NDK_HOME, $env:ANDROID_NDK_ROOT)) {
        if ($root) {
            $candidates += (Join-Path $root "simpleperf\bin\windows\x86_64\simpleperf.exe")
        }
    }
    $sdkRoot = $env:ANDROID_HOME
    if (!$sdkRoot) {
        $sdkRoot = $env:ANDROID_SDK_ROOT
    }
    if (!$sdkRoot) {
        $sdkRoot = Join-Path $env:LOCALAPPDATA "Android\Sdk"
    }
    if ($sdkRoot -and (Test-Path (Join-Path $sdkRoot "ndk"))) {
        $ndkDirs = Get-ChildItem -Path (Join-Path $sdkRoot "ndk") -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending
        foreach ($ndkDir in $ndkDirs) {
            $candidates += (Join-Path $ndkDir.FullName "simpleperf\bin\windows\x86_64\simpleperf.exe")
        }
    }

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return (Resolve-Path $candidate).Path
        }
    }
    return ""
}

function Resolve-SimpleperfSymbolDir {
    $symbolDir = Join-Path $RepoRoot "android\android_studio_project\app\build\intermediates\ndkBuild\githubDebug\obj\local\arm64-v8a"
    if (Test-Path (Join-Path $symbolDir "libxenia-app.so")) {
        return (Resolve-Path $symbolDir).Path
    }
    return ""
}

function Write-SimpleperfCapture {
    param(
        [string]$Stamp,
        [string]$Label,
        [string]$OutDir
    )

    $safeLabel = $Label -replace "[^A-Za-z0-9_.-]", "_"
    $deviceBase = "/data/local/tmp/xenia-thor-$Stamp-$safeLabel"
    $deviceData = "$deviceBase.data"
    $deviceRecordLog = "$deviceBase-record.txt"
    $localDataPath = Join-Path $OutDir "$Stamp-simpleperf-$safeLabel.data"
    $localRecordPath = Join-Path $OutDir "$Stamp-simpleperf-$safeLabel-record.txt"
    $localReportPath = Join-Path $OutDir "$Stamp-simpleperf-$safeLabel-report.txt"

    $recordCommand = @(
        "rm -f $deviceData $deviceRecordLog",
        "simpleperf record --app $PackageName -e $SimpleperfEvent -f $SimpleperfFrequency --duration $SimpleperfSeconds --call-graph $SimpleperfCallGraph -o $deviceData > $deviceRecordLog 2>&1",
        "echo SIMPLEPERF_RECORD_EXIT:`$?",
        "cat $deviceRecordLog",
        "ls -l $deviceData 2>/dev/null"
    ) -join "; "
    $recordLines = Invoke-AdbShellCommand $recordCommand
    $recordLines | Out-File -Encoding utf8 $localRecordPath

    Invoke-AdbPullToFile $deviceData $localDataPath | Out-File -Encoding utf8 -Append $localRecordPath

    $hostSimpleperf = Resolve-SimpleperfHostPath
    $symbolDir = Resolve-SimpleperfSymbolDir
    if ($hostSimpleperf -and (Test-Path $localDataPath)) {
        $reportArgs = @(
            "report",
            "-i", $localDataPath,
            "--sort", "comm,dso,symbol",
            "-n",
            "--children",
            "--percent-limit", $SimpleperfPercentLimit
        )
        if ($symbolDir) {
            $reportArgs += @("--symdir", $symbolDir)
        }
        & $hostSimpleperf @reportArgs 2>&1 | Out-File -Encoding utf8 $localReportPath
    } else {
        @(
            "simpleperf_report_skipped=true",
            "host_simpleperf=$hostSimpleperf",
            "local_data_exists=$(Test-Path $localDataPath)",
            "symbol_dir=$symbolDir"
        ) | Out-File -Encoding utf8 $localReportPath
    }

    return @{
        Data = $localDataPath
        Record = $localRecordPath
        Report = $localReportPath
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

function Set-DefaultIfNotBound {
    param(
        [string]$Name,
        [string]$Value
    )

    if (!$script:RootBoundParameters.ContainsKey($Name)) {
        Set-Variable -Name $Name -Scope Script -Value $Value
    }
}

function Test-BlueDragonSpeedLogRowsRequested {
    if ($script:Arm64SpeedProfileIntervalMs -or
        $script:Arm64SpeedProfileTopFunctions -or
        $script:Arm64SpeedProfileMinDelta -or
        $script:Arm64SpeedProfileBodyTimeFilter -or
        $script:Arm64SpeedProfileEntryExitTimeFilter -or
        $script:Arm64SpeedProfileBlockFilter -or
        $script:Arm64SpeedProfileCallEdgeFilter) {
        return $true
    }

    foreach ($name in @(
        "A64RtlLeaveFastpathAudit",
        "A64KfLowerIrqlApcGuardAudit",
        "Arm64BlueDragonDrawWaitCallerProfile",
        "Arm64BlueDragonStricmpReturnProfile",
        "Arm64BlueDragonStvewxStackLaneAudit",
        "Arm64BlueDragonMulAddV128Audit",
        "Arm64BlueDragonCallBoundaryStateAudit",
        "Arm64BlueDragonF1CarrierAudit",
        "Arm64BlueDragonStateCarrierDesignAudit",
        "Arm64BlueDragonEdgeVariantAudit",
        "Arm64BlueDragonEdgePayloadStorageAudit",
        "Arm64BlueDragonFpscrCfgWritebackAudit",
        "Arm64AddSubImmAudit",
        "Arm64ImmediateLoweringAudit",
        "Arm64ContextPinnedGprR1Audit",
        "Arm64ContextPromotionGprLocalSlotsAudit",
        "Arm64ContextPromotionGprLiveInR1Audit",
        "Arm64GuestStateRegisterCacheAudit",
        "Arm64GuestStateRegisterCacheResidualAudit",
        "Arm64RegisterAllocationAudit",
        "Arm64ContextTrafficAudit",
        "Arm64SpeedProfileBlockBodyTime",
        "Arm64SpeedProfileCallEdgeAuditOnly",
        "Arm64SpeedProfileThreadSnapshot",
        "Arm64SpeedProfileThreadSnapshotOnIdle"
    )) {
        $value = (Get-Variable -Name $name -Scope Script -ValueOnly -ErrorAction SilentlyContinue)
        if (Test-TrueLikeText $value) {
            return $true
        }
    }

    return $false
}

function Set-BlueDragonSpeedDefaultLogLevel {
    if (!$script:RootBoundParameters.ContainsKey("LogLevel")) {
        $script:LogLevel = "0"
        if (Test-BlueDragonSpeedLogRowsRequested) {
            $script:LogLevel = "1"
        }
    }
}

function Test-BlueDragonSpeedProfilerRowsRequested {
    if ($script:Arm64SpeedProfileTopFunctions -or
        $script:Arm64SpeedProfileMinDelta -or
        $script:Arm64SpeedProfileBodyTimeFilter -or
        $script:Arm64SpeedProfileEntryExitTimeFilter -or
        $script:Arm64SpeedProfileBlockFilter -or
        $script:Arm64SpeedProfileCallEdgeFilter) {
        return $true
    }

    foreach ($name in @(
        "A64RtlLeaveFastpathAudit",
        "A64KfLowerIrqlApcGuardAudit",
        "Arm64BlueDragonStvewxStackLaneAudit",
        "Arm64BlueDragonMulAddV128Audit",
        "Arm64BlueDragonCallBoundaryStateAudit",
        "Arm64BlueDragonF1CarrierAudit",
        "Arm64BlueDragonStateCarrierDesignAudit",
        "Arm64BlueDragonEdgeVariantAudit",
        "Arm64BlueDragonEdgePayloadStorageAudit",
        "Arm64BlueDragonFpscrCfgWritebackAudit",
        "Arm64SpeedProfileBlockBodyTime",
        "Arm64SpeedProfileCallEdgeAuditOnly",
        "Arm64SpeedProfileThreadSnapshot",
        "Arm64SpeedProfileThreadSnapshotOnIdle"
    )) {
        $value = (Get-Variable -Name $name -Scope Script -ValueOnly -ErrorAction SilentlyContinue)
        if (Test-TrueLikeText $value) {
            return $true
        }
    }

    return $false
}

function Set-BlueDragonSpeedDefaultProfilerInterval {
    if (!$script:RootBoundParameters.ContainsKey("Arm64SpeedProfileIntervalMs") -and
        (Test-BlueDragonSpeedProfilerRowsRequested)) {
        $script:Arm64SpeedProfileIntervalMs = "15000"
    }
}

function Use-BlueDragonA64SpeedPack {
    Set-DefaultIfNotBound "Arm64ContextValueCache" "false"
    Set-DefaultIfNotBound "Arm64ContextValueCacheFallthrough" "false"
    Set-DefaultIfNotBound "Arm64ContextValueCachePreserveBarrier" "false"
    Set-DefaultIfNotBound "Arm64ContextPinnedGprR1" "false"
    Set-DefaultIfNotBound "Arm64ContextPinnedGprR1Fallthrough" "false"
    Set-DefaultIfNotBound "Arm64ContextPinnedGprR1Function" ""
    Set-DefaultIfNotBound "Arm64ContextPinnedGprR1Audit" "false"
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLocalSlots" "false"
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLocalSlotsFunction" ""
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLocalSlotsAudit" "false"
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLiveInR1" "false"
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLiveInR1Function" ""
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLiveInR1PreserveBarrier" "true"
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLiveInR1Audit" "false"
    Set-DefaultIfNotBound "Arm64GuestStateRegisterCacheAudit" "false"
    Set-DefaultIfNotBound "Arm64GuestStateRegisterCacheAuditFunction" ""
    Set-DefaultIfNotBound "Arm64GuestStateRegisterCacheResidualAudit" "false"
    Set-DefaultIfNotBound "Arm64GuestStateRegisterCacheResidualAuditFunction" ""
    Set-DefaultIfNotBound "Arm64RegisterAllocationAudit" "false"
    Set-DefaultIfNotBound "Arm64RegisterAllocationAuditFunction" ""
    Set-DefaultIfNotBound "Arm64GuestCallFastEntryAudit" "false"
    Set-DefaultIfNotBound "Arm64GuestCallFastEntryAuditFunction" ""
    Set-DefaultIfNotBound "Arm64GuestCallFastEntryAuditBudget" ""
    Set-DefaultIfNotBound "Arm64GuestStackArgHandoffAudit" "false"
    Set-DefaultIfNotBound "Arm64GuestStackArgHandoffAuditFunction" ""
    Set-DefaultIfNotBound "Arm64GuestStackArgHandoffAuditBudget" ""
    Set-DefaultIfNotBound "Arm64CrCompareBranchAcrossContextBarrier" "false"
    Set-DefaultIfNotBound "Arm64CrStoreElideForFusedBranch" "false"
    Set-DefaultIfNotBound "A64EnableHostGuestStackSynchronization" "true"
    Set-DefaultIfNotBound "A64InlinePpcThreadFieldLeafHelpers" "true"
    Set-DefaultIfNotBound "Arm64BlueDragonDrawWaitProbe" "true"
    Set-DefaultIfNotBound "Arm64BlueDragonDrawWaitFastpath" "true"
    Set-DefaultIfNotBound "Arm64BlueDragonDrawWaitFastpathHostCounterTime" "true"
    Set-DefaultIfNotBound "Arm64BlueDragonDrawWaitInlineInCaller" "true"
    Set-DefaultIfNotBound "Arm64BlueDragonMemcpyFastpath" "true"
    Set-DefaultIfNotBound "Arm64BlueDragonStricmpFastpath" "true"
    Set-DefaultIfNotBound "Arm64BlueDragonStricmpDeferredCrFastpath" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonJumpTableFastpath" "true"
    Set-DefaultIfNotBound "Arm64BlueDragonJumpTableInlineInCaller" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonWordCopyLoopFastpath" "true"
    Set-DefaultIfNotBound "Arm64BlueDragonStvewxStackLaneFastpath" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonStvewxStackLaneAudit" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonMulAddV128Fastpath" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonMulAddV128Audit" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonCallBoundaryStateAudit" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonCallBoundaryStateSuppressDeadStores" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonF1CarrierAudit" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonF1CarrierFastpath" "false"
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
    Use-BlueDragonA64SpeedPack
    $script:HideAndroidOsd = "true"
    $script:HidDriver = "nop"
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
    Set-BlueDragonSpeedDefaultLogLevel
    Set-BlueDragonSpeedDefaultProfilerInterval
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
        "Arm64GlobalReservationHelpers",
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

function Use-BlueDragonTitleDefaults {
    Set-DefaultIfNotBound "Arm64ContextValueCache" "false"
    Set-DefaultIfNotBound "Arm64ContextValueCacheFallthrough" "false"
    Set-DefaultIfNotBound "Arm64ContextValueCachePreserveBarrier" "false"
    Set-DefaultIfNotBound "Arm64ContextPinnedGprR1" "false"
    Set-DefaultIfNotBound "Arm64ContextPinnedGprR1Fallthrough" "false"
    Set-DefaultIfNotBound "Arm64ContextPinnedGprR1Function" ""
    Set-DefaultIfNotBound "Arm64ContextPinnedGprR1Audit" "false"
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLocalSlots" "false"
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLocalSlotsFunction" ""
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLocalSlotsAudit" "false"
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLiveInR1" "false"
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLiveInR1Function" ""
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLiveInR1PreserveBarrier" "true"
    Set-DefaultIfNotBound "Arm64ContextPromotionGprLiveInR1Audit" "false"
    Set-DefaultIfNotBound "Arm64GuestStateRegisterCacheAudit" "false"
    Set-DefaultIfNotBound "Arm64GuestStateRegisterCacheAuditFunction" ""
    Set-DefaultIfNotBound "Arm64GuestStateRegisterCacheResidualAudit" "false"
    Set-DefaultIfNotBound "Arm64GuestStateRegisterCacheResidualAuditFunction" ""
    Set-DefaultIfNotBound "Arm64RegisterAllocationAudit" "false"
    Set-DefaultIfNotBound "Arm64RegisterAllocationAuditFunction" ""
    Set-DefaultIfNotBound "Arm64GuestCallFastEntryAudit" "false"
    Set-DefaultIfNotBound "Arm64GuestCallFastEntryAuditFunction" ""
    Set-DefaultIfNotBound "Arm64GuestCallFastEntryAuditBudget" ""
    Set-DefaultIfNotBound "Arm64GuestStackArgHandoffAudit" "false"
    Set-DefaultIfNotBound "Arm64GuestStackArgHandoffAuditFunction" ""
    Set-DefaultIfNotBound "Arm64GuestStackArgHandoffAuditBudget" ""
    Set-DefaultIfNotBound "Arm64CrCompareBranchAcrossContextBarrier" "false"
    Set-DefaultIfNotBound "Arm64CrStoreElideForFusedBranch" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonStvewxStackLaneFastpath" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonStvewxStackLaneAudit" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonMulAddV128Fastpath" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonMulAddV128Audit" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonCallBoundaryStateAudit" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonCallBoundaryStateSuppressDeadStores" "false"
    Set-DefaultIfNotBound "Arm64BlueDragonF1CarrierAudit" "false"
    $script:HideAndroidOsd = "false"
    $script:HidNopConnected = "false"
    $script:HidNopButtonSequence = ""
    $script:VulkanForceSigned2101010UnormFallback = "false"
    $script:VulkanForce2101010Rgba8Fallback = "false"
    Set-BlueDragonSpeedDefaultLogLevel
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
        } finally {
            Stop-XeniaAfterCapture "live capture complete" $MetaPath
        }
        Write-Output "Log: $LogPath"
        Write-Output "Filtered log: $FilteredLogPath"
        Write-Output "Log errors: $LogErrorPath"
        Write-Output "Meta: $MetaPath"
        Write-Output "Screenshot: $ScreenshotPath"
        if ($ShaderDumpPath) {
            Write-Output "Shader dumps: $ShaderDumpPath"
        }
    }
    "LaunchBlueDragonTitleCapture" {
        if (!$PSBoundParameters.ContainsKey("LiveCaptureSeconds")) {
            $LiveCaptureSeconds = 45
        }
        Use-BlueDragonTitleDefaults
        $Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $LogPath = Join-Path $OutDir "$Stamp-title-logcat.txt"
        $LogErrorPath = Join-Path $OutDir "$Stamp-title-logcat.err.txt"
        $FilteredLogPath = Join-Path $OutDir "$Stamp-title-logcat-filtered.txt"
        $MetaPath = Join-Path $OutDir "$Stamp-meta.txt"
        $ScreenshotPath = Join-Path $OutDir "$Stamp-screenshot.png"
        $launchTarget = Resolve-BlueDragonLaunchTarget
        $titleScreenshotSamples = @(Get-PerfSampleSecondValues $TitleScreenshotSeconds $LiveCaptureSeconds)
        $titleScreenshotPaths = @()
        $elapsedSeconds = 0
        Write-Output "Launching target: $launchTarget"
        Write-Output "Title capture seconds: $LiveCaptureSeconds"
        Write-Output "Title screenshot seconds: $($titleScreenshotSamples -join ',')"
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
            try {
                Start-XeniaEmulator $launchTarget -SkipForceStop -SkipLogcatClear
                foreach ($sampleSecond in $titleScreenshotSamples) {
                    if ($sampleSecond -gt $elapsedSeconds) {
                        Start-Sleep -Seconds ($sampleSecond - $elapsedSeconds)
                        $elapsedSeconds = $sampleSecond
                    }
                    $sampleScreenshotPath = Join-Path $OutDir "$Stamp-title-${sampleSecond}s-screenshot.png"
                    Invoke-AdbExecOutToFile "screencap -p" $sampleScreenshotPath
                    $titleScreenshotPaths += $sampleScreenshotPath
                }
                if ($LiveCaptureSeconds -gt $elapsedSeconds) {
                    Start-Sleep -Seconds ($LiveCaptureSeconds - $elapsedSeconds)
                }
            } finally {
                Stop-LiveLogcat $logcatProcess
            }

            Write-FilteredLog $LogPath $FilteredLogPath
            $ShaderDumpPath = Pull-ActiveShaderDumps $Stamp $OutDir
            Write-CaptureMetadata $Stamp $MetaPath $launchTarget
            Invoke-AdbExecOutToFile "screencap -p" $ScreenshotPath
        } finally {
            Stop-XeniaAfterCapture "title capture complete" $MetaPath
        }
        Write-Output "Log: $LogPath"
        Write-Output "Filtered log: $FilteredLogPath"
        Write-Output "Log errors: $LogErrorPath"
        Write-Output "Meta: $MetaPath"
        Write-Output "Screenshot: $ScreenshotPath"
        foreach ($titleScreenshotPath in $titleScreenshotPaths) {
            Write-Output "Title screenshot: $titleScreenshotPath"
        }
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
        $simpleperfPaths = @()
        $simpleperfEnabled = (ConvertTo-BooleanText $Simpleperf) -eq "true"
        $simpleperfRan = $false
        $elapsedSeconds = 0

        Use-BlueDragonSpeedDefaults
        Write-Output "Launching target: $launchTarget"
        Write-Output "Speed sample seconds: $($sampleSeconds -join ',')"
        if ($simpleperfEnabled) {
            Write-Output "Simpleperf: start=${SimpleperfStartSecond}s duration=${SimpleperfSeconds}s event=$SimpleperfEvent freq=$SimpleperfFrequency callgraph=$SimpleperfCallGraph"
        }
        if ($Arm64SpeedProfileIntervalMs) {
            Write-Output "A64 speed profile interval: ${Arm64SpeedProfileIntervalMs}ms"
        }
        if ($DisassembleFunctionFilter) {
            Write-Output "Disassemble function filter: $DisassembleFunctionFilter"
        }
        if ($Arm64SpeedProfileBodyTimeFilter) {
            Write-Output "A64 body-time filter: $Arm64SpeedProfileBodyTimeFilter"
        }
        if ($Arm64SpeedProfileEntryExitTimeFilter) {
            Write-Output "A64 entry/exit-time filter: $Arm64SpeedProfileEntryExitTimeFilter"
        }
        if ($Arm64SpeedProfileBodyTimeAfterMs) {
            Write-Output "A64 body-time after: ${Arm64SpeedProfileBodyTimeAfterMs}ms"
        }
        if ($Arm64SpeedProfileBlockFilter) {
            Write-Output "A64 block filter: $Arm64SpeedProfileBlockFilter"
        }
        if ($Arm64SpeedProfileBlockBodyTime) {
            Write-Output "A64 block body-time: $(ConvertTo-BooleanText $Arm64SpeedProfileBlockBodyTime)"
        }
        if ($Arm64SpeedProfileCallEdgeFilter) {
            Write-Output "A64 call-edge filter: $Arm64SpeedProfileCallEdgeFilter"
        }
        if ($Arm64SpeedProfileCallEdgeAuditOnly) {
            Write-Output "A64 call-edge audit-only: $(ConvertTo-BooleanText $Arm64SpeedProfileCallEdgeAuditOnly)"
        }
        if ($Arm64BlueDragonMulAddV128Audit) {
            Write-Output "A64 Blue Dragon MUL_ADD_V128 audit: $(ConvertTo-BooleanText $Arm64BlueDragonMulAddV128Audit)"
        }
        if ($Arm64BlueDragonMulAddV128Fastpath) {
            Write-Output "A64 Blue Dragon MUL_ADD_V128 fastpath: $(ConvertTo-BooleanText $Arm64BlueDragonMulAddV128Fastpath)"
        }
        if ($Arm64BlueDragonCallBoundaryStateAudit) {
            Write-Output "A64 Blue Dragon call-boundary state audit: $(ConvertTo-BooleanText $Arm64BlueDragonCallBoundaryStateAudit)"
        }
        if ($Arm64BlueDragonCallBoundaryStateSuppressDeadStores) {
            Write-Output "A64 Blue Dragon call-boundary state suppress dead stores: $(ConvertTo-BooleanText $Arm64BlueDragonCallBoundaryStateSuppressDeadStores)"
        }
        if ($Arm64BlueDragonF1CarrierAudit) {
            Write-Output "A64 Blue Dragon f1 carrier audit: $(ConvertTo-BooleanText $Arm64BlueDragonF1CarrierAudit)"
        }
        if ($Arm64BlueDragonF1CarrierFastpath) {
            Write-Output "A64 Blue Dragon f1 carrier fastpath: $(ConvertTo-BooleanText $Arm64BlueDragonF1CarrierFastpath)"
        }
        if ($Arm64BlueDragonEdgePayloadStorageAudit) {
            Write-Output "A64 Blue Dragon edge payload storage audit: $(ConvertTo-BooleanText $Arm64BlueDragonEdgePayloadStorageAudit)"
        }
        if ($VulkanTracePerfCounters) {
            Write-Output "Vulkan perf counters: $(ConvertTo-BooleanText $VulkanTracePerfCounters)"
        }
        if ($VulkanTracePerfCountersLogInterval) {
            Write-Output "Vulkan perf counter log interval: $VulkanTracePerfCountersLogInterval"
        }
        try {
            Invoke-Adb @("shell", "am", "force-stop", $PackageName) | Out-Null
            Invoke-Adb @("logcat", "-c") | Out-Null
            Start-XeniaEmulator $launchTarget -SkipForceStop -SkipLogcatClear

            foreach ($sampleSecond in $sampleSeconds) {
                if ($simpleperfEnabled -and !$simpleperfRan -and
                    $SimpleperfStartSecond -le $sampleSecond -and
                    $SimpleperfSeconds -gt 0) {
                    if ($SimpleperfStartSecond -gt $elapsedSeconds) {
                        Start-Sleep -Seconds ($SimpleperfStartSecond - $elapsedSeconds)
                        $elapsedSeconds = $SimpleperfStartSecond
                    }
                    $simpleperfPaths += Write-SimpleperfCapture $Stamp "${SimpleperfStartSecond}s-${SimpleperfSeconds}s" $OutDir
                    $elapsedSeconds += $SimpleperfSeconds
                    $simpleperfRan = $true
                }
                if ($sampleSecond -gt $elapsedSeconds) {
                    Start-Sleep -Seconds ($sampleSecond - $elapsedSeconds)
                    $elapsedSeconds = $sampleSecond
                }
                $perfPaths += Write-PerfSnapshot $Stamp "${sampleSecond}s" $OutDir
            }
            if ($simpleperfEnabled -and !$simpleperfRan -and
                $SimpleperfStartSecond -le $LiveCaptureSeconds -and
                $SimpleperfSeconds -gt 0) {
                if ($SimpleperfStartSecond -gt $elapsedSeconds) {
                    Start-Sleep -Seconds ($SimpleperfStartSecond - $elapsedSeconds)
                    $elapsedSeconds = $SimpleperfStartSecond
                }
                $simpleperfPaths += Write-SimpleperfCapture $Stamp "${SimpleperfStartSecond}s-${SimpleperfSeconds}s" $OutDir
                $elapsedSeconds += $SimpleperfSeconds
                $simpleperfRan = $true
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
        } finally {
            Stop-XeniaAfterCapture "speed capture complete" $MetaPath
        }
        Write-Output "Log: $LogPath"
        Write-Output "Filtered log: $FilteredLogPath"
        Write-Output "Meta: $MetaPath"
        Write-Output "Screenshot: $ScreenshotPath"
        foreach ($perfPath in $perfPaths) {
            Write-Output "Perf: $perfPath"
        }
        foreach ($simpleperfPath in $simpleperfPaths) {
            Write-Output "Simpleperf data: $($simpleperfPath.Data)"
            Write-Output "Simpleperf record: $($simpleperfPath.Record)"
            Write-Output "Simpleperf report: $($simpleperfPath.Report)"
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
