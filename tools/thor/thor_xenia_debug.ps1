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
        "StopNoise",
        "Capture")]
    [string]$Mode = "DeviceInfo",
    [string]$OutDir = "",
    [string]$Target = "",
    [string]$Arm64MiniJit = "true",
    [string]$Arm64MiniJitBlacklist = "",
    [string]$Arm64ForceInterpreterRanges = "",
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
    param([string]$LaunchTarget)
    Invoke-Adb @("logcat", "-c")
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
        "--ez discord false")
    if ($Arm64MiniJitBlacklist) {
        $parts += "--es arm64_mini_jit_blacklist $(ConvertTo-AdbShellSingleQuote $Arm64MiniJitBlacklist)"
    }
    if ($Arm64ForceInterpreterRanges) {
        $parts += "--es arm64_force_interpreter_guest_ranges $(ConvertTo-AdbShellSingleQuote $Arm64ForceInterpreterRanges)"
    }
    if ($LaunchTarget) {
        $parts += "--es target $(ConvertTo-AdbShellSingleQuote $LaunchTarget)"
    }
    Invoke-AdbShellCommand ($parts -join " ")
    Start-Sleep -Seconds 5
    Invoke-Adb @("shell", "pidof", $PackageName)
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
        $launchTarget = $Target
        if (!$launchTarget) {
            $launchTarget = Find-BlueDragonTarget
        }
        if (!$launchTarget) {
            $launchTarget = $BlueDragonDisc1
        }
        Write-Output "Launching target: $launchTarget"
        Start-XeniaEmulator $launchTarget
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
        if ($LogFilter) {
            $logcat | Select-String -Pattern $LogFilter |
                Out-File -Encoding utf8 $FilteredLogPath
        }
        $branch = (& git -C $RepoRoot branch --show-current) 2>$null
        $head = (& git -C $RepoRoot rev-parse --short HEAD) 2>$null
        $apkHash = ""
        if (Test-Path $ApkPath) {
            $apkHash = (Get-FileHash -Algorithm SHA256 $ApkPath).Hash
        }
        $captureTarget = $Target
        if (!$captureTarget -and (Test-Path $LastTargetPath)) {
            $captureTarget = (Get-Content -Raw $LastTargetPath).Trim()
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
            "target=$captureTarget",
            "",
            "adb_events:",
            ($script:AdbEvents -join "`n"),
            "",
            "activity:",
            $focused
        ) | Out-File -Encoding utf8 $MetaPath
        Invoke-AdbExecOutToFile "screencap -p" $ScreenshotPath
        Write-Output "Log: $LogPath"
        if ($LogFilter) {
            Write-Output "Filtered log: $FilteredLogPath"
        }
        Write-Output "Meta: $MetaPath"
        Write-Output "Screenshot: $ScreenshotPath"
    }
}
