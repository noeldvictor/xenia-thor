[CmdletBinding()]
param(
    [string[]]$CodegenAuditPaths = @(
        "scratch\thor-debug\20260526-080921-82282490-822825e0-codegen-audit.txt",
        "scratch\thor-debug\20260526-054200-82281d28-hot-block-codegen-audit.txt",
        "scratch\thor-debug\20260526-080921-82287788-822877bc-codegen-audit.txt"
    ),
    [string]$SequencesPath = "src\xenia\cpu\backend\a64\a64_sequences.cc",
    [string]$EmitterPath = "src\xenia\cpu\backend\a64\a64_emitter.cc",
    [string]$EmitterHeaderPath = "src\xenia\cpu\backend\a64\a64_emitter.h",
    [string]$OffsetFastpathReportPath = "docs\research\20260526-075500-a64-offset-address-fastpath-quiet-ab.md",
    [string]$NoWrapReportPath = "docs\research\20260526-083000-a64-no-wrap-memory-eligibility-audit.md",
    [string]$NonclosedCacheReportPath = "docs\research\20260526-092500-a64-nonclosed-gpr-cache-capture.md",
    [string]$CallSetupReportPath = "docs\research\20260526-032000-82281d28-call-setup-audit.md",
    [string]$CrBranchReportPath = "docs\research\20260526-114200-a64-context-cr-branch-lowering-audit.md"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return (Join-Path $repoRoot $Path)
}

function Read-RequiredText {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        throw "Required path not found: $Path"
    }
    return Get-Content -LiteralPath $resolved -Raw
}

function Read-OptionalText {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        return ""
    }
    return Get-Content -LiteralPath $resolved -Raw
}

function Parse-PairLine {
    param([string]$Line)

    $result = @{}
    if ([string]::IsNullOrWhiteSpace($Line) -or $Line -notmatch '=') {
        return $result
    }
    $right = $Line.Substring($Line.IndexOf('=') + 1)
    foreach ($part in ($right -split ',')) {
        $trimmed = $part.Trim()
        if ($trimmed -match '^(?<key>[^:]+):(?<value>\d+)$') {
            $result[$Matches.key.Trim()] = [int64]$Matches.value
        }
    }
    return $result
}

function Sum-Matching {
    param(
        [hashtable]$Table,
        [string]$Pattern
    )

    $sum = [int64]0
    foreach ($entry in $Table.GetEnumerator()) {
        if ($entry.Name -match $Pattern) {
            $sum += [int64]$entry.Value
        }
    }
    return $sum
}

function Format-TopPairs {
    param(
        [hashtable]$Table,
        [int]$Limit = 8
    )

    if ($Table.Count -eq 0) {
        return "-"
    }
    return (($Table.GetEnumerator() |
        Sort-Object -Property @{ Expression = "Value"; Descending = $true },
                              @{ Expression = "Name"; Ascending = $true } |
        Select-Object -First $Limit |
        ForEach-Object { "{0}:{1}" -f $_.Name, $_.Value }) -join ",")
}

