$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$scratchRoot = Join-Path $repoRoot "scratch\thor-debug"
$goalConfigPath = Join-Path $scratchRoot "codex-goal-loop.json"
$goalStatePath = Join-Path $scratchRoot "codex-goal-loop-state.json"
$flagPath = Join-Path $scratchRoot "continue-until-blue-dragon-title.flag"
$proofPath = Join-Path $scratchRoot "latest-title-proof.json"
$legacyStatePath = Join-Path $scratchRoot "continue-until-blue-dragon-title-state.json"

function Write-HookJson {
    param([hashtable]$Value)
    $Value | ConvertTo-Json -Depth 8 -Compress
}

function Resolve-GoalPath {
    param(
        [string]$Path,
        [string]$DefaultRelativePath
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        $Path = $DefaultRelativePath
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return (Join-Path $repoRoot $Path)
}

function Read-Attempts {
    param([string]$Path)

    if (!(Test-Path -LiteralPath $Path)) {
        return 0
    }
    try {
        $state = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
        if ($null -ne $state.attempts) {
            return [int]$state.attempts
        }
    } catch {
        return 0
    }
    return 0
}

function Write-Attempts {
    param(
        [string]$Path,
        [int]$Attempts,
        [string]$GoalName
    )

    @{
        attempts = $Attempts
        goal_name = $GoalName
        updated_at = (Get-Date).ToString("o")
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Path -Encoding UTF8
}

if (!(Test-Path -LiteralPath $scratchRoot)) {
    New-Item -ItemType Directory -Path $scratchRoot | Out-Null
}

if (Test-Path -LiteralPath $goalConfigPath) {
    try {
        $goalConfig = Get-Content -LiteralPath $goalConfigPath -Raw | ConvertFrom-Json
    } catch {
        Write-HookJson @{
            continue = $false
            reason = "Codex goal loop config is invalid JSON at scratch/thor-debug/codex-goal-loop.json. Stop and fix the config before continuing automatically."
        }
        exit 0
    }

    if ($goalConfig.enabled -eq $true) {
        $goalName = "xenia-thor-goal"
        if (![string]::IsNullOrWhiteSpace($goalConfig.goal_name)) {
            $goalName = [string]$goalConfig.goal_name
        }

        $goalProofPath = Resolve-GoalPath `
            -Path ([string]$goalConfig.proof_path) `
            -DefaultRelativePath "scratch/thor-debug/latest-goal-proof.json"
        if (Test-Path -LiteralPath $goalProofPath) {
            Write-HookJson @{}
            exit 0
        }

        $maxAttempts = 12
        if ($null -ne $goalConfig.max_attempts) {
            $maxAttempts = [Math]::Max(1, [int]$goalConfig.max_attempts)
        }

        $attempts = (Read-Attempts -Path $goalStatePath) + 1
        Write-Attempts -Path $goalStatePath -Attempts $attempts -GoalName $goalName

        if ($attempts -gt $maxAttempts) {
            Write-HookJson @{
                continue = $false
                reason = "Codex goal loop '$goalName' reached $maxAttempts Stop-hook attempts without proof marker '$goalProofPath'. Stop and summarize the latest blocker, capture paths, and next recommended experiment."
            }
            exit 0
        }

        $prompt = [string]$goalConfig.prompt
        if ([string]::IsNullOrWhiteSpace($prompt)) {
            $prompt = "Continue the configured xenia-thor goal. Check current repo state and latest Thor evidence, choose one narrow next action, update dated worklogs/research, and stop only after writing the configured proof marker or a concrete blocker."
        }

        Write-HookJson @{
            decision = "block"
            reason = $prompt
        }
        exit 0
    }
}

if (!(Test-Path -LiteralPath $flagPath)) {
    Write-HookJson @{}
    exit 0
}

if (Test-Path -LiteralPath $proofPath) {
    Write-HookJson @{}
    exit 0
}

$attempts = (Read-Attempts -Path $legacyStatePath) + 1
Write-Attempts -Path $legacyStatePath -Attempts $attempts -GoalName "blue-dragon-title-legacy"

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
