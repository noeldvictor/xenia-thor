[CmdletBinding()]
param(
    [string[]]$CodegenAuditPaths = @(
        "scratch\thor-debug\20260526-080921-82282490-822825e0-codegen-audit.txt",
        "scratch\thor-debug\20260526-054200-82281d28-hot-block-codegen-audit.txt",
        "scratch\thor-debug\20260526-080921-82282490-8228252c-codegen-audit.txt",
        "scratch\thor-debug\20260526-080921-82287788-822877bc-codegen-audit.txt"
    ),
    [string]$NoWrapReportPath = "docs\research\20260526-083000-a64-no-wrap-memory-eligibility-audit.md",
    [string]$NonclosedCacheReportPath = "docs\research\20260526-092500-a64-nonclosed-gpr-cache-capture.md",
    [string]$VmxRouteReportPath = "docs\research\20260526-012000-vmx128-route-stabilized-counters.md",
    [string]$FastEntryReportPath = "docs\research\20260526-111500-a64-fast-entry-emitter-planning.md"
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

function Read-OptionalText {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        return ""
    }
    return Get-Content -LiteralPath $resolved -Raw
}

function Add-Score {
    param(
        [hashtable]$Table,
        [string]$Key,
        [double]$Amount
    )

    if ($Table.ContainsKey($Key)) {
        $Table[$Key] += $Amount
    } else {
        $Table[$Key] = $Amount
    }
}

function Add-Count {
    param(
        [hashtable]$Table,
        [string]$Key,
        [int64]$Amount
    )

    if ($Table.ContainsKey($Key)) {
        $Table[$Key] += $Amount
    } else {
        $Table[$Key] = $Amount
    }
}

function Get-ShapeClass {
    param([string]$Shape)

    switch -Regex ($Shape) {
        '^store_context$|^load_context$' { return "context_state" }
        '^compare$|^branch$' { return "scalar_cr_branch" }
        '^load_offset\.|^store_offset\.|^load\.|^store\.' { return "memory_addressing" }
        '^extract_|^mul_add_v128$|^permute$|^splat_|^load_vector_' { return "vmx_vector" }
        '^call$' { return "direct_call_fast_entry" }
        '^add$|^and$|^or$|^zero_extend$|^truncate$|^convert$|^cast$' { return "scalar_dataflow" }
        default { return "other" }
    }
}

function Format-Score {
    param([double]$Value)

    return ([Math]::Round($Value, 0)).ToString("0")
}

function Parse-ContextFieldCount {
    param(
        [string]$Line,
        [string]$PrefixRegex
    )

    if ([string]::IsNullOrWhiteSpace($Line) -or $Line -notmatch '=') {
        return [int64]0
    }
    $right = $Line.Substring($Line.IndexOf('=') + 1)
    if ([string]::IsNullOrWhiteSpace($right) -or $right.Trim() -eq '-') {
        return [int64]0
    }

    $count = [int64]0
    foreach ($part in ($right -split ',')) {
        $trimmed = $part.Trim()
        if ($trimmed -match ("^{0}[^:]*:(?<n>\d+)$" -f $PrefixRegex)) {
            $count += [int64]$Matches.n
        }
    }
    return $count
}

