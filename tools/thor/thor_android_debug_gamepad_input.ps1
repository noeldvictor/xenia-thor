param(
    [string]$DeviceSerial = "",
    [string]$PackageName = "jp.xenia.emulator.github.debug",
    [ValidateSet(
        "A", "B", "X", "Y",
        "DpadUp", "DpadDown", "DpadLeft", "DpadRight",
        "Start", "Back", "LB", "RB", "LT", "RT", "LS", "RS", "Guide")]
    [string]$Button = "A",
    [int]$KeyCode = -1,
    [int]$HoldMs = 80,
    [switch]$Physical
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

$buttonKeyCodes = @{
    A = 96
    B = 97
    X = 99
    Y = 100
    DpadUp = 19
    DpadDown = 20
    DpadLeft = 21
    DpadRight = 22
    Start = 108
    Back = 109
    LB = 102
    RB = 103
    LT = 104
    RT = 105
    LS = 106
    RS = 107
    Guide = 110
}

if ($KeyCode -lt 0) {
    $KeyCode = [int]$buttonKeyCodes[$Button]
}

$safeHoldMs = [Math]::Max(1, [Math]::Min(2000, $HoldMs))
$action = "$PackageName.DEBUG_GAMEPAD_KEY"
$alreadyMapped = if ($Physical) { "false" } else { "true" }

$script:AdbPath = Resolve-AdbPath
$args = @(
    "shell", "am", "broadcast",
    "--receiver-foreground",
    "-a", $action,
    "-p", $PackageName,
    "--ei", "key_code", $KeyCode.ToString(),
    "--ei", "hold_ms", $safeHoldMs.ToString(),
    "--ez", "already_mapped", $alreadyMapped
)

Write-Output "adb=$script:AdbPath"
Write-Output "package=$PackageName"
Write-Output "action=$action"
Write-Output "button=$Button"
Write-Output "key_code=$KeyCode"
Write-Output "hold_ms=$safeHoldMs"
Write-Output "physical_mapping=$([bool]$Physical)"
Invoke-Adb $args
