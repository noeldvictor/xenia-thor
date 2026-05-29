param(
    [ValidateSet("Status", "Screenshot", "Screenrecord", "UiDump", "CrashBundle", "Mirror")]
    [string]$Mode = "Status",
    [string]$DeviceSerial = "",
    [string]$PackageName = "jp.xenia.emulator.github.debug",
    [string]$OutDir = "",
    [int]$Seconds = 30,
    [int]$LogcatTailLines = 50000,
    [string]$ScrcpyPath = "scrcpy",
    [switch]$FullLogcat,
    [switch]$ClearLogcat
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
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $script:AdbPath @allArgs 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($exitCode -ne 0) {
        throw "adb failed with exit code $exitCode`: $($Arguments -join ' ')"
    }
    $output
}

function Invoke-AdbNoSerial {
    param([string[]]$Arguments)

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $script:AdbPath @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($exitCode -ne 0) {
        throw "adb failed with exit code $exitCode`: $($Arguments -join ' ')"
    }
    $output
}

function Invoke-AdbToFile {
    param(
        [string[]]$Arguments,
        [string]$OutputPath
    )

    $allArgs = @()
    if ($DeviceSerial) {
        $allArgs += @("-s", $DeviceSerial)
    }
    $allArgs += $Arguments
    $errorPath = "$OutputPath.err.txt"
    if (Test-Path -LiteralPath $OutputPath) {
        Remove-Item -LiteralPath $OutputPath -Force
    }
    if (Test-Path -LiteralPath $errorPath) {
        Remove-Item -LiteralPath $errorPath -Force
    }

    $process = Start-Process -FilePath $script:AdbPath -ArgumentList $allArgs `
        -RedirectStandardOutput $OutputPath -RedirectStandardError $errorPath `
        -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        $errorText = ""
        if (Test-Path -LiteralPath $errorPath) {
            $errorText = (Get-Content -LiteralPath $errorPath -Raw)
        }
        throw "adb failed with exit code $($process.ExitCode): $($Arguments -join ' ') $errorText"
    }
    if ((Test-Path -LiteralPath $errorPath) -and
        ((Get-Item -LiteralPath $errorPath).Length -eq 0)) {
        Remove-Item -LiteralPath $errorPath -Force
    }
}

function New-OutputDirectory {
    if ($script:OutDir) {
        New-Item -ItemType Directory -Force -Path $script:OutDir | Out-Null
        return (Resolve-Path -LiteralPath $script:OutDir).Path
    }

    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $path = Join-Path $repoRoot "scratch\thor-debug\remote-debug-$stamp"
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    return $path
}

function Write-Metadata {
    param([string]$Directory)

    $metaPath = Join-Path $Directory "meta.txt"
    $branch = (& git branch --show-current 2>$null)
    $commit = (& git rev-parse --short HEAD 2>$null)
    $dirty = (& git status --short 2>$null)
    $deviceModel = ((Invoke-Adb @("shell", "getprop", "ro.product.model")) -join " ").Trim()
    $sdk = ((Invoke-Adb @("shell", "getprop", "ro.build.version.sdk")) -join " ").Trim()
    $activity = ((Invoke-Adb @("shell", "dumpsys", "activity", "activities")) |
        Select-String -Pattern "mResumedActivity|topResumedActivity|ResumedActivity" |
        Select-Object -First 8)

    $lines = @(
        "created_at=$(Get-Date -Format o)",
        "mode=$Mode",
        "clear_logcat=$ClearLogcat",
        "device_serial=$DeviceSerial",
        "package=$PackageName",
        "device_model=$deviceModel",
        "android_sdk=$sdk",
        "full_logcat=$FullLogcat",
        "logcat_tail_lines=$LogcatTailLines",
        "branch=$branch",
        "commit=$commit",
        "dirty_state=$([bool]$dirty)",
        "active_activity=$($activity -join ' | ')"
    )
    if ($dirty) {
        $lines += "dirty_files:"
        $lines += $dirty
    }
    $lines | Out-File -Encoding utf8 -FilePath $metaPath
    return $metaPath
}

function Save-Logcat {
    param([string]$Directory)

    $logPath = Join-Path $Directory "logcat.txt"
    $focusedPath = Join-Path $Directory "logcat-focused.txt"
    $statusPath = Join-Path $Directory "status-report.txt"

    $logcatArgs = @("logcat", "-d", "-v", "threadtime")
    if (!$FullLogcat -and $LogcatTailLines -gt 0) {
        $logcatArgs += @("-t", $LogcatTailLines.ToString())
    }
    Invoke-AdbToFile -Arguments $logcatArgs -OutputPath $logPath

    Select-String -Path $logPath -Pattern "xenia|Xenia|AndroidRuntime|FATAL|fatal|tombstone|Fatal signal|signal [0-9]+|SIGABRT|SIGSEGV|backtrace|crash|RtlRaiseException|assert|Vulkan|Adreno|XeniaInput|DEBUG_GAMEPAD|VdSwap" |
        ForEach-Object { $_.Line } |
        Out-File -Encoding utf8 -FilePath $focusedPath
    $reportScript = Join-Path $PSScriptRoot "thor_android_game_status_report.ps1"
    if (Test-Path -LiteralPath $reportScript) {
        & $reportScript -LogPath $logPath -OutPath $statusPath | Out-Null
        return @($logPath, $focusedPath, $statusPath)
    }
    return @($logPath, $focusedPath)
}