function Parse-CodegenAudit {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        throw "Codegen audit not found: $Path"
    }

    $lines = Get-Content -LiteralPath $resolved
    $function = ""
    $slice = ""
    $bodyTicks = [int64]0
    $estimatedFloor = [int64]0
    $ppcOps = @{}
    $hirOps = @{}
    $contextLoads = @{}
    $contextStores = @{}
    $contextBarriers = [int64]0
    $calls = [int64]0
    $branches = [int64]0

    foreach ($line in $lines) {
        if ($line -match '^function=(?<value>[0-9A-Fa-f]+)') {
            $function = $Matches.value.ToUpperInvariant()
            continue
        }
        if ($line -match '^slice=(?<value>[0-9A-Fa-f]+-[0-9A-Fa-f]+)') {
            $slice = $Matches.value.ToUpperInvariant()
            continue
        }
        if ($line -match '^body_ticks_total=(?<value>\d+)') {
            $bodyTicks = [int64]$Matches.value
            continue
        }
        if ($line -match '^estimated_floor_total=(?<value>\d+)') {
            $estimatedFloor = [int64]$Matches.value
            continue
        }
        if ($line -match '^ppc_ops=') {
            $ppcOps = Parse-PairLine $line
            continue
        }
        if ($line -match '^hir_ops=') {
            $hirOps = Parse-PairLine $line
            continue
        }
        if ($line -match '^context_loads=') {
            $contextLoads = Parse-PairLine $line
            continue
        }
        if ($line -match '^context_stores=') {
            $contextStores = Parse-PairLine $line
            continue
        }
        if ($line -match '^context_barrier_note=count:(?<value>\d+)') {
            $contextBarriers = [int64]$Matches.value
            continue
        }
        if ($line -match '^context_barriers=(?<barriers>\d+)\s+calls=(?<calls>\d+)\s+branches=(?<branches>\d+)') {
            $contextBarriers = [int64]$Matches.barriers
            $calls = [int64]$Matches.calls
            $branches = [int64]$Matches.branches
            continue
        }
    }

    if ([string]::IsNullOrWhiteSpace($function) -or
        [string]::IsNullOrWhiteSpace($slice) -or
        $bodyTicks -le 0) {
        throw "Could not parse required fields from $Path"
    }

    $loadContext = Sum-Matching $hirOps '^load_context$'
    $storeContext = Sum-Matching $hirOps '^store_context$'
    $loadOffset = Sum-Matching $hirOps '^load_offset\.'
    $storeOffset = Sum-Matching $hirOps '^store_offset\.'
    $compare = Sum-Matching $hirOps '^compare'
    $branch = Sum-Matching $hirOps '^branch'
    if ($branches -gt 0) {
        $branch = $branches
    }
    $call = Sum-Matching $hirOps '^call$'
    if ($calls -gt 0) {
        $call = $calls
    }
    $gprLoads = Sum-Matching $contextLoads '^r\['
    $gprStores = Sum-Matching $contextStores '^r\['
    $crStores = Sum-Matching $contextStores '^cr'
    $lrStores = Sum-Matching $contextStores '^lr$'

    $status = "source_review"
    $reason = "needs manual source review"
    if ($function -eq "82281D28" -and $slice -match '^8228233C-8228237') {
        $status = "blocked_by_guest_call_abi_and_existing_direct_lowering"
        $reason = "context stores are call live-ins; context ldr/str is already direct from x20; guest-stack memory is already covered by offset fastpath"
    } elseif ($crStores -gt 0 -and $compare -gt 0 -and $branch -gt 0) {
        $status = "not_scalar_context_lane_cr_branch_closed"
        $reason = "CR compare/store/branch behavior is blocked by prior crash paths and CR-store elide guards"
    } elseif ($loadContext -gt 0 -or $storeContext -gt 0) {
        $status = "existing_direct_context_lowering_only"
        $reason = "LOAD_CONTEXT/STORE_CONTEXT lower to one direct context memory op where source value is already materialized"
    }

    return [pscustomobject][ordered]@{
        path = $Path
        function = $function
        slice = $slice
        body_ticks_total = $bodyTicks
        estimated_floor_total = $estimatedFloor
        load_context = $loadContext
        store_context = $storeContext
        gpr_loads = $gprLoads
        gpr_stores = $gprStores
        lr_stores = $lrStores
        cr_stores = $crStores
        load_offset = $loadOffset
        store_offset = $storeOffset
        compare = $compare
        branch = $branch
        call = $call
        context_barriers = $contextBarriers
        ppc_ops = Format-TopPairs $ppcOps 10
        context_load_top = Format-TopPairs $contextLoads 10
        context_store_top = Format-TopPairs $contextStores 12
        status = $status
        reason = $reason
    }
}

$sequencesText = Read-RequiredText $SequencesPath
$emitterText = Read-RequiredText $EmitterPath
$emitterHeaderText = Read-RequiredText $EmitterHeaderPath
$offsetFastpathText = Read-OptionalText $OffsetFastpathReportPath
$noWrapText = Read-OptionalText $NoWrapReportPath
$nonclosedText = Read-OptionalText $NonclosedCacheReportPath
$callSetupText = Read-OptionalText $CallSetupReportPath
$crBranchText = Read-OptionalText $CrBranchReportPath

$blocks = @()
foreach ($path in $CodegenAuditPaths) {
    $blocks += Parse-CodegenAudit $path
}

$sourceChecks = [ordered]@{
    context_reg_is_pinned_x20 = ($emitterHeaderText -match 'GetContextReg\(\)\s+const\s+\{\s+return x20;')
    context_barrier_noop_on_a64 = ($sequencesText -match 'No-op on ARM64 \(context is always in x20\)')
    load_context_direct_ldr = (
        $sequencesText -match 'OPCODE_LOAD_CONTEXT' -and
        $sequencesText -match 'e\.ldr\(i\.dest,\s*ptr\(e\.GetContextReg\(\),\s*offset\)\)'
    )
    store_context_direct_str = (
        $sequencesText -match 'OPCODE_STORE_CONTEXT' -and
        $sequencesText -match 'e\.str\(i\.src2,\s*ptr\(e\.GetContextReg\(\),\s*offset\)\)'
    )
    zero_store_uses_zero_register = (
        $sequencesText -match 'e\.str\(e\.xzr,\s*ptr\(e\.GetContextReg\(\),\s*offset\)\)' -and
        $sequencesText -match 'e\.str\(e\.wzr,\s*ptr\(e\.GetContextReg\(\),\s*offset\)\)'
    )
    context_traffic_audit_present = (
        $emitterText -match 'MaybeLogContextTrafficAudit' -and
        $emitterText -match 'context_loads=\{\} context_stores=\{\}'
    )
    offset_fastpath_route_positive = (
        $offsetFastpathText -match 'locally positive' -or
        $offsetFastpathText -match 'local signal is positive' -or
        $offsetFastpathText -match 'generated-code/body-time improvement'
    )
    no_wrap_host_pointer_fastmem_closed = (
        $noWrapText -match 'static_no_wrap_provable_rows=0' -and
        $noWrapText -match 'runtime_no_wrap_proven_rows=0'
    )
    nonclosed_gpr_cache_closed = (
        $nonclosedText -match 'clean_hits_possible=0' -and
        $nonclosedText -match 'dirty_hits_possible=0'
    )
    call_setup_local_elision_closed = (
        $callSetupText -match 'strict local promotion found zero safe wins' -or
        $callSetupText -match 'strict_redundant_loads=0'
    )
    cr_branch_behavior_closed = (
        $crBranchText -match 'decision=close_cr_branch_behavior_keep_source_audit_only'
    )
}

