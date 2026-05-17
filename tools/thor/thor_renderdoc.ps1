param(
    [string]$DeviceSerial = "",
    [string]$PackageName = "jp.xenia.emulator.github.debug",
    [string]$LayerName = "VK_LAYER_RENDERDOC_Capture",
    [string]$LayerPackage = "org.renderdoc.renderdoccmd.arm64",
    [ValidateSet("Status", "EnablePerApp", "EnableGlobal", "Disable", "PullCaptures")]
    [string]$Mode = "Status",
    [string]$CaptureDeviceDir = "",
    [string]$OutDir = ""
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

function Invoke-AdbShellCommand {
    param([string]$Command)
    Invoke-Adb @("shell", $Command)
}

function Test-InstalledPackage {
    param([string]$Name)
    if (!$Name) {
        return $false
    }
    $result = Invoke-Adb @("shell", "pm", "list", "packages", $Name)
    return ($result | Select-String -SimpleMatch "package:$Name") -ne $null
}

if (!$CaptureDeviceDir) {
    $CaptureDeviceDir = "/sdcard/Android/data/$PackageName/files/RenderDoc"
}

if (!$OutDir) {
    $OutDir = Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path "scratch\renderdoc"
}

switch ($Mode) {
    "Status" {
        Write-Output "GPU debug settings:"
        Invoke-AdbShellCommand "settings list global | grep gpu"
        Write-Output ""
        Write-Output "RenderDoc-like packages:"
        Invoke-AdbShellCommand "pm list packages | grep -i renderdoc"
        Write-Output ""
        Write-Output "Current Vulkan global property:"
        Invoke-AdbShellCommand "getprop debug.vulkan.layers"
    }
    "EnablePerApp" {
        Invoke-Adb @("shell", "settings", "put", "global", "enable_gpu_debug_layers", "1")
        Invoke-Adb @("shell", "settings", "put", "global", "gpu_debug_app", $PackageName)
        Invoke-Adb @("shell", "settings", "put", "global", "gpu_debug_layers", $LayerName)
        if ($LayerPackage) {
            if (Test-InstalledPackage $LayerPackage) {
                Invoke-Adb @("shell", "settings", "put", "global", "gpu_debug_layer_app", $LayerPackage)
            } else {
                Write-Warning "Layer package '$LayerPackage' is not installed; leaving gpu_debug_layer_app unchanged. Pass the installed RenderDoc layer package name if it differs."
            }
        }
        Invoke-AdbShellCommand "settings list global | grep gpu"
    }
    "EnableGlobal" {
        Invoke-Adb @("shell", "setprop", "debug.vulkan.layers", $LayerName)
        Invoke-AdbShellCommand "getprop debug.vulkan.layers"
    }
    "Disable" {
        foreach ($key in @(
            "enable_gpu_debug_layers",
            "gpu_debug_app",
            "gpu_debug_layers",
            "gpu_debug_layer_app")) {
            Invoke-Adb @("shell", "settings", "delete", "global", $key) | Out-Null
        }
        Invoke-Adb @("shell", "setprop", "debug.vulkan.layers", "") | Out-Null
        Invoke-AdbShellCommand "settings list global | grep gpu"
    }
    "PullCaptures" {
        New-Item -ItemType Directory -Force $OutDir | Out-Null
        Invoke-Adb @("pull", "$CaptureDeviceDir/.", $OutDir)
    }
}
