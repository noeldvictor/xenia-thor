param(
  [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
  [int]$LatestLogs = 12,
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

function Get-UniqueRegexMatches($Text, $Pattern, $Group = 1) {
  return @([regex]::Matches($Text, $Pattern) |
      ForEach-Object { $_.Groups[$Group].Value } |
      Where-Object { $_ } |
      Sort-Object -Unique)
}

function Get-Cases($Text) {
  return Get-UniqueRegexMatches $Text "case\s+hir::(OPCODE_[A-Z0-9_]+)"
}

function Get-X64EmitterOpcodes($Paths) {
  $all = [System.Collections.Generic.List[string]]::new()
  foreach ($path in $Paths) {
    $text = Read-FileRaw $path
    $items = Get-UniqueRegexMatches $text "EMITTER_OPCODE_TABLE\(\s*(OPCODE_[A-Z0-9_]+)"
    foreach ($item in $items) {
      $all.Add($item)
    }
  }
  return @($all | Sort-Object -Unique)
}

function Count-Lines($Path) {
  if (-not (Test-Path -LiteralPath $Path)) {
    return 0
  }
  return (Get-Content -LiteralPath $Path | Measure-Object -Line).Lines
}

function Add-Section($Lines, $Title) {
  $Lines.Add("")
  $Lines.Add("## $Title")
  $Lines.Add("")
}

function Add-Items($Lines, $Items) {
  if (-not $Items -or $Items.Count -eq 0) {
    $Lines.Add("- None")
    return
  }
  foreach ($item in $Items) {
    $Lines.Add("- $item")
  }
}

function Add-CodeItems($Lines, $Items) {
  if (-not $Items -or $Items.Count -eq 0) {
    $Lines.Add("- None")
    return
  }
  foreach ($item in $Items) {
    $Lines.Add("- ``$item``")
  }
}

$repoRootPath = (Resolve-Path -LiteralPath $RepoRoot).Path
$opcodePath = Join-Path $repoRootPath "src\xenia\cpu\hir\opcodes.h"
$x64Root = Join-Path $repoRootPath "src\xenia\cpu\backend\x64"
$arm64Root = Join-Path $repoRootPath "src\xenia\cpu\backend\arm64"
$arm64JitPath = Join-Path $arm64Root "arm64_jit.cc"
$arm64FunctionPath = Join-Path $arm64Root "arm64_function.cc"
$arm64BackendPath = Join-Path $arm64Root "arm64_backend.h"
$x64BackendPath = Join-Path $x64Root "x64_backend.h"
$arm64CodeCachePath = Join-Path $arm64Root "arm64_code_cache.h"
$x64CodeCachePath = Join-Path $x64Root "x64_code_cache.h"

$x64Files = @(Get-ChildItem -LiteralPath $x64Root -File -Filter "*.cc")
$arm64Files = @(Get-ChildItem -LiteralPath $arm64Root -File -Filter "*.cc")

$opcodes = @(Get-HirOpcodes $opcodePath)
$x64EmitterOpcodes = @(Get-X64EmitterOpcodes $x64Files.FullName)
$arm64JitCases = @(Get-Cases (Read-FileRaw $arm64JitPath))
$arm64InterpreterCases = @(Get-Cases (Read-FileRaw $arm64FunctionPath))

$missingX64Emitter = @($opcodes | Where-Object { $_ -notin $x64EmitterOpcodes })
$missingArm64Jit = @($opcodes | Where-Object { $_ -notin $arm64JitCases })
$missingArm64Interpreter = @($opcodes | Where-Object { $_ -notin $arm64InterpreterCases })

$arm64JitText = Read-FileRaw $arm64JitPath
$arm64FunctionText = Read-FileRaw $arm64FunctionPath
$helperNames = @(Get-UniqueRegexMatches $arm64JitText "\b(Arm64Jit[A-Za-z0-9_]+)\b")
$helperCallSites = @([regex]::Matches(
    $arm64JitText,
    "EmitCall\(\s*reinterpret_cast<void\*>\(&([A-Za-z0-9_]+)\)") |
    ForEach-Object { $_.Groups[1].Value })
$rejectSignals = @(
  (Select-String -LiteralPath $arm64JitPath,$arm64FunctionPath -Pattern @(
      "unsupported ",
      "missing ",
      "failed to resolve",
      "step limit",
      "guest function calls need",
      "ignoring saturate",
      "return Reject",
      "Fail\(reject_reason"
    ) -CaseSensitive:$false) |
    ForEach-Object {
      $relative = Resolve-Path -LiteralPath $_.Path -Relative
      "${relative}:$($_.LineNumber): $($_.Line.Trim())"
    }
)

$backendDiff = @()
$x64BackendText = Read-FileRaw $x64BackendPath
$arm64BackendText = Read-FileRaw $arm64BackendPath
foreach ($capability in @(
    "HostToGuestThunk",
    "GuestToHostThunk",
    "ResolveFunctionThunk",
    "InstallBreakpoint",
    "UninstallBreakpoint",
    "ExceptionCallback",
    "capstone_handle",
    "emitter_data")) {
  $x64Has = $x64BackendText -match [regex]::Escape($capability)
  $arm64Has = $arm64BackendText -match [regex]::Escape($capability)
  if ($x64Has -and -not $arm64Has) {
    $backendDiff += "ARM64 backend is missing x64 capability ``$capability``."
  }
}

$codeCacheDiff = @()
$x64CodeCacheText = Read-FileRaw $x64CodeCachePath
$arm64CodeCacheText = Read-FileRaw $arm64CodeCachePath
foreach ($capability in @(
    "indirection_table",
    "AddIndirection",
    "CommitExecutableRange",
    "PlaceData",
    "LookupUnwindInfo",
    "UnwindReservation",
    "generated_code_commit_mark")) {
  $x64Has = $x64CodeCacheText -match [regex]::Escape($capability)
  $arm64Has = $arm64CodeCacheText -match [regex]::Escape($capability)
  if ($x64Has -and -not $arm64Has) {
    $codeCacheDiff += "ARM64 code cache is missing x64 capability ``$capability``."
  }
}

$latestSignals = @()
$logRoot = Join-Path $repoRootPath "scratch\thor-debug"
if (Test-Path -LiteralPath $logRoot) {
  $logs = @(Get-ChildItem -LiteralPath $logRoot -Filter "*.txt" |
      Sort-Object LastWriteTime -Descending |
      Select-Object -First $LatestLogs)
  foreach ($log in $logs) {
    $matches = Select-String -LiteralPath $log.FullName -Pattern @(
      "ARM64 JIT fallback",
      "unsupported opcode",
      "unimplemented HIR opcode",
      "unsupported .*type",
      "interpreter slow function",
      "interpreter step limit",
      "guest crash",
      "Fatal signal",
      "GPU swap trace",
      "VdSwap",
      "XE_SWAP",
      "Vulkan IssueSwap",
      "The GPU is hung",
      "CP_RB_RPTR",
      "CP_RB_WPTR"
    ) -CaseSensitive:$false
    foreach ($match in $matches) {
      $relative = Resolve-Path -LiteralPath $match.Path -Relative
      $latestSignals += "${relative}:$($match.LineNumber): $($match.Line.Trim())"
    }
  }
}

$firstPassQueue = @(
  "Build a real AArch64 emitter backbone: ABI prolog/epilog, register allocator, native integer/scalar/vector lowering, and helper-call escape hatches only for hard cases.",
  "Port x64 backend services before more game chasing: host-to-guest thunk, guest-to-host thunk, resolve thunk, breakpoint install/uninstall, source maps, and exception/signal recovery.",
  "Replace helper-heavy mini-JIT paths in broad families: scalar integer, scalar float, memory/MMIO, control flow, vector arithmetic, vector shuffle, pack/unpack, atomics.",
  "Add compiled-vs-interpreter differential tests for HIR microprograms so each opcode family can be validated on PC/CI before Thor.",
  "Add x64-vs-ARM64 replay tests for guest functions that Blue Dragon actually compiles, comparing PPC context and touched memory after bounded execution.",
  "Make code cache production-shaped: W^X, instruction cache flush, per-function metadata, unwind info where available, indirection/resolve table, invalidation hooks.",
  "Audit PowerPC semantic fidelity beyond HIR switch coverage: CR/XER/FPSCR, saturate flag, rounding, NaN, denorms, signed overflow, carry, byte order, unaligned memory.",
  "Make Android debug loops batchable: one command for build, deploy, launch, capture, summarize, and diff against prior known-good captures.",
  "Use Ghidra for focused guest function mapping only after the backend audit identifies a suspicious Blue Dragon function, not as the primary iteration loop.",
  "Keep Thor runs as milestone checkpoints after broad passes, not as the only unit test."
)

$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz"
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("# ARM64 From x64 Conversion Audit")
$lines.Add("")
$lines.Add("- Generated: $timestamp")
$lines.Add("- Repo: $repoRootPath")
$lines.Add("- Scope: broad backend conversion inventory before more one-by-one Thor fixes.")
$lines.Add("- HIR opcodes: $($opcodes.Count)")
$lines.Add("- x64 emitter opcode tables: $($x64EmitterOpcodes.Count)")
$lines.Add("- ARM64 mini-JIT switch cases: $($arm64JitCases.Count)")
$lines.Add("- ARM64 interpreter switch cases: $($arm64InterpreterCases.Count)")
$lines.Add("- ARM64 JIT helper symbols: $($helperNames.Count)")
$lines.Add("- ARM64 JIT helper call sites: $($helperCallSites.Count)")
$lines.Add("- x64 backend .cc files: $($x64Files.Count), lines: $(($x64Files | ForEach-Object { Count-Lines $_.FullName } | Measure-Object -Sum).Sum)")
$lines.Add("- ARM64 backend .cc files: $($arm64Files.Count), lines: $(($arm64Files | ForEach-Object { Count-Lines $_.FullName } | Measure-Object -Sum).Sum)")

Add-Section $lines "Read This First"
Add-Items $lines @(
  "ARM64 switch coverage is necessary but not enough. It only says each HIR opcode has a case label; it does not prove native ABI, PPC semantic fidelity, memory ordering, vector corner cases, exception behavior, or performance.",
  "The current ARM64 path is still a research mini-JIT plus interpreter fallback. It is useful for forward progress, but it should not be treated like a converted x64 backend.",
  "Thor log symbol/import listings can mention names such as VdSwap without proving a runtime call. Treat the explicit ``GPU swap trace: VdSwap`` line as runtime proof.",
  "The faster loop is to batch-convert and batch-test backend families, then use Thor and Blue Dragon as milestone evidence."
)

Add-Section $lines "Missing HIR Switch Coverage"
$lines.Add("Mini-JIT:")
Add-CodeItems $lines $missingArm64Jit
$lines.Add("")
$lines.Add("Interpreter:")
Add-CodeItems $lines $missingArm64Interpreter
$lines.Add("")
$lines.Add("x64 emitter tables missing from scan:")
Add-CodeItems $lines $missingX64Emitter

Add-Section $lines "Backend Capability Gaps"
Add-Items $lines $backendDiff

Add-Section $lines "Code Cache Capability Gaps"
Add-Items $lines $codeCacheDiff

Add-Section $lines "ARM64 Helper-Heavy Surface"
Add-Items $lines @(
  "Unique helper symbols found in ``arm64_jit.cc``: $($helperNames.Count)",
  "Helper call sites emitted by the mini-JIT: $($helperCallSites.Count)",
  "Interpretation: lots of helper calls are fine for bring-up, but they are an explicit backlog for native AArch64 conversion and a likely reason Thor debugging feels slow."
)
$lines.Add("")
$lines.Add("Helpers:")
Add-CodeItems $lines $helperNames

Add-Section $lines "ARM64 Reject/Error Signals In Code"
Add-Items $lines $rejectSignals

Add-Section $lines "Latest Thor Evidence"
if ($latestSignals.Count -gt 120) {
  Add-Items $lines ($latestSignals | Select-Object -First 120)
  $lines.Add("- ... truncated; rerun with a smaller log set or inspect scratch logs directly.")
} else {
  Add-Items $lines $latestSignals
}

Add-Section $lines "First-Pass Conversion Queue"
Add-Items $lines $firstPassQueue

Add-Section $lines "Batch Plan"
Add-Items $lines @(
  "Batch 1: backend infrastructure parity, no game-specific hacks.",
  "Batch 2: native lowering for scalar integer/control/memory families with interpreter differential tests.",
  "Batch 3: native lowering for scalar float and rounding/NaN behavior with focused microtests.",
  "Batch 4: native vec128 arithmetic/shuffle/pack/unpack with generated test vectors.",
  "Batch 5: Blue Dragon guest-function replay harness, then Thor milestone run.",
  "Batch 6: GPU/D3D contract work only after the CPU backend stops being the most likely source of false signals."
)

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