$weightedLoadContext = [double]0
$weightedStoreContext = [double]0
$weightedGprStores = [double]0
$weightedCrStores = [double]0
$weightedLoadOffset = [double]0
foreach ($block in $blocks) {
    $weightedLoadContext += [double]$block.body_ticks_total * [double]$block.load_context
    $weightedStoreContext += [double]$block.body_ticks_total * [double]$block.store_context
    $weightedGprStores += [double]$block.body_ticks_total * [double]$block.gpr_stores
    $weightedCrStores += [double]$block.body_ticks_total * [double]$block.cr_stores
    $weightedLoadOffset += [double]$block.body_ticks_total * [double]$block.load_offset
}

Write-Output "audit=a64_scalar_context_load_store_lowering"
foreach ($entry in $sourceChecks.GetEnumerator()) {
    Write-Output ("source_check {0}={1}" -f $entry.Key, $entry.Value.ToString().ToLowerInvariant())
}

foreach ($block in ($blocks | Sort-Object -Property @{ Expression = "body_ticks_total"; Descending = $true })) {
    Write-Output ("block function={0} slice={1} body_ticks_total={2} estimated_floor_total={3} load_context={4} store_context={5} gpr_loads={6} gpr_stores={7} lr_stores={8} cr_stores={9} load_offset={10} store_offset={11} calls={12} branches={13} context_barriers={14} status={15} reason={16} context_load_top={17} context_store_top={18} ppc_ops={19}" -f `
        $block.function,
        $block.slice,
        $block.body_ticks_total,
        $block.estimated_floor_total,
        $block.load_context,
        $block.store_context,
        $block.gpr_loads,
        $block.gpr_stores,
        $block.lr_stores,
        $block.cr_stores,
        $block.load_offset,
        $block.store_offset,
        $block.call,
        $block.branch,
        $block.context_barriers,
        $block.status,
        $block.reason,
        $block.context_load_top,
        $block.context_store_top,
        $block.ppc_ops)
}

Write-Output ("weighted load_context={0} store_context={1} gpr_stores={2} cr_stores={3} load_offset={4}" -f `
    ([Math]::Round($weightedLoadContext, 0).ToString("0")),
    ([Math]::Round($weightedStoreContext, 0).ToString("0")),
    ([Math]::Round($weightedGprStores, 0).ToString("0")),
    ([Math]::Round($weightedCrStores, 0).ToString("0")),
    ([Math]::Round($weightedLoadOffset, 0).ToString("0")))

$allSourceChecksPass = $true
foreach ($entry in $sourceChecks.GetEnumerator()) {
    if (-not [bool]$entry.Value) {
        $allSourceChecksPass = $false
        break
    }
}

$hot82281d28 = @($blocks | Where-Object {
        $_.function -eq "82281D28" -and $_.slice -match '^8228233C-8228237'
    })
$hasBlockedAbiBlock = $hot82281d28.Count -gt 0 -and
    $hot82281d28[0].status -eq "blocked_by_guest_call_abi_and_existing_direct_lowering"

if ($allSourceChecksPass -and $hasBlockedAbiBlock) {
    Write-Output "decision=close_scalar_context_load_store_behavior_for_current_route"
    Write-Output "safe_next_patch=lane_switch_or_broader_cfg_static_superblock_design_only"
    Write-Output "closed_reason=context_load_store_lowering_is_already_direct;hot_82281d28_stores_are_guest_call_live_ins;offset_fastpath_already_handles_guest_stack_loads;no_wrap_and_nonclosed_cache_lanes_are_closed"
    Write-Output "do_not_patch=store_context_elision;load_context_replacement;context_barrier_fusion;cr_store_elide;branch_across_barrier;guest_stack_handoff;fast_entry_behavior;host_pointer_immediate_fastmem;speed_ab"
} else {
    Write-Output "decision=needs_manual_review_before_scalar_context_behavior"
    Write-Output "safe_next_patch=inspect_failed_source_checks_or_missing_82281d28_block_evidence"
}
