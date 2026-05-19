$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$scratchRoot = Join-Path $repoRoot "scratch\thor-debug"
$flagPath = Join-Path $scratchRoot "continue-until-blue-dragon-title.flag"
$proofPath = Join-Path $scratchRoot "latest-title-proof.json"
$statePath = Join-Path $scratchRoot "continue-until-blue-dragon-title-state.json"

function Write-HookJson {
    param([hashtable]$Value)
    $Value | ConvertTo-Json -Depth 8 -Compress
}

if (!(Test-Path -LiteralPath $flagPath)) {
    Write-HookJson @{}
    exit 0
}

if (Test-Path -LiteralPath $proofPath) {
    Write-HookJson @{}
    exit 0
}

if (!(Test-Path -LiteralPath $scratchRoot)) {
    New-Item -ItemType Directory -Path $scratchRoot | Out-Null
}

$attempts = 0
if (Test-Path -LiteralPath $statePath) {
    try {
        $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        if ($null -ne $state.attempts) {
            $attempts = [int]$state.attempts
        }
    } catch {
        $attempts = 0
    }
}

$attempts += 1
@{
    attempts = $attempts
    updated_at = (Get-Date).ToString("o")
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $statePath -Encoding UTF8

if ($attempts -gt 12) {
    Write-HookJson @{
        continue = $false
        reason = "Blue Dragon title auto-continue reached 12 Stop-hook attempts without a proof marker. Stop and summarize the blocker with the latest capture paths."
    }
    exit 0
}

Write-HookJson @{
    decision = "block"
    reason = "Continue the Blue Dragon title milestone. Build/deploy if needed, launch Blue Dragon from the Thor SD-card path with Android OSD hidden, capture screenshot/logcat, inspect for fatal/GPU/ARM64 fallback lines, update the dated worklog, and stop only after writing scratch/thor-debug/latest-title-proof.json or recording a concrete blocker."
}
