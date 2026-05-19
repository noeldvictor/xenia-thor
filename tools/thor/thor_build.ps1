param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [ValidateSet("FullApk", "ApkShell", "NativeCore", "Install", "FullDeploy", "ApkShellDeploy")]
    [string]$Mode = "FullApk",
    [string]$Variant = "GithubDebug",
    [string]$DeviceSerial = "",
    [switch]$NoSubst
)

$ErrorActionPreference = "Stop"

function Invoke-Adb {
    param([string[]]$Arguments)
    if ($DeviceSerial) {
        & adb -s $DeviceSerial @Arguments
    } else {
        & adb @Arguments
    }
    if ($LASTEXITCODE -ne 0) {
        throw "adb failed with exit code $($LASTEXITCODE): $($Arguments -join ' ')"
    }
}

function Get-VariantParts {
    param([string]$Name)
    if ($Name -notmatch "^(?<flavor>.+?)(?<buildType>Debug|Release|Checked)$") {
        throw "Variant must look like GithubDebug, GithubRelease, GooglePlayDebug, or GooglePlayChecked."
    }
    return @{
        Flavor = $Matches.flavor.Substring(0, 1).ToLowerInvariant() + $Matches.flavor.Substring(1)
        BuildType = $Matches.buildType.ToLowerInvariant()
    }
}

function Get-ApkPath {
    $parts = Get-VariantParts $Variant
    return Join-Path $RepoRoot "android\android_studio_project\app\build\outputs\apk\$($parts.Flavor)\$($parts.BuildType)\app-$($parts.Flavor)-$($parts.BuildType).apk"
}

function Get-FreeSubstDrive {
    foreach ($letter in @("X", "Y", "Z", "W", "V", "U")) {
        if (!(Get-PSDrive -Name $letter -ErrorAction SilentlyContinue)) {
            return $letter
        }
    }
    throw "No free subst drive letter found."
}

function Invoke-WithRepoPath {
    param([scriptblock]$Body)
    $resolvedRepo = (Resolve-Path $RepoRoot).Path
    $needsSubst = !$NoSubst -and $resolvedRepo.Contains(" ")
    if (!$needsSubst) {
        & $Body $resolvedRepo
        return
    }

    $letter = Get-FreeSubstDrive
    $drive = "$letter`:"
    subst $drive $resolvedRepo
    try {
        & $Body $drive
    } finally {
        subst $drive /D
    }
}

function Invoke-Gradle {
    param([string[]]$GradleArgs)
    Invoke-WithRepoPath {
        param([string]$MappedRepo)
        Push-Location (Join-Path $MappedRepo "android\android_studio_project")
        try {
            & .\gradlew.bat @GradleArgs
            if ($LASTEXITCODE -ne 0) {
                throw "Gradle failed with exit code $($LASTEXITCODE): $($GradleArgs -join ' ')"
            }
        } finally {
            Pop-Location
        }
    }
}

function Install-Apk {
    $apkPath = Get-ApkPath
    if (!(Test-Path $apkPath)) {
        throw "APK not found: $apkPath"
    }
    Invoke-Adb @("install", "-r", "-d", $apkPath)
}

$assembleTask = ":app:assemble$Variant"
$nativeTask = ":app:externalNativeBuild$Variant"

switch ($Mode) {
    "FullApk" {
        Invoke-Gradle @($assembleTask)
    }
    "ApkShell" {
        Invoke-Gradle @($assembleTask, "-x", $nativeTask)
    }
    "NativeCore" {
        Invoke-Gradle @($nativeTask)
    }
    "Install" {
        Install-Apk
    }
    "FullDeploy" {
        Invoke-Gradle @($assembleTask)
        Install-Apk
    }
    "ApkShellDeploy" {
        Invoke-Gradle @($assembleTask, "-x", $nativeTask)
        Install-Apk
    }
}