function Parse-CodegenAudit {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        throw "Codegen audit not found: $Path"
    }

    $lines = Get-Content -LiteralPath $resolved
    $text = $lines -join "`n"

    $function = ""
    $slice = ""
    $bodyTicks = [int64]0
    $estimatedFloor = [int64]0
    $contextBarrierCount = [int64]0
    $crStoreCount = [int64]0
    $gprStoreCount = [int64]0
    $shapeRows = New-Object System.Collections.Generic.List[object]

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
            $contextBarrierCount = [int64]$Matches.value
            continue
        }
        if ($line -match '^context_stores=') {
            $crStoreCount = Parse-ContextFieldCount $line 'cr'
            $gprStoreCount = Parse-ContextFieldCount $line 'r\['
            continue
        }
        if ($line -match '^shape=(?<shape>\S+)\s+count=(?<count>\d+)\s+floor_per_op=(?<floor_per_op>\d+)\s+floor_total=(?<floor_total>\d+)') {
            $shape = $Matches.shape
            $class = Get-ShapeClass $shape
            $shapeRows.Add([pscustomobject][ordered]@{
                shape = $shape
                class = $class
                count = [int64]$Matches.count
                floor_per_op = [int64]$Matches.floor_per_op
                floor_total = [int64]$Matches.floor_total
            }) | Out-Null
        }
    }

    if ([string]::IsNullOrWhiteSpace($function) -or
        [string]::IsNullOrWhiteSpace($slice) -or
        $bodyTicks -le 0) {
        throw "Could not parse required header fields from $Path"
    }

    return [pscustomobject][ordered]@{
        path = $Path
        text = $text
        function = $function
        slice = $slice
        body_ticks_total = $bodyTicks
        estimated_floor_total = $estimatedFloor
        context_barrier_count = $contextBarrierCount
        cr_store_count = $crStoreCount
        gpr_store_count = $gprStoreCount
        shape_rows = $shapeRows
    }
}

$audits = @()
foreach ($path in $CodegenAuditPaths) {
    $audits += Parse-CodegenAudit $path
}

$classScores = @{}
$classFloorTotals = @{}
$classShapeCounts = @{}
$blockScores = New-Object System.Collections.Generic.List[object]

foreach ($audit in $audits) {
    $localClassScores = @{}
    foreach ($shape in $audit.shape_rows) {
        $score = [double]$audit.body_ticks_total * [double]$shape.floor_total
        Add-Score $classScores $shape.class $score
        Add-Count $classFloorTotals $shape.class $shape.floor_total
        Add-Count $classShapeCounts $shape.shape $shape.count
        Add-Score $localClassScores $shape.class $score
    }

    $dominant = "-"
    if ($localClassScores.Count -gt 0) {
        $dominant = ($localClassScores.GetEnumerator() |
            Sort-Object -Property @{ Expression = "Value"; Descending = $true },
                                  @{ Expression = "Name"; Ascending = $true } |
            Select-Object -First 1).Name
    }

    $blockScores.Add([pscustomobject][ordered]@{
        function = $audit.function
        slice = $audit.slice
        body_ticks_total = $audit.body_ticks_total
        estimated_floor_total = $audit.estimated_floor_total
        dominant_class = $dominant
        context_barriers = $audit.context_barrier_count
        cr_store_count = $audit.cr_store_count
        gpr_store_count = $audit.gpr_store_count
    }) | Out-Null
}

$barrierWeighted = [double]0
$crStoreWeighted = [double]0
$gprStoreWeighted = [double]0
foreach ($audit in $audits) {
    $barrierWeighted += [double]$audit.body_ticks_total * [double]$audit.context_barrier_count
    $crStoreWeighted += [double]$audit.body_ticks_total * [double]$audit.cr_store_count
    $gprStoreWeighted += [double]$audit.body_ticks_total * [double]$audit.gpr_store_count
}

$noWrapText = Read-OptionalText $NoWrapReportPath
$nonclosedText = Read-OptionalText $NonclosedCacheReportPath
$vmxText = Read-OptionalText $VmxRouteReportPath
$fastEntryText = Read-OptionalText $FastEntryReportPath

$fastmemBehaviorClosed =
    $noWrapText -match 'static_no_wrap_provable_rows=0' -and
    $noWrapText -match 'runtime_no_wrap_proven_rows=0'
$nonclosedCacheClosed =
    $nonclosedText -match 'clean_hits_possible=0' -and
    $nonclosedText -match 'dirty_hits_possible=0'
$vmxBehaviorClosed =
    $vmxText -match 'Do not patch broad VMX128' -or
    $vmxText -match 'closes broad PERMUTE'
$fastEntryClosed =
    $fastEntryText -match 'emitter_planning_source_ready_behavior_blocked' -and
    ($fastEntryText -match 'fast-entry source-only.*chain' -or
     $fastEntryText -match 'closes the current fast-entry')

