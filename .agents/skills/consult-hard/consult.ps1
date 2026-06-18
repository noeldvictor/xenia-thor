<#
.SYNOPSIS
  Consult the heavyweight external model (OpenAI Codex gpt-5.5) on a hard problem
  and capture the answer to a file.

.DESCRIPTION
  Reads a prompt file (the full problem + context + the specific ask) and pipes it
  to `codex exec` (gpt-5.5 at high reasoning), writing codex.md into the output dir.
  Runs at maximum reasoning effort, so expect MINUTES -- run this with a long
  timeout (600000 ms) or in the background.

  NOTE: Gemini was REMOVED 2026-06-18 (its free-tier OAuth was deprecated -
  "IneligibleTierError / migrate to Antigravity", 403 SUBSCRIPTION_REQUIRED).
  Per user direction, this skill now consults ONLY Codex/gpt-5.5. To re-add Gemini
  later you would need a GEMINI_API_KEY from AI Studio + settings.json auth flip.

.EXAMPLE
  .\consult.ps1 -PromptFile scratch\consult\problem.txt -OutDir scratch\consult
#>
param(
  [Parameter(Mandatory = $true)][string]$PromptFile,
  [string]$OutDir = "scratch/consult",
  [string]$CodexModel = "gpt-5.5",
  [string]$Effort = "high"
)
$ErrorActionPreference = "Stop"

if (-not (Test-Path $PromptFile)) { throw "Prompt file not found: $PromptFile" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

# Make sure the npm global bin (where codex lives) is on PATH.
$npmBin = (npm prefix -g).Trim()
$env:Path = "$npmBin;" + $env:Path
$prompt = Get-Content (Resolve-Path $PromptFile).Path -Raw

Write-Output "Consulting Codex ($CodexModel @ $Effort)..."
Write-Output "(max reasoning -> this takes minutes; output -> $OutDir/codex.md)"

# Use codex's own -o to write the final message. Do NOT use `*>&1 | Out-File` on
# the node exe in foreground: in PowerShell 5.1 that wraps native stderr in a
# NativeCommandError and aborts the pipeline -> empty file + exit 1 (the bug that
# bit the Codex-only rewrite; the old Gemini+Codex version dodged it via Start-Job).
$prompt | codex exec -m $CodexModel -c "model_reasoning_effort=$Effort" --skip-git-repo-check -o "$OutDir/codex.md" -

Write-Output "DONE."
Write-Output "  Codex -> $OutDir/codex.md ($((Get-Item "$OutDir/codex.md" -ErrorAction SilentlyContinue).Length) bytes)"
