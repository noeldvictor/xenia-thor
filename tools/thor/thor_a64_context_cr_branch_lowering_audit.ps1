[CmdletBinding()]
param(
    [string[]]$CodegenAuditPaths = @(
        "scratch\thor-debug\20260526-080921-82282490-822825e0-codegen-audit.txt",
        "scratch\thor-debug\20260526-054200-82281d28-hot-block-codegen-audit.txt",
        "scratch\thor-debug\20260526-080921-82287788-822877bc-codegen-audit.txt"
    ),
    [string]$SequencesPath = "src\xenia\cpu\backend\a64\a64_sequences.cc",
    [string]$BackendPath = "src\xenia\cpu\backend\a64\a64_backend.cc",
    [string]$EmitterPath = "src\xenia\cpu\backend\a64\a64_emitter.cc",
    [string]$NegativeReportPath = "docs\research\20260521-153300-a64-context-cache-cr-branch-negative.md"
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
    $contextBarriers = [int64]0
    $ppcOps = @{}
    $hirOps = @{}
    $contextStores = @{}

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
        if ($line -match '^context_barrier_note=count:(?<value>\d+)') {
            $contextBarriers = [int64]$Matches.value
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
        if ($line -match '^context_stores=') {
            $contextStores = Parse-PairLine $line
            continue
        }
    }

    if ([string]::IsNullOrWhiteSpace($function) -or
        [string]::IsNullOrWhiteSpace($slice) -or $bodyTicks -le 0) {
        throw "Could not parse required fields from $Path"
    }

    $compareCount = Sum-Matching $hirOps '^compare'
    $branchCount = Sum-Matching $hirOps '^branch'
    $storeContextCount = Sum-Matching $hirOps '^store_context$'
    $loadContextCount = Sum-Matching $hirOps '^load_context$'
    $crStoreCount = Sum-Matching $contextStores '^cr'
    $gprStoreCount = Sum-Matching $contextStores '^r\['
    $ppcCompareCount = Sum-Matching $ppcOps '^cmp|^cmpl'
    $ppcBranchCount = Sum-Matching $ppcOps '^b'

    $status = "no_cr_branch_lane"
    $reason = "no CR compare-store branch shape in this block"
    if ($crStoreCount -gt 0 -and $compareCount -gt 0 -and $branchCount -gt 0) {
        if ($contextBarriers -gt 0) {
            $status = "blocked_by_barrier_and_prior_crash"
            $reason = "branch fusion or CR-store elision would need the known default-off barrier/store-elide path"
        } else {
            $status = "covered_by_existing_safe_peephole"
            $reason = "adjacent compare/store/branch shape can use the existing non-eliding peephole"
        }
    } elseif ($crStoreCount -gt 0 -and $compareCount -gt 0) {
        $status = "covered_by_existing_cr_store_peephole"
        $reason = "CR compare-store lowering exists, but there is no immediate branch decision in this slice"
    } elseif ($gprStoreCount -gt 0 -or $loadContextCount -gt 0) {
        $status = "context_state_not_cr_branch"
        $reason = "dominant state traffic is GPR/load-store or call setup rather than CR branch lowering"
    }

    return [pscustomobject][ordered]@{
        path = $Path
        function = $function
        slice = $slice
        body_ticks_total = $bodyTicks
        estimated_floor_total = $estimatedFloor
        ppc_compare_count = $ppcCompareCount
        ppc_branch_count = $ppcBranchCount
        compare_count = $compareCount
        branch_count = $branchCount
        context_barriers = $contextBarriers
        load_context_count = $loadContextCount
        store_context_count = $storeContextCount
        cr_store_count = $crStoreCount
        gpr_store_count = $gprStoreCount
        cr_store_top = Format-TopPairs $contextStores 10
        status = $status
        reason = $reason
    }
}

$sequencesText = Read-RequiredText $SequencesPath
$backendText = Read-RequiredText $BackendPath
$emitterText = Read-RequiredText $EmitterPath
$negativeText = Read-RequiredText $NegativeReportPath

