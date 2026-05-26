[CmdletBinding()]
param(
    [string]$StateSpanScriptPath = "tools\thor\thor_hir_state_span_report.ps1",
    [string]$FunctionLogMap = "82281D28=scratch\thor-debug\20260526-025401-speed-logcat.txt;82282490=scratch\thor-debug\20260526-080921-speed-logcat.txt;82287788=scratch\thor-debug\20260526-080921-speed-logcat.txt",
    [string]$ContextPromotionPath = "src\xenia\cpu\compiler\passes\context_promotion_pass.cc",
    [string]$RegisterAllocationPath = "src\xenia\cpu\compiler\passes\register_allocation_pass.cc",
    [string]$A64EmitterPath = "src\xenia\cpu\backend\a64\a64_emitter.cc",
    [string]$A64EmitterHeaderPath = "src\xenia\cpu\backend\a64\a64_emitter.h",
    [int]$Top = 16
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Resolve-RepoPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $repoRoot $Path
}

function Read-RepoText {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        throw "Path not found: $Path"
    }
    return Get-Content -LiteralPath $resolved -Raw
}

function Read-RepoLines {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        throw "Path not found: $Path"
    }
    return @(Get-Content -LiteralPath $resolved)
}

function Find-Line {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )

    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -match $Pattern) {
            return $i + 1
        }
    }
    return 0
}