Write-Output "audit=a64_route_structural_ranking"
Write-Output ("input_blocks={0} total_body_ticks={1} total_estimated_floor={2}" -f `
    $audits.Count,
    (($audits | Measure-Object -Property body_ticks_total -Sum).Sum),
    (($audits | Measure-Object -Property estimated_floor_total -Sum).Sum))

foreach ($block in ($blockScores | Sort-Object -Property @{ Expression = "body_ticks_total"; Descending = $true })) {
    Write-Output ("block function={0} slice={1} body_ticks_total={2} estimated_floor_total={3} dominant_class={4} context_barriers={5} cr_store_count={6} gpr_store_count={7}" -f `
        $block.function,
        $block.slice,
        $block.body_ticks_total,
        $block.estimated_floor_total,
        $block.dominant_class,
        $block.context_barriers,
        $block.cr_store_count,
        $block.gpr_store_count)
}

foreach ($row in ($classScores.GetEnumerator() |
        Sort-Object -Property @{ Expression = "Value"; Descending = $true },
                              @{ Expression = "Name"; Ascending = $true })) {
    $class = $row.Name
    $status = "open_source_audit"
    $reason = "not closed by current decision notes"
    if ($class -eq "memory_addressing" -and $fastmemBehaviorClosed) {
        $status = "closed_behavior"
        $reason = "no static/runtime no-wrap proof; keep offset helper only"
    } elseif ($class -eq "vmx_vector" -and $vmxBehaviorClosed) {
        $status = "closed_behavior"
        $reason = "route counters put VMX volume in closed or non-dominant blocks"
    } elseif ($class -eq "direct_call_fast_entry" -and $fastEntryClosed) {
        $status = "closed_behavior"
        $reason = "fast-entry source-only chain closed until missing codegen contracts are solved"
    } elseif ($class -eq "context_state" -and $nonclosedCacheClosed) {
        $status = "open_source_audit_only"
        $reason = "post-promotion cache behavior is closed, but scalar context load/store lowering can still be source-audited"
    } elseif ($class -eq "scalar_cr_branch") {
        $status = "open_source_audit_only"
        $reason = "dominant route class is scalar compare/branch/CR context traffic; behavior needs source proof beyond broad CR/cache closures"
    }

    Write-Output ("rank class={0} body_weighted_floor={1} floor_total={2} status={3} reason={4}" -f `
        $class,
        (Format-Score $row.Value),
        $classFloorTotals[$class],
        $status,
        $reason)
}

Write-Output ("route_flags fastmem_behavior_closed={0} nonclosed_cache_closed={1} vmx_behavior_closed={2} fast_entry_closed={3}" -f `
    $fastmemBehaviorClosed.ToString().ToLowerInvariant(),
    $nonclosedCacheClosed.ToString().ToLowerInvariant(),
    $vmxBehaviorClosed.ToString().ToLowerInvariant(),
    $fastEntryClosed.ToString().ToLowerInvariant())
Write-Output ("weighted_context_barriers={0} weighted_cr_stores={1} weighted_gpr_stores={2}" -f `
    (Format-Score $barrierWeighted),
    (Format-Score $crStoreWeighted),
    (Format-Score $gprStoreWeighted))

if ($classScores.ContainsKey("context_state") -and
    $classScores.ContainsKey("scalar_cr_branch") -and
    $fastmemBehaviorClosed -and $vmxBehaviorClosed -and $fastEntryClosed) {
    Write-Output "decision=rank_context_cr_scalar_state_source_audit_next"
    Write-Output "safe_next_patch=source_audit_a64_context_cr_compare_branch_lowering_for_body_dominant_blocks"
    Write-Output "candidate_blocks=82282490:822825E0-822825F0;82281D28:8228233C-82282374;82287788:822877BC-82287864"
    Write-Output "do_not_patch=fast_entry;host_pointer_immediate_fastmem_without_no_wrap;vmx128_closed_shapes;nonclosed_gpr_cache;single_pc_barrier_fusion;speed_ab"
} else {
    Write-Output "decision=needs_manual_review_before_next_structural_lane"
    Write-Output "safe_next_patch=inspect_failed_route_ranking_inputs"
}
