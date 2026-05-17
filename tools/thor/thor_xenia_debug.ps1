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
    [string[]]$NoisePackages = @("net.rpcsx.easy"),
    [string]$LogFilter = "xenia|Vulkan|Adreno|AndroidRuntime|FATAL|crash|tombstone|signal|backtrace"
)

$ErrorActionPreference = "Stop"

function Invoke-Adb {
    param([string[]]$Arguments)
    if ($DeviceSerial) {
        & adb -s $DeviceSerial @Arguments
    } else {
        & adb @Arguments
    }
}

function ConvertTo-AdbShellSingleQuote {
    param([string]$Value)
    return "'" + ($Value -replace "'", "'\\''") + "'"
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
        "--ez discord false")
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
        $packagePid = (Invoke-Adb @("shell", "pidof", $PackageName)) -join " "
        $focused = (Invoke-AdbShellCommand "dumpsys activity activities | grep -E 'mFocusedApp|mResumedActivity|$PackageName' | head -40") -join "`n"
        @(
            "timestamp=$Stamp",
            "branch=$branch",
            "head=$head",
            "package=$PackageName",
            "pid=$packagePid",
            "apk=$ApkPath",
            "apk_sha256=$apkHash",
            "target=$captureTarget",
            "",
            "activity:",
            $focused
        ) | Out-File -Encoding utf8 $MetaPath
        cmd /c "adb $(if ($DeviceSerial) { "-s $DeviceSerial " })exec-out screencap -p > `"$ScreenshotPath`""
        Write-Output "Log: $LogPath"
        if ($LogFilter) {
            Write-Output "Filtered log: $FilteredLogPath"
        }
        Write-Output "Meta: $MetaPath"
        Write-Output "Screenshot: $ScreenshotPath"
    }
}