function Test-Pattern {
    param(
        [string]$Text,
        [string]$Pattern
    )

    return [regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
}

function Format-Source {
    param(
        [string]$Path,
        [int]$Line
    )

    if ($Line -gt 0) {
        return ("{0}:{1}" -f $Path, $Line)
    }
    return "$Path:?"
}

function Parse-FunctionLogMap {
    param([string]$MapText)

    $entries = @()
    foreach ($rawPart in ($MapText -split ';')) {
        $part = $rawPart.Trim()
        if (!$part) {
            continue
        }
        $kv = $part -split '=', 2
        if ($kv.Count -ne 2) {
            throw "Invalid FunctionLogMap entry: $part"
        }
        $entries += [pscustomobject]@{
            Function = $kv[0].Trim()
            LogPath = $kv[1].Trim()
        }
    }
    return $entries
}

function Add-Candidate {
    param(
        [hashtable]$Table,
        [string]$Function,
        [int]$Slot,
        [int]$Score,
        [int]$SameReloads,
        [int]$SameLoadAfterStore,
        [int]$SameStoreAfterLoad,
        [int]$CrossLoadAfterStore,
        [int]$CrossLoadAfterLoad
    )

    if (!$Table.ContainsKey($Slot)) {
        $Table[$Slot] = [ordered]@{
            slot = $Slot
            score = 0
            same_reloads = 0
            same_load_after_store = 0
            same_store_after_load = 0
            cross_load_after_store = 0
            cross_load_after_load = 0
            functions = @{}
        }
    }

    $row = $Table[$Slot]
    $row.score += $Score
    $row.same_reloads += $SameReloads
    $row.same_load_after_store += $SameLoadAfterStore
    $row.same_store_after_load += $SameStoreAfterLoad
    $row.cross_load_after_store += $CrossLoadAfterStore
    $row.cross_load_after_load += $CrossLoadAfterLoad
    $row.functions[$Function] = $Score
}

$stateSpanScript = Resolve-RepoPath $StateSpanScriptPath
if (!(Test-Path -LiteralPath $stateSpanScript)) {
    throw "State-span script not found: $StateSpanScriptPath"
}

$contextPromotionText = Read-RepoText $ContextPromotionPath
$contextPromotionLines = Read-RepoLines $ContextPromotionPath
$registerAllocationText = Read-RepoText $RegisterAllocationPath
$registerAllocationLines = Read-RepoLines $RegisterAllocationPath
$a64EmitterText = Read-RepoText $A64EmitterPath
$a64EmitterLines = Read-RepoLines $A64EmitterPath
$a64EmitterHeaderText = Read-RepoText $A64EmitterHeaderPath
$a64EmitterHeaderLines = Read-RepoLines $A64EmitterHeaderPath

$closedSlots = @(1, 11)
$callArgSlots = @(3, 4, 5, 6, 7, 8, 9, 10)
$strictExcludedSlots = @($closedSlots + $callArgSlots | Sort-Object -Unique)
$ranking = @{}
$allRanking = @{}
$reports = @()

foreach ($entry in Parse-FunctionLogMap $FunctionLogMap) {
    $logPath = Resolve-RepoPath $entry.LogPath
    if (!(Test-Path -LiteralPath $logPath)) {
        throw "Log path not found for $($entry.Function): $($entry.LogPath)"
    }

    $report = @(& $stateSpanScript -LogPath $logPath -Function $entry.Function -Phase OptHIR -Top $Top)
    $reports += [pscustomobject]@{
        Function = $entry.Function
        LogPath = $entry.LogPath
        Report = $report
    }

    foreach ($line in $report) {
        if ($line -match '^\+(\d+)\s+r\[(\d+)\]\s+score=(\d+)\s+same_reloads=(\d+)\s+same_las=(\d+)\s+same_sal=(\d+)\s+cross_las=(\d+)\s+cross_lal=(\d+)') {
            $slot = [int]$Matches[2]
            $score = [int]$Matches[3]
            $sameReloads = [int]$Matches[4]
            $sameLoadAfterStore = [int]$Matches[5]
            $sameStoreAfterLoad = [int]$Matches[6]
            $crossLoadAfterStore = [int]$Matches[7]
            $crossLoadAfterLoad = [int]$Matches[8]
            Add-Candidate $allRanking $entry.Function $slot $score $sameReloads `
                $sameLoadAfterStore $sameStoreAfterLoad $crossLoadAfterStore `
                $crossLoadAfterLoad
            if ($strictExcludedSlots -notcontains $slot) {
                Add-Candidate $ranking $entry.Function $slot $score $sameReloads `
                    $sameLoadAfterStore $sameStoreAfterLoad $crossLoadAfterStore `
                    $crossLoadAfterLoad
            }
        }
    }
}

$promoteBlockLine = Find-Line $contextPromotionLines 'void ContextPromotionPass::PromoteBlock'
$processEachBlockLine = Find-Line $contextPromotionLines 'Process each block independently'
$auditHookLine = Find-Line $contextPromotionLines 'AuditGuestStateRegisterCache'
$raBlockLine = Find-Line $registerAllocationLines 'Registers do not move across blocks'
$raSpillLine = Find-Line $registerAllocationLines 'Spill required'
$emitCacheClassLine = Find-Line $a64EmitterLines 'class A64ContextValueCache'
$emitCacheResetLine = Find-Line $a64EmitterLines 'context_value_cache.ResetBlock'
$emitCacheCvarLine = Find-Line $a64EmitterLines 'cvars::arm64_context_value_cache'
$gprCountLine = Find-Line $a64EmitterHeaderLines 'GPR_COUNT\s*=\s*7'
$fixedRegisterLine = Find-Line $a64EmitterHeaderLines 'Reserved: sp, x19'

$sameBlockPromotionExists =
    (Test-Pattern $contextPromotionText 'PromoteBlock') -and
    (Test-Pattern $contextPromotionText 'OPCODE_LOAD_CONTEXT') -and
    (Test-Pattern $contextPromotionText 'OPCODE_STORE_CONTEXT') -and
    (Test-Pattern $contextPromotionText 'TryGetContextValue')
$contextPromotionPerBlock = $processEachBlockLine -gt 0
$registerAllocationPerBlock = $raBlockLine -gt 0
$registerAllocationSpills = $raSpillLine -gt 0
$emitTimeCacheResetsPerBlock = $emitCacheResetLine -gt 0
$hostGprPressure = ($gprCountLine -gt 0) -and (Test-Pattern $a64EmitterHeaderText 'x22-x28')

Write-Output "audit=a64_nonclosed_guest_state_cache"
Write-Output ("function_logs={0}" -f $FunctionLogMap)
Write-Output ("closed_slots=r1,r11 call_arg_slots=r3-r10 strict_excluded=r1,r3-r10,r11")
Write-Output ""

Write-Output "source_checks:"
Write-Output ("  same_block_context_promotion_exists={0} source={1}" -f $sameBlockPromotionExists, (Format-Source $ContextPromotionPath $promoteBlockLine))
Write-Output ("  context_promotion_per_block={0} source={1}" -f $contextPromotionPerBlock, (Format-Source $ContextPromotionPath $processEachBlockLine))
Write-Output ("  register_allocation_per_block={0} spills_to_locals={1} source={2};{3}" -f $registerAllocationPerBlock, $registerAllocationSpills, (Format-Source $RegisterAllocationPath $raBlockLine), (Format-Source $RegisterAllocationPath $raSpillLine))
Write-Output ("  emit_time_context_cache_resets_per_block={0} source={1};{2};{3}" -f $emitTimeCacheResetsPerBlock, (Format-Source $A64EmitterPath $emitCacheClassLine), (Format-Source $A64EmitterPath $emitCacheResetLine), (Format-Source $A64EmitterPath $emitCacheCvarLine))
Write-Output ("  host_gpr_pressure={0} source={1};{2}" -f $hostGprPressure, (Format-Source $A64EmitterHeaderPath $fixedRegisterLine), (Format-Source $A64EmitterHeaderPath $gprCountLine))
Write-Output ("  existing_pre_promotion_audit_hook={0} source={1}" -f ($auditHookLine -gt 0), (Format-Source $ContextPromotionPath $auditHookLine))
Write-Output ""

Write-Output "all_gpr_candidates:"
foreach ($row in ($allRanking.Values | Sort-Object -Property @{Expression = { $_.score }; Descending = $true}, slot)) {
    $functions = ($row.functions.GetEnumerator() | Sort-Object Name | ForEach-Object { "{0}:{1}" -f $_.Name, $_.Value }) -join ","
    $class = if ($closedSlots -contains $row.slot) {
        "closed_core"
    } elseif ($callArgSlots -contains $row.slot) {
        "call_arg_overlap"
    } else {
        "nonclosed_pointer_or_state"
    }
    Write-Output ("  r{0} class={1} score={2} same_reloads={3} same_las={4} same_sal={5} cross_las={6} cross_lal={7} functions={8}" -f `
        $row.slot, $class, $row.score, $row.same_reloads,
        $row.same_load_after_store, $row.same_store_after_load,
        $row.cross_load_after_store, $row.cross_load_after_load, $functions)
}
Write-Output ""

Write-Output "strict_nonclosed_candidates:"
foreach ($row in ($ranking.Values | Sort-Object -Property @{Expression = { $_.score }; Descending = $true}, slot)) {
    $functions = ($row.functions.GetEnumerator() | Sort-Object Name | ForEach-Object { "{0}:{1}" -f $_.Name, $_.Value }) -join ","
    Write-Output ("  r{0} score={1} same_reloads={2} same_las={3} same_sal={4} cross_las={5} cross_lal={6} functions={7}" -f `
        $row.slot, $row.score, $row.same_reloads,
        $row.same_load_after_store, $row.same_store_after_load,
        $row.cross_load_after_store, $row.cross_load_after_load, $functions)
}
Write-Output ""

$topStrict = @($ranking.Values | Sort-Object -Property @{Expression = { $_.score }; Descending = $true}, slot | Select-Object -First 5)
if ($topStrict.Count -gt 0) {
    $topSlots = ($topStrict | ForEach-Object { "r{0}" -f $_.slot }) -join ","
    Write-Output ("decision=nonclosed_guest_state_cache_target_exists top_slots={0}" -f $topSlots)
    Write-Output "behavior_patch_now=0"
    Write-Output "reason=same-block loads are already promoted; current emit-time cache resets per block; nonclosed candidates are mostly cross-span pointer/state GPRs and need a post-promotion CFG/lifetime audit before replacement or store elision."
    Write-Output "next_slice=default-off counter-only post-promotion nonclosed GPR state-cache audit for r31,r30,r29,r28,r27 with no store elision, no load replacement, no payload materialization, and explicit miss/flush/spill-pressure rows."
} else {
    Write-Output "decision=no_nonclosed_guest_state_cache_target"
    Write-Output "behavior_patch_now=0"
    Write-Output "next_slice=move_to_helper_block_linking_or_android_ux"
}
