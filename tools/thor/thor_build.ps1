param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [ValidateSet("FullApk", "ApkShell", "NativeCore", "NativeGradle", "Install", "FullDeploy", "ApkShellDeploy")]
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

# NativeCore runs ndk-build directly (2026-09-21). Gradle's externalNativeBuild
# task took 2 m 43 s for a build with NO change (its metadata generation dry-runs
# every module of the workspace); ndk-build itself answers "nothing to be done"
# in 1 s and links a one-file change in well under a minute. The objects live
# where Gradle put them (intermediates\cxx\<type>\<hash>\obj), so the two
# paths share one object tree; the result is copied to the ndkBuild\<variant>
# tree the ApkShell staging reads. NativeGradle is the old path.
function Get-CxxObjRoot {
    $parts = Get-VariantParts $Variant
    $typeDir = $parts.BuildType.Substring(0, 1).ToUpperInvariant() + $parts.BuildType.Substring(1)
    $cxxRoot = Join-Path $RepoRoot "android\android_studio_project\app\build\intermediates\cxx\$typeDir"
    if (!(Test-Path $cxxRoot)) {
        return $null
    }
    # One hashed directory per configuration; take the one that has objects.
    foreach ($dir in (Get-ChildItem -LiteralPath $cxxRoot -Directory | Sort-Object LastWriteTime -Descending)) {
        if (Test-Path (Join-Path $dir.FullName "obj\local\arm64-v8a")) {
            return $dir.FullName
        }
    }
    return $null
}

