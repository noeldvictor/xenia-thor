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
