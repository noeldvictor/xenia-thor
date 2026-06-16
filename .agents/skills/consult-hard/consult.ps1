<#
.SYNOPSIS
  Consult both heavyweight external models (OpenAI Codex gpt-5.5 + Google Gemini
  3.x Pro) on a hard problem, in PARALLEL, and capture each answer to a file.

.DESCRIPTION
  Reads a prompt file (the full problem + context + the specific ask), pipes it
  to `codex exec` (gpt-5.5 at xhigh reasoning) and `gemini` (gemini-3.1-pro-preview)
  concurrently, and writes codex.md / gemini.md into the output dir. Both run at
  maximum reasoning effort, so expect MINUTES per call -- run this with a long
  timeout (600000 ms) or in the background.

.EXAMPLE
  .\consult.ps1 -PromptFile scratch\consult\problem.txt -OutDir scratch\consult
#>
param(
  [Parameter(Mandatory = $true)][string]$PromptFile,
  [string]$OutDir = "scratch/consult",
  [string]$CodexModel = "gpt-5.5",
  [string]$GeminiModel = "gemini-3.1-pro-preview",
  [string]$Effort = "xhigh"
)
$ErrorActionPreference = "Stop"

if (-not (Test-Path $PromptFile)) { throw "Prompt file not found: $PromptFile" }
New-Item -ItemType Directory -Force $OutDir | Out-Null

# Make sure the npm global bin (where codex/gemini live) is on PATH for the jobs.
$npmBin = (npm prefix -g).Trim()
$prompt = Get-Content $PromptFile -Raw

Write-Output "Consulting Codex ($CodexModel @ $Effort) + Gemini ($GeminiModel) in parallel..."
Write-Output "(max reasoning -> this takes minutes; outputs -> $OutDir)"

$codexJob = Start-Job -ArgumentList $prompt, $CodexModel, $Effort, $OutDir, $npmBin {
  param($p, $m, $eff, $out, $bin)
  $env:Path = "$bin;" + $env:Path
  $p | codex exec -m $m -c "model_reasoning_effort=$eff" --skip-git-repo-check - *>&1 |
    Out-File "$out/codex.md" -Encoding utf8
}
$geminiJob = Start-Job -ArgumentList $prompt, $GeminiModel, $OutDir, $npmBin {
  param($p, $m, $out, $bin)
  $env:Path = "$bin;" + $env:Path
  # stdin is the prompt; headless mode auto-triggers on a non-TTY pipe.
  $p | gemini -m $m *>&1 | Out-File "$out/gemini.md" -Encoding utf8
}

Wait-Job $codexJob, $geminiJob | Out-Null
Receive-Job $codexJob, $geminiJob | Out-Null
Remove-Job $codexJob, $geminiJob | Out-Null

Write-Output "DONE."
Write-Output "  Codex  -> $OutDir/codex.md  ($((Get-Item "$OutDir/codex.md" -ErrorAction SilentlyContinue).Length) bytes)"
Write-Output "  Gemini -> $OutDir/gemini.md ($((Get-Item "$OutDir/gemini.md" -ErrorAction SilentlyContinue).Length) bytes)"