$sourceChecks = [ordered]@{
    cr_triplet_selector_present = ($sequencesText -match 'TrySelectIntegerCrTripletCompareStores')
    cr_gt_eq_selector_present = ($sequencesText -match 'TrySelectUnsignedGtEqCompareStores')
    branch_across_barrier_cvar_present = ($sequencesText -match 'arm64_cr_compare_branch_across_context_barrier')
    branch_across_barrier_default_false = ($backendText -match 'arm64_cr_compare_branch_across_context_barrier,\s*false')
    store_elide_cvar_present = ($sequencesText -match 'arm64_cr_store_elide_for_fused_branch')
    store_elide_default_false = ($backendText -match 'arm64_cr_store_elide_for_fused_branch,\s*false')
    store_elide_marked_unsafe = ($backendText -match 'unsafe outside targeted experiments')
    prior_crash_report_present = ($negativeText -match 'crashed the guest' -and $negativeText -match 'Default-off')
    context_traffic_cr_shape_counters_present = ($emitterText -match 'cr_update_triplets' -and $emitterText -match 'cr_gt_eq_pairs')
    select_sequence_uses_cr_selectors = ($sequencesText -match 'TrySelectIntegerCrTripletCompareStores\(e, i, new_tail\)' -and $sequencesText -match 'TrySelectUnsignedGtEqCompareStores\(e, i, new_tail\)')
    interleaved_store_order_present = ($sequencesText -match '(?s)cset\(lt_reg.*?strb\(lt_reg.*?cset\(gt_reg.*?strb\(gt_reg.*?cset\(eq_reg.*?strb\(eq_reg')
}

$blocks = @()
foreach ($path in $CodegenAuditPaths) {
    $blocks += Parse-CodegenAudit $path
}

$weightedCrStores = [double]0
$weightedCompares = [double]0
$weightedBranches = [double]0
$weightedBarriers = [double]0
foreach ($block in $blocks) {
    $weightedCrStores += [double]$block.body_ticks_total * [double]$block.cr_store_count
    $weightedCompares += [double]$block.body_ticks_total * [double]$block.compare_count
    $weightedBranches += [double]$block.body_ticks_total * [double]$block.branch_count
    $weightedBarriers += [double]$block.body_ticks_total * [double]$block.context_barriers
}

Write-Output "audit=a64_context_cr_branch_lowering"
foreach ($entry in $sourceChecks.GetEnumerator()) {
    Write-Output ("source_check {0}={1}" -f $entry.Key, $entry.Value.ToString().ToLowerInvariant())
}

foreach ($block in ($blocks | Sort-Object -Property @{ Expression = "body_ticks_total"; Descending = $true })) {
    Write-Output ("block function={0} slice={1} body_ticks_total={2} estimated_floor_total={3} compare_count={4} cr_store_count={5} gpr_store_count={6} branch_count={7} context_barriers={8} ppc_compare_count={9} ppc_branch_count={10} status={11} reason={12} cr_store_top={13}" -f `
        $block.function,
        $block.slice,
        $block.body_ticks_total,
        $block.estimated_floor_total,
        $block.compare_count,
        $block.cr_store_count,
        $block.gpr_store_count,
        $block.branch_count,
        $block.context_barriers,
        $block.ppc_compare_count,
        $block.ppc_branch_count,
        $block.status,
        $block.reason,
        $block.cr_store_top)
}

Write-Output ("weighted cr_stores={0} compares={1} branches={2} context_barriers={3}" -f `
    ([Math]::Round($weightedCrStores, 0).ToString("0")),
    ([Math]::Round($weightedCompares, 0).ToString("0")),
    ([Math]::Round($weightedBranches, 0).ToString("0")),
    ([Math]::Round($weightedBarriers, 0).ToString("0")))

$allSourceSafe = $true
foreach ($entry in $sourceChecks.GetEnumerator()) {
    if (-not [bool]$entry.Value) {
        $allSourceSafe = $false
        break
    }
}

$blockedCrBlocks = @($blocks | Where-Object { $_.status -eq "blocked_by_barrier_and_prior_crash" })
$contextOnlyBlocks = @($blocks | Where-Object { $_.status -eq "context_state_not_cr_branch" })

if ($allSourceSafe -and $blockedCrBlocks.Count -gt 0) {
    Write-Output "decision=close_cr_branch_behavior_keep_source_audit_only"
    Write-Output "safe_next_patch=source_audit_scalar_context_load_store_lowering_without_cr_store_elide"
    Write-Output "blocked_cr_blocks=$((($blockedCrBlocks | ForEach-Object { "{0}:{1}" -f $_.function,$_.slice }) -join ';'))"
    Write-Output "context_state_blocks=$((($contextOnlyBlocks | ForEach-Object { "{0}:{1}" -f $_.function,$_.slice }) -join ';'))"
    Write-Output "do_not_patch=arm64_cr_compare_branch_across_context_barrier;arm64_cr_store_elide_for_fused_branch;single_pc_barrier_fusion;speed_ab"
} else {
    Write-Output "decision=needs_manual_review_before_cr_or_context_behavior"
    Write-Output "safe_next_patch=inspect_source_check_failures_or_missing_block_evidence"
}