function Invoke-NdkBuildDirect {
    $objRoot = Get-CxxObjRoot
    if (!$objRoot) {
        Write-Host "no Gradle object tree yet; running the Gradle native build once"
        Invoke-Gradle @($nativeTask)
        return
    }
    # The NDK Gradle uses (ndkVersion in app/build.gradle): a newer NDK's clang
    # has new -Werror warnings the tree does not pass (ANDROID_NDK_HOME was 28).
    $gradleFile = Join-Path $RepoRoot "android\android_studio_project\app\build.gradle"
    $ndkVersion = (Select-String -Path $gradleFile -Pattern "ndkVersion\s+'([^']+)'").Matches[0].Groups[1].Value
    $sdkRoot = $env:ANDROID_SDK_ROOT
    if (!$sdkRoot) { $sdkRoot = $env:ANDROID_HOME }
    if (!$sdkRoot) { $sdkRoot = Join-Path $env:LOCALAPPDATA "Android\Sdk" }
    $ndkRoot = Join-Path $sdkRoot "ndk\$ndkVersion"
    $ndkBuild = Join-Path $ndkRoot "ndk-build.cmd"
    if (!(Test-Path $ndkBuild)) {
        throw "ndk-build not found at $ndkBuild (set ANDROID_NDK_HOME)"
    }
    $jobs = [Environment]::ProcessorCount
    $appDir = Join-Path $RepoRoot "android\android_studio_project\app"
    $wks = Join-Path $RepoRoot "build\xenia.wks.Android.mk"
    Push-Location $appDir
    try {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        # Start-Process with redirected streams: PowerShell 5.1 turns every
        # stderr line of a native command into an error record, and clang
        # notes are stderr.
        $logDir = Join-Path $RepoRoot "scratch\mcp"
        New-Item -ItemType Directory -Force -Path $logDir | Out-Null
        $outLog = Join-Path $logDir "ndk-build-last.log"
        $errLog = Join-Path $logDir "ndk-build-last.err"
        $ndkArgs = @(
            "NDK_PROJECT_PATH=null", "APP_BUILD_SCRIPT=$wks", "APP_ABI=arm64-v8a", "NDK_ALL_ABIS=arm64-v8a",
            "NDK_DEBUG=1", "APP_PLATFORM=android-26", "NDK_OUT=$objRoot/obj", "NDK_LIBS_OUT=$objRoot/lib",
            "NDK_APPLICATION_MK:=../../../build/xenia.Application.mk", "PREMAKE_ANDROIDNDK_PLATFORMS:=Android-ARM64",
            "-j$jobs", "--output-sync=none", "PREMAKE_ANDROIDNDK_CONFIGURATIONS:=Release",
            "xenia-app", "main_hook", "hook_impl")
        $proc = Start-Process -FilePath $ndkBuild -ArgumentList $ndkArgs -WorkingDirectory $appDir `
            -RedirectStandardOutput $outLog -RedirectStandardError $errLog -NoNewWindow -Wait -PassThru
        $code = $proc.ExitCode
        foreach ($file in @($outLog, $errLog)) {
            if (Test-Path $file) {
                Get-Content -LiteralPath $file | Where-Object {
                    $_ -notmatch "non-system libraries in linker flags|This is likely to result|or LOCAL_SHARED_LIBRARIES|current module|^Android NDK:\s*$"
                } | ForEach-Object { Write-Host $_ }
            }
        }
        if ($code -ne 0) {
            throw "ndk-build failed with exit code $code"
        }
        Write-Host ("ndk-build: {0:N0} s" -f $sw.Elapsed.TotalSeconds)
    } finally {
        Pop-Location
    }
    # Publish to the tree the ApkShell staging reads.
    $stageRoot = Join-Path (Get-ApkShellNativeSourceRoot) "arm64-v8a"
    New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null
    foreach ($name in @("libxenia-app.so", "libmain_hook.so", "libhook_impl.so")) {
        $built = Join-Path $objRoot "obj\local\arm64-v8a\$name"
        if (Test-Path $built) {
            # Windows Defender holds a freshly written .so open for scanning:
            # an overwrite fails ("used by another process") while a delete is
            # allowed (2026-09-22: five NativeCore builds failed on this copy
            # alone, each on the next file). Delete, then copy, with a retry.
            $dest = Join-Path $stageRoot $name
            $copied = $false
            for ($attempt = 1; $attempt -le 6 -and -not $copied; $attempt++) {
                try {
                    Remove-Item -LiteralPath $dest -Force -ErrorAction SilentlyContinue
                    Copy-Item -LiteralPath $built -Destination $dest -Force -ErrorAction Stop
                    $copied = $true
                } catch {
                    if ($attempt -eq 6) { throw }
                    Write-Host ("copy of {0} blocked (attempt {1}); waiting 3 s" -f $name, $attempt)
                    Start-Sleep -Seconds 3
                }
            }
        }
    }
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
    # AGP merges the existing cxx output into the APK even when the native
    # build task is skipped. A copy staged into jniLibs then collides with it
    # ("2 files found with path lib/arm64-v8a/libxenia-app.so"). So skip the
    # native task first with nothing staged, and stage only when the APK comes
    # out without the library (no cxx output on this machine).
    Invoke-Gradle @($assembleTask, "-x", $nativeTask)
    if (Test-ApkContainsNativeLibrary) {
        return
    }
    Write-Warning "APK has no native library from the cxx output; staging from $(Get-ApkShellNativeSourceRoot)."
    $stagedFiles = Stage-ExistingNativeLibsForApkShell
    try {
        Invoke-Gradle @($assembleTask, "-x", $nativeTask)
        Assert-ApkContainsNativeLibrary
    } finally {
        Remove-StagedNativeLibs $stagedFiles
    }
}

function Test-ApkContainsNativeLibrary {
    $apkPath = Get-ApkPath
    if (!(Test-Path -LiteralPath $apkPath)) {
        return $false
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($apkPath)
    try {
        $requiredEntry = "lib/arm64-v8a/libxenia-app.so"
        foreach ($entry in $zip.Entries) {
            if ($entry.FullName -eq $requiredEntry) {
                return $true
            }
        }
        return $false
    } finally {
        $zip.Dispose()
    }
}
function Assert-ApkContainsNativeLibrary {
    $apkPath = Get-ApkPath
    if (!(Test-Path -LiteralPath $apkPath)) {
        throw "APK not found: $apkPath"
    }
    if (!(Test-ApkContainsNativeLibrary)) {
        throw "APK is missing lib/arm64-v8a/libxenia-app.so; refusing to install a launcher that cannot start native Xenia."
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

# Before every mode: the generated headers (build/version.h and the LLVM
# lowering stamp that keys the object cache) and a syntax check of the MCP
# server, which sat broken for a whole session once (2026-09-20).
& python (Join-Path $RepoRoot "tools\build\gen_version_h.py")
& python -m py_compile (Join-Path $RepoRoot "tools\mcp\xenia_thor_mcp.py")
if ($LASTEXITCODE -ne 0) {
    throw "tools/mcp/xenia_thor_mcp.py does not compile; fix it before building"
}
# The NDK makefiles (build/*.prj.Android.mk) list every source file and are
# generated. A new .cc is invisible to the build until premake runs again
# (debug_api_android.cc linked to nothing on 2026-09-20). Premake takes 2 s,
# so it runs before every native build.
if ($Mode -in @("NativeCore", "FullApk", "FullDeploy")) {
    # NativeCore is the dev loop: ThinLTO with a cache (10 to 20 s rebuilds).
    # FullApk and FullDeploy are the builds that ship: full LTO (2026-09-22,
    # the Blue Dragon gameplay regression sat in the ThinLTO switch window).
    $premakeArgs = @("--file=premake5.lua", "--os=android")
    if ($Mode -eq "NativeCore") { $premakeArgs += "--thinlto" }
    $premakeArgs += "androidndk"
    & (Join-Path $RepoRoot "tools\build\bin\premake5.exe") @premakeArgs | Select-Object -Last 1
    if ($LASTEXITCODE -ne 0) {
        throw "premake5 androidndk failed"
    }
}

switch ($Mode) {
    "FullApk" {
        Invoke-Gradle @($assembleTask)
    }
    "ApkShell" {
        Invoke-ApkShellGradle
    }
    "NativeCore" {
        Invoke-NdkBuildDirect
    }
    "NativeGradle" {
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
