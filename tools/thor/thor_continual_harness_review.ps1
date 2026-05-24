[CmdletBinding()]
param(
    [ValidateSet("Window")]
    [string]$Mode = "Window",

    [int]$RecentWorklogCount = 2,

    [int]$RecentResearchCount = 8,

    [int]$RecentCaptureCount = 5,

    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$scratchRoot = Join-Path $repoRoot "scratch\thor-debug"
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $scratchRoot "continual-harness-window.md"
}
if (![System.IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath = Join-Path $repoRoot $OutputPath
}

function Ensure-ParentDirectory {
    param([string]$Path)
    $parent = Split-Path -Parent $Path
    if (!(Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
}

function Invoke-GitText {
    param([string[]]$GitArgs)
    try {
        return (& git @GitArgs 2>$null) -join "`n"
    } catch {
        return ""
    }
}

function Append-FileExcerpt {
    param(
        [System.Text.StringBuilder]$Builder,
        [string]$Path,
        [int]$Tail = 120,
        [int]$Head = 0
    )

    if (!(Test-Path -LiteralPath $Path)) {
        return
    }
    [void]$Builder.AppendLine("### $((Resolve-Path -LiteralPath $Path).Path.Substring($repoRoot.Length + 1))")
    [void]$Builder.AppendLine()
    [void]$Builder.AppendLine('```text')
    if ($Head -gt 0) {
        Get-Content -LiteralPath $Path -TotalCount $Head | ForEach-Object {
            [void]$Builder.AppendLine($_)
        }
    } else {
        Get-Content -LiteralPath $Path -Tail $Tail | ForEach-Object {
            [void]$Builder.AppendLine($_)
        }
    }
    [void]$Builder.AppendLine('```')
    [void]$Builder.AppendLine()
}

function Append-KeyValueFile {
    param(
        [System.Text.StringBuilder]$Builder,
        [string]$Path,
        [string[]]$Keys
    )

    if (!(Test-Path -LiteralPath $Path)) {
        return
    }
    [void]$Builder.AppendLine("### $((Resolve-Path -LiteralPath $Path).Path.Substring($repoRoot.Length + 1))")
    [void]$Builder.AppendLine()
    [void]$Builder.AppendLine('```text')
    $patterns = @()
    foreach ($key in $Keys) {
        $patterns += "^$([regex]::Escape($key))="
    }
    $regex = [string]::Join("|", $patterns)
    Get-Content -LiteralPath $Path | Where-Object { $_ -match $regex } | ForEach-Object {
        [void]$Builder.AppendLine($_)
    }
    [void]$Builder.AppendLine('```')
    [void]$Builder.AppendLine()
}

if ($Mode -eq "Window") {
    Ensure-ParentDirectory -Path $OutputPath
    $builder = [System.Text.StringBuilder]::new()
    $now = Get-Date

    [void]$builder.AppendLine("# xenia-thor Continual Harness Window")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("Generated: $($now.ToString('o'))")
    [void]$builder.AppendLine("Repo: $repoRoot")
    [void]$builder.AppendLine()

    [void]$builder.AppendLine("## Refiner Checklist")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("- Identify repeated failures, stale next actions, missing tools, and stale skills.")
    [void]$builder.AppendLine("- Recommend conservative CRUD over AGENTS.md, repo-local skills, goal-loop prompts, research memory, or tooling.")
    [void]$builder.AppendLine("- Keep codegen experiments one-variable and default-off until route proof exists.")
    [void]$builder.AppendLine("- Do not commit this scratch window; commit only durable docs/tooling/code changes.")
    [void]$builder.AppendLine()

    [void]$builder.AppendLine("## Git And Goal State")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('```text')
    [void]$builder.AppendLine("branch_status:")
    [void]$builder.AppendLine((Invoke-GitText -GitArgs @("status", "--short", "--branch")))
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("head:")
    [void]$builder.AppendLine((Invoke-GitText -GitArgs @("rev-parse", "--short=9", "HEAD")))
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("proof_exists=$((Test-Path -LiteralPath (Join-Path $repoRoot 'scratch\thor-debug\latest-blue-dragon-full-speed-proof.json')))")
    [void]$builder.AppendLine('```')
    [void]$builder.AppendLine()

    Append-FileExcerpt -Builder $builder -Path (Join-Path $repoRoot "scratch\thor-debug\codex-goal-loop.json") -Head 120

    [void]$builder.AppendLine("## Recent Worklogs")
    [void]$builder.AppendLine()
    Get-ChildItem -LiteralPath (Join-Path $repoRoot "docs\worklogs") -Filter "*.md" |
        Sort-Object Name -Descending |
        Select-Object -First $RecentWorklogCount |
        ForEach-Object { Append-FileExcerpt -Builder $builder -Path $_.FullName -Tail 160 }

    [void]$builder.AppendLine("## Recent Research Notes")
    [void]$builder.AppendLine()
    Get-ChildItem -LiteralPath (Join-Path $repoRoot "docs\research") -Filter "*.md" |
        Sort-Object Name -Descending |
        Select-Object -First $RecentResearchCount |
        ForEach-Object { Append-FileExcerpt -Builder $builder -Path $_.FullName -Head 80 }

    [void]$builder.AppendLine("## Recent Capture Metadata")
    [void]$builder.AppendLine()
    $metaKeys = @(
        "timestamp",
        "branch",
        "head",
        "apk_sha256",
        "target",
        "live_capture_seconds",
        "disassemble_functions",
        "disassemble_function_filter",
        "arm64_speed_profile_body_time_filter",
        "arm64_speed_profile_entry_exit_time_filter",
        "arm64_speed_profile_body_time_after_ms",
        "arm64_speed_profile_block_filter",
        "arm64_speed_profile_block_body_time",
        "arm64_speed_profile_call_edge_filter",
        "arm64_context_promotion_gpr_livein_r1",
        "stop_app_after_capture",
        "log_level"
    )
    Get-ChildItem -LiteralPath $scratchRoot -Filter "*-meta.txt" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First $RecentCaptureCount |
        ForEach-Object { Append-KeyValueFile -Builder $builder -Path $_.FullName -Keys $metaKeys }

    $text = $builder.ToString()
    Set-Content -LiteralPath $OutputPath -Value $text -Encoding UTF8
    Write-Output "Wrote $OutputPath"
    Write-Output "chars=$($text.Length)"
}
