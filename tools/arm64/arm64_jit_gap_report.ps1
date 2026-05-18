param(
  [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
  [int]$LatestLogs = 8,
  [string]$OutPath
)

$ErrorActionPreference = "Stop"

function Read-FileRaw($Path) {
  if (-not (Test-Path -LiteralPath $Path)) {
    return ""
  }
  return Get-Content -LiteralPath $Path -Raw
}

function Get-HirOpcodes($Path) {
  $opcodes = @()
  $inEnum = $false
  foreach ($line in Get-Content -LiteralPath $Path) {
    if (-not $inEnum) {
      if ($line -match "^\s*enum\s+Opcode\s*\{") {
        $inEnum = $true
      }
      continue
    }
    if ($line -match "^\s*__OPCODE_MAX_VALUE\b") {
      break
    }
    if ($line -match "^\s*(OPCODE_[A-Z0-9_]+)\b") {
      $opcodes += $Matches[1]
    }
  }
  return $opcodes
}

function Get-Cases($Text) {
  return [regex]::Matches($Text, "case\s+hir::(OPCODE_[A-Z0-9_]+)") |
    ForEach-Object { $_.Groups[1].Value } |
    Sort-Object -Unique
}

function Add-Section($Lines, $Title, $Items) {
  $Lines.Add("")
  $Lines.Add("## $Title")
  if (-not $Items -or $Items.Count -eq 0) {
    $Lines.Add("")
    $Lines.Add("- None")
    return
  }
  $Lines.Add("")
  foreach ($item in $Items) {
    $Lines.Add("- ``$item``")
  }
}

$repoRootPath = (Resolve-Path -LiteralPath $RepoRoot).Path
$opcodePath = Join-Path $repoRootPath "src\xenia\cpu\hir\opcodes.h"
$jitPath = Join-Path $repoRootPath "src\xenia\cpu\backend\arm64\arm64_jit.cc"
$functionPath = Join-Path $repoRootPath "src\xenia\cpu\backend\arm64\arm64_function.cc"

$opcodes = @(Get-HirOpcodes $opcodePath)
$jitCases = @(Get-Cases (Read-FileRaw $jitPath))
$functionCases = @(Get-Cases (Read-FileRaw $functionPath))

$missingJit = @($opcodes | Where-Object { $_ -notin $jitCases })
$missingFunction = @($opcodes | Where-Object { $_ -notin $functionCases })

$logRoot = Join-Path $repoRootPath "scratch\thor-debug"
$logs = @()
if (Test-Path -LiteralPath $logRoot) {
  $logs = @(Get-ChildItem -LiteralPath $logRoot -Filter "*.txt" |
      Sort-Object LastWriteTime -Descending |
      Select-Object -First $LatestLogs)
}

$fallbackLines = @()
$crashLines = @()
foreach ($log in $logs) {
  $matches = Select-String -LiteralPath $log.FullName -Pattern @(
    "unsupported opcode",
    "unimplemented HIR opcode",
    "ARM64 JIT fallback",
    "unsupported .*type",
    "guest crash",
    "The GPU is hung",
    "XE_SWAP"
  ) -CaseSensitive:$false
  foreach ($match in $matches) {
    $relative = Resolve-Path -LiteralPath $match.Path -Relative
    $fallbackLines += "${relative}:$($match.LineNumber): $($match.Line.Trim())"
  }
}

$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz"
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("# ARM64 JIT Gap Report")
$lines.Add("")
$lines.Add("- Generated: $timestamp")
$lines.Add("- HIR opcodes: $($opcodes.Count)")
$lines.Add("- ARM64 mini-JIT cases: $($jitCases.Count)")
$lines.Add("- ARM64 interpreter cases: $($functionCases.Count)")
$lines.Add("- Log files scanned: $($logs.Count)")

Add-Section $lines "Missing Mini-JIT Cases" $missingJit
Add-Section $lines "Missing Interpreter Cases" $missingFunction
Add-Section $lines "Recent Log Signals" $fallbackLines

$markdown = $lines -join [Environment]::NewLine
if ($OutPath) {
  $resolvedOut = if ([System.IO.Path]::IsPathRooted($OutPath)) {
    $OutPath
  } else {
    Join-Path $repoRootPath $OutPath
  }
  $parent = Split-Path -Parent $resolvedOut
  if ($parent -and -not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Path $parent | Out-Null
  }
  Set-Content -LiteralPath $resolvedOut -Value $markdown -Encoding UTF8
  Write-Host "Wrote $resolvedOut"
} else {
  $markdown
}
