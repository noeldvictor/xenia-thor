param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string[]]$LogPath = @(),
    [int]$Latest = 1,
    [string]$OutPath = ""
)

$ErrorActionPreference = "Stop"

function Add-Count {
    param(
        [hashtable]$Table,
        [string]$Key,
        [int64]$Amount = 1
    )
    if (!$Table.ContainsKey($Key)) {
        $Table[$Key] = 0
    }
    $Table[$Key] += $Amount
}

function Get-HirOpcodeNames {
    param([string]$RepoRoot)

    $path = Join-Path $RepoRoot "src\xenia\cpu\hir\opcodes.h"
    $names = @{}
    $inEnum = $false
    $index = 0
    foreach ($line in Get-Content -Path $path) {
        if (!$inEnum) {
            if ($line -match "^\s*enum\s+Opcode\s*\{") {
                $inEnum = $true
            }
            continue
        }

        if ($line -match "^\s*__OPCODE_MAX_VALUE\b") {
            break
        }
        if ($line -match "^\s*(OPCODE_[A-Z0-9_]+)\b") {
            $names[$index] = $Matches[1]
            $index++
        }
    }
    return $names
}

function Get-OpcodeName {
    param(
        [hashtable]$OpcodeNames,
        [int]$Opcode
    )
    if ($OpcodeNames.ContainsKey($Opcode)) {
        return $OpcodeNames[$Opcode]
    }
    return "OPCODE_$Opcode"
}

function Add-MarkdownCountTable {
    param(
        [System.Collections.Generic.List[string]]$Lines,
        [string]$Title,
        [hashtable]$Counts,
        [hashtable]$OpcodeNames,
        [switch]$KeysAreOpcodes
    )

    $Lines.Add("")
    $Lines.Add("## $Title")
    $Lines.Add("")
    if ($Counts.Count -eq 0) {
        $Lines.Add("- None found.")
        return
    }

    if ($KeysAreOpcodes) {
        $Lines.Add("| Opcode | Name | Count |")
        $Lines.Add("| --- | --- | ---: |")
        foreach ($entry in $Counts.GetEnumerator() | Sort-Object -Property Value -Descending) {
            $opcode = [int]$entry.Key
            $Lines.Add("| $opcode | $(Get-OpcodeName $OpcodeNames $opcode) | $($entry.Value) |")
        }
    } else {
        $Lines.Add("| Item | Count |")
        $Lines.Add("| --- | ---: |")
        foreach ($entry in $Counts.GetEnumerator() | Sort-Object -Property Value -Descending) {
            $safeKey = ($entry.Key -replace "\|", "\\|")
            $Lines.Add("| $safeKey | $($entry.Value) |")
        }
    }
}

if (!$LogPath -or $LogPath.Count -eq 0) {
    $debugDir = Join-Path $RepoRoot "scratch\thor-debug"
    if (Test-Path $debugDir) {
        $LogPath = Get-ChildItem -Path $debugDir -Filter "*-logcat.txt" |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First $Latest -ExpandProperty FullName
    }
}

if (!$LogPath -or $LogPath.Count -eq 0) {
    throw "No log files found. Pass -LogPath or capture logs under scratch\thor-debug."
}

$opcodeNames = Get-HirOpcodeNames $RepoRoot
$opcodeProfileCounts = @{}
$unimplementedCounts = @{}
$unsupportedJitOpcodeCounts = @{}
$jitFallbackReasons = @{}
$jitFallbackFunctions = @{}
$compiledFunctions = @{}
$slowFunctions = @{}
$guestCrashPcs = @{}
$guestStoreWatchHits = @{}
$ppcGlobalRefs = @{}

