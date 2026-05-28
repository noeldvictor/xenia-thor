param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [ValidateSet("FullApk", "ApkShell", "NativeCore", "Install", "FullDeploy", "ApkShellDeploy")]
    [string]$Mode = "FullApk",
    [string]$Variant = "GithubDebug",
    [string]$DeviceSerial = "",
    [string]$PackageName = "jp.xenia.emulator.github.debug",
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

function Get-VariantDirectoryName {
    $parts = Get-VariantParts $Variant
    return "$($parts.Flavor)$($parts.BuildType.Substring(0, 1).ToUpperInvariant())$($parts.BuildType.Substring(1))"
}

function Get-ApkShellNativeSourceRoot {
    $variantDir = Get-VariantDirectoryName
    return Join-Path $RepoRoot "android\android_studio_project\app\build\intermediates\ndkBuild\$variantDir\obj\local"
}

function Get-JniLibsRoot {
    return Join-Path $RepoRoot "android\android_studio_project\app\src\main\jniLibs"
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

function Stage-ExistingNativeLibsForApkShell {
    $sourceRoot = Get-ApkShellNativeSourceRoot
    if (!(Test-Path -LiteralPath $sourceRoot)) {
        Write-Warning "No existing native output found at $sourceRoot; APK shell build may not be installable."
        return @()
    }

    $jniLibsRoot = Get-JniLibsRoot
    $stagedFiles = @()
    foreach ($source in (Get-ChildItem -LiteralPath $sourceRoot -Recurse -Filter "libxenia-app.so" -ErrorAction SilentlyContinue)) {
        $abi = Split-Path -Leaf (Split-Path -Parent $source.FullName)
        if (!$abi) {
            continue
        }
        $destinationDirectory = Join-Path $jniLibsRoot $abi
        $destination = Join-Path $destinationDirectory "libxenia-app.so"
        if (Test-Path -LiteralPath $destination) {
            continue
        }
        New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
        Copy-Item -LiteralPath $source.FullName -Destination $destination -Force
        $stagedFiles += $destination
    }

    if ($stagedFiles.Count -eq 0) {
        Write-Warning "No native libraries were staged for APK shell build."
    } else {
        Write-Output "Staged $($stagedFiles.Count) native library file(s) for APK shell build."
    }
    return $stagedFiles
}

function Remove-StagedNativeLibs {
    param([string[]]$StagedFiles)
    if (!$StagedFiles -or $StagedFiles.Count -eq 0) {
        return
    }

    $jniLibsRoot = Get-JniLibsRoot
    foreach ($file in $StagedFiles) {
        if (Test-Path -LiteralPath $file) {
            Remove-Item -LiteralPath $file -Force
        }
        $abiDirectory = Split-Path -Parent $file
        if ($abiDirectory -and
            (Test-Path -LiteralPath $abiDirectory) -and
            -not (Get-ChildItem -LiteralPath $abiDirectory -Force)) {
            Remove-Item -LiteralPath $abiDirectory -Force
        }
    }
    if ((Test-Path -LiteralPath $jniLibsRoot) -and
        -not (Get-ChildItem -LiteralPath $jniLibsRoot -Force)) {
        Remove-Item -LiteralPath $jniLibsRoot -Force
    }
}

function Invoke-ApkShellGradle {
    $stagedFiles = Stage-ExistingNativeLibsForApkShell
    try {
        Invoke-Gradle @($assembleTask, "-x", $nativeTask)
        Assert-ApkContainsNativeLibrary
    } finally {
        Remove-StagedNativeLibs $stagedFiles
    }
}

function Assert-ApkContainsNativeLibrary {
    $apkPath = Get-ApkPath
    if (!(Test-Path -LiteralPath $apkPath)) {
        throw "APK not found: $apkPath"
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($apkPath)
    try {
        $requiredEntry = "lib/arm64-v8a/libxenia-app.so"
        $hasRequiredEntry = $false
        foreach ($entry in $zip.Entries) {
            if ($entry.FullName -eq $requiredEntry) {
                $hasRequiredEntry = $true
                break
            }
        }
        if (!$hasRequiredEntry) {
            throw "APK is missing $requiredEntry; refusing to install a launcher that cannot start native Xenia."
        }
    } finally {
        $zip.Dispose()
    }
}

function Install-Apk {
    $apkPath = Get-ApkPath
    if (!(Test-Path $apkPath)) {
        throw "APK not found: $apkPath"
    }
    Assert-ApkContainsNativeLibrary
    Invoke-Adb @("install", "-r", "-d", $apkPath)
}

function Grant-DebugStorageAccess {
    if (!$DeviceSerial) {
        return
    }
    Invoke-Adb @("shell", "appops", "set", $PackageName, "MANAGE_EXTERNAL_STORAGE", "allow")
}

$assembleTask = ":app:assemble$Variant"
$nativeTask = ":app:externalNativeBuild$Variant"

switch ($Mode) {
    "FullApk" {
        Invoke-Gradle @($assembleTask)
    }
    "ApkShell" {
        Invoke-ApkShellGradle
    }
    "NativeCore" {
        Invoke-Gradle @($nativeTask)
    }
    "Install" {
        Install-Apk
        Grant-DebugStorageAccess
    }
    "FullDeploy" {
        Invoke-Gradle @($assembleTask)
        Install-Apk
        Grant-DebugStorageAccess
    }
    "ApkShellDeploy" {
        Invoke-ApkShellGradle
        Install-Apk
        Grant-DebugStorageAccess
    }
}
