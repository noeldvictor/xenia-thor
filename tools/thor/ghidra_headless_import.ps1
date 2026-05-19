param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$GhidraHome = "",
    [string]$AnalyzeHeadless = "",
    [string]$ProjectDir = "",
    [string]$ProjectName = "xenia-thor",
    [string]$ImportPath = "",
    [string]$Processor = "",
    [switch]$NoAnalysis
)

$ErrorActionPreference = "Stop"

if (!$ProjectDir) {
    $ProjectDir = Join-Path $RepoRoot "scratch\ghidra"
}
New-Item -ItemType Directory -Force $ProjectDir | Out-Null

if (!$AnalyzeHeadless) {
    if ($GhidraHome) {
        $AnalyzeHeadless = Join-Path $GhidraHome "support\analyzeHeadless.bat"
    } elseif ($env:GHIDRA_HOME) {
        $AnalyzeHeadless = Join-Path $env:GHIDRA_HOME "support\analyzeHeadless.bat"
    } else {
        $knownGhidraHomes = @(
            "C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC"
        )
        foreach ($knownGhidraHome in $knownGhidraHomes) {
            $candidate = Join-Path $knownGhidraHome "support\analyzeHeadless.bat"
            if (Test-Path $candidate) {
                $GhidraHome = $knownGhidraHome
                $AnalyzeHeadless = $candidate
                break
            }
        }
    }
}

if (!$AnalyzeHeadless -or !(Test-Path $AnalyzeHeadless)) {
    throw "Ghidra analyzeHeadless.bat not found. Pass -GhidraHome, -AnalyzeHeadless, or set GHIDRA_HOME."
}

if (!$ImportPath) {
    $ImportPath = Join-Path $RepoRoot "android\android_studio_project\app\build\intermediates\ndkBuild\githubDebug\obj\local\arm64-v8a\libxenia-app.so"
}

if (!(Test-Path $ImportPath)) {
    throw "Import path not found: $ImportPath"
}

Write-Output "Using analyzeHeadless: $AnalyzeHeadless"
if ($GhidraHome) {
    Write-Output "Using Ghidra home: $GhidraHome"
}
Write-Output "Project dir: $ProjectDir"
Write-Output "Project name: $ProjectName"
Write-Output "Import path: $ImportPath"

$arguments = @(
    $ProjectDir,
    $ProjectName,
    "-import", $ImportPath,
    "-overwrite")

if ($Processor) {
    $arguments += @("-processor", $Processor)
}

if ($NoAnalysis) {
    $arguments += "-noanalysis"
}

& $AnalyzeHeadless @arguments