foreach ($path in $LogPath) {
    if (!(Test-Path $path)) {
        throw "Log not found: $path"
    }

    foreach ($line in Get-Content -Path $path) {
        if ($line -match "ARM64 HIR opcode\s+(\d+):\s+(\d+)") {
            Add-Count $opcodeProfileCounts $Matches[1] ([int64]$Matches[2])
        }

        if ($line -match "unimplemented HIR opcode\s+(\d+)") {
            Add-Count $unimplementedCounts $Matches[1]
        }

        if ($line -match "ARM64 JIT fallback for guest\s+([0-9A-Fa-f]+):\s+(.+)$") {
            $guest = $Matches[1].ToUpperInvariant()
            $reason = $Matches[2].Trim()
            Add-Count $jitFallbackFunctions $guest
            Add-Count $jitFallbackReasons $reason
            if ($reason -match "unsupported opcode\s+(\d+)") {
                Add-Count $unsupportedJitOpcodeCounts $Matches[1]
            }
        }

        if ($line -match "ARM64 mini JIT compiled guest\s+([0-9A-Fa-f]+):") {
            Add-Count $compiledFunctions $Matches[1].ToUpperInvariant()
        }

        if ($line -match "ARM64 interpreter slow function\s+([0-9A-Fa-f]+):\s+step\s+(\d+)") {
            Add-Count $slowFunctions $Matches[1].ToUpperInvariant()
        }

        if ($line -match "guest PC\s+(?:to\s+)?(?:0x)?([0-9A-Fa-f]{8})" -or
            $line -match "!\>\s+\S+\s+PC:\s+0x([0-9A-Fa-f]{8})") {
            Add-Count $guestCrashPcs $Matches[1].ToUpperInvariant()
        }

        if ($line -match "ARM64 guest store watch hit: fn\s+([0-9A-Fa-f]{8})\s+guest\s+([0-9A-Fa-f]{8})\s+store\s+([0-9A-Fa-f]{8})") {
            Add-Count $guestStoreWatchHits "$($Matches[1].ToUpperInvariant())@$($Matches[2].ToUpperInvariant())->$($Matches[3].ToUpperInvariant())"
        }

        if ($line -match "ARM64 guest memory watch hit: fn\s+([0-9A-Fa-f]{8})\s+guest\s+([0-9A-Fa-f]{8})\s+range\s+([0-9A-Fa-f]{8})\s+size\s+(\d+)\s+op\s+(\S+)") {
            Add-Count $guestStoreWatchHits "$($Matches[1].ToUpperInvariant())@$($Matches[2].ToUpperInvariant())->$($Matches[3].ToUpperInvariant())/$($Matches[5])/$($Matches[4])"
        }

        if ($line -match "PPC global ref 0x([0-9A-Fa-f]{8}):\s+(\S+)\s+(\S+)\s+fn\s+0x([0-9A-Fa-f]{8})-0x([0-9A-Fa-f]{8})\s+at\s+0x([0-9A-Fa-f]{8})") {
            Add-Count $ppcGlobalRefs "$($Matches[1].ToUpperInvariant())/$($Matches[2])/$($Matches[3])/$($Matches[4].ToUpperInvariant())-$($Matches[5].ToUpperInvariant())@$($Matches[6].ToUpperInvariant())"
        }
    }
}

$stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz"
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# ARM64 HIR Coverage Report")
$lines.Add("")
$lines.Add("Generated: $stamp")
$lines.Add("")
$lines.Add("Logs:")
foreach ($path in $LogPath) {
    $lines.Add("- $path")
}

Add-MarkdownCountTable $lines "Profiled HIR Opcode Counts" $opcodeProfileCounts $opcodeNames -KeysAreOpcodes
Add-MarkdownCountTable $lines "Unimplemented Interpreter Opcodes" $unimplementedCounts $opcodeNames -KeysAreOpcodes
Add-MarkdownCountTable $lines "Unsupported Mini-JIT Opcodes" $unsupportedJitOpcodeCounts $opcodeNames -KeysAreOpcodes
Add-MarkdownCountTable $lines "Mini-JIT Fallback Reasons" $jitFallbackReasons $opcodeNames
Add-MarkdownCountTable $lines "Mini-JIT Fallback Functions" $jitFallbackFunctions $opcodeNames
Add-MarkdownCountTable $lines "Compiled Mini-JIT Functions" $compiledFunctions $opcodeNames
Add-MarkdownCountTable $lines "Slow Interpreter Functions" $slowFunctions $opcodeNames
Add-MarkdownCountTable $lines "Guest Crash PCs" $guestCrashPcs $opcodeNames
Add-MarkdownCountTable $lines "Guest Store Watch Hits" $guestStoreWatchHits $opcodeNames
Add-MarkdownCountTable $lines "PPC Global References" $ppcGlobalRefs $opcodeNames

$report = $lines -join [Environment]::NewLine
if ($OutPath) {
    $parent = Split-Path -Parent $OutPath
    if ($parent) {
        New-Item -ItemType Directory -Force $parent | Out-Null
    }
    $report | Out-File -Encoding utf8 $OutPath
}

Write-Output $report