function Save-Screenshot {
    param([string]$Directory)

    $screenPath = Join-Path $Directory "screen.png"
    $serialPart = ""
    if ($DeviceSerial) {
        $serialPart = "-s `"$DeviceSerial`" "
    }
    $cmdLine = "`"$script:AdbPath`" ${serialPart}exec-out screencap -p > `"$screenPath`""
    cmd /c $cmdLine
    if ($LASTEXITCODE -ne 0) {
        throw "screencap failed with exit code $LASTEXITCODE"
    }
    return $screenPath
}

function Save-UiDump {
    param([string]$Directory)

    $devicePath = "/sdcard/xenia-thor-ui.xml"
    $localPath = Join-Path $Directory "ui.xml"
    Invoke-Adb @("shell", "uiautomator", "dump", $devicePath) | Out-Null
    Invoke-Adb @("pull", $devicePath, $localPath) | Out-Null
    Invoke-Adb @("shell", "rm", "-f", $devicePath) | Out-Null
    return $localPath
}

function Save-Screenrecord {
    param([string]$Directory)

    $safeSeconds = [Math]::Max(1, [Math]::Min($Seconds, 180))
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $devicePath = "/sdcard/xenia-thor-screen-$stamp.mp4"
    $localPath = Join-Path $Directory "screen.mp4"
    Invoke-Adb @("shell", "screenrecord", "--time-limit", $safeSeconds.ToString(), $devicePath) | Out-Null
    Invoke-Adb @("pull", $devicePath, $localPath) | Out-Null
    Invoke-Adb @("shell", "rm", "-f", $devicePath) | Out-Null
    return $localPath
}

function Save-Bugreport {
    param([string]$Directory)

    $bugreportLog = Join-Path $Directory "bugreport-command.txt"
    $output = Invoke-Adb @("bugreport", $Directory)
    $output | Out-File -Encoding utf8 -FilePath $bugreportLog
    return $bugreportLog
}

$script:AdbPath = Resolve-AdbPath

if ($Mode -eq "Status") {
    Write-Output "adb=$script:AdbPath"
    Write-Output "devices:"
    Invoke-AdbNoSerial @("devices", "-l")
    if ($DeviceSerial) {
        Write-Output ""
        Write-Output "device:"
        Invoke-Adb @("shell", "getprop", "ro.product.model")
        Invoke-Adb @("shell", "getprop", "ro.build.version.release")
        Invoke-Adb @("shell", "getprop", "ro.build.version.sdk")
        Write-Output ""
        Write-Output "package pid:"
        Invoke-Adb @("shell", "pidof", $PackageName)
        Write-Output ""
        Write-Output "resumed activity:"
        Invoke-Adb @("shell", "dumpsys", "activity", "activities") |
            Select-String -Pattern "mResumedActivity|topResumedActivity|ResumedActivity" |
            Select-Object -First 8
    }
    Write-Output ""
    $scrcpy = Get-Command $ScrcpyPath -ErrorAction SilentlyContinue
    if ($scrcpy) {
        Write-Output "scrcpy=$($scrcpy.Source)"
    } else {
        Write-Output "scrcpy=not_found"
    }
    exit 0
}

if ($Mode -eq "Mirror") {
    $scrcpy = Get-Command $ScrcpyPath -ErrorAction SilentlyContinue
    if (!$scrcpy) {
        throw "scrcpy not found. Install scrcpy or pass -ScrcpyPath."
    }
    $args = @("--stay-awake", "--show-touches", "--max-size=1280")
    if ($DeviceSerial) {
        $args = @("--serial", $DeviceSerial) + $args
    }
    Start-Process -FilePath $scrcpy.Source -ArgumentList $args
    Write-Output "Started scrcpy mirror for $DeviceSerial"
    exit 0
}

$directory = New-OutputDirectory
if ($ClearLogcat) {
    Invoke-Adb @("logcat", "-c") | Out-Null
}

$metaPath = Write-Metadata $directory
$saved = @($metaPath)

switch ($Mode) {
    "Screenshot" {
        $saved += Save-Screenshot $directory
        $saved += Save-Logcat $directory
    }
    "Screenrecord" {
        $saved += Save-Screenrecord $directory
        $saved += Save-Screenshot $directory
        $saved += Save-Logcat $directory
    }
    "UiDump" {
        $saved += Save-Screenshot $directory
        $saved += Save-UiDump $directory
        $saved += Save-Logcat $directory
    }
    "CrashBundle" {
        $saved += Save-Screenshot $directory
        $saved += Save-UiDump $directory
        $saved += Save-Logcat $directory
        $saved += Save-Bugreport $directory
    }
}

Write-Output "Remote debug packet: $directory"
foreach ($path in $saved) {
    Write-Output "  $path"
}
