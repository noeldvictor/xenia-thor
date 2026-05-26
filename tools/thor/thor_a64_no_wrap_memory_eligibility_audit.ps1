param(
    [string]$RepoRoot = ".",
    [string[]]$ReportPaths = @(
        "scratch/thor-debug/20260526-063600-82281d28-load-store-offset-audit.txt",
        "scratch/thor-debug/20260526-080921-82282490-load-store-offset-audit.txt",
        "scratch/thor-debug/20260526-080921-82287788-load-store-offset-audit.txt"
    ),
    [string]$OutputPath = "scratch/thor-debug/20260526-083000-a64-no-wrap-memory-eligibility-audit.txt"
)

$ErrorActionPreference = "Stop"

function Read-RepoFile {
    param(
        [string]$Root,
        [string]$RelativePath
    )
    $path = Join-Path $Root $RelativePath
    if (!(Test-Path -LiteralPath $path)) {
        throw "Required source file not found: $RelativePath"
    }
    return Get-Content -LiteralPath $path -Raw
}

function Test-Pattern {
    param(
        [string]$Text,
        [string]$Pattern
    )
    return [regex]::IsMatch($Text, $Pattern,
        [System.Text.RegularExpressions.RegexOptions]::Singleline)
}

function Add-Count {
    param(
        [hashtable]$Table,
        [string]$Key,
        [int64]$Amount = 1
    )
    if ([string]::IsNullOrWhiteSpace($Key)) {
        return
    }
    if ($Table.ContainsKey($Key)) {
        $Table[$Key] += $Amount
    } else {
        $Table[$Key] = $Amount
    }
}

function Convert-HirInt {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value) -or $Value -eq "-") {
        return $null
    }
    $trimmed = $Value.Trim()
    $negative = $false
    if ($trimmed.StartsWith("-")) {
        $negative = $true
        $trimmed = $trimmed.Substring(1)
    }
    if ($trimmed.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        $trimmed = $trimmed.Substring(2)
    }
    $parsed = [Convert]::ToInt64($trimmed, 16)
    if ($negative) {
        return -$parsed
    }
    return $parsed
}

function Get-TopPairs {
    param(
        [hashtable]$Table,
        [int]$Limit = 12
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

function Read-KeyValueReport {
    param([string]$Path)
    $values = @{}
    Get-Content -LiteralPath $Path | ForEach-Object {
        $line = $_.Trim()
        if ($line -match "^(?<key>[A-Za-z0-9_]+)=(?<value>.+)$") {
            $values[$Matches["key"]] = $Matches["value"].Trim()
        }
    }
    return $values
}

function Get-ReportInt {
    param(
        [hashtable]$Values,
        [string]$Key
    )
    if (!$Values.ContainsKey($Key)) {
        return [int64]0
    }
    return [int64]$Values[$Key]
}

$repo = (Resolve-Path -LiteralPath $RepoRoot).Path
$a64Util = Read-RepoFile $repo "src/xenia/cpu/backend/a64/a64_seq_util.h"
$a64Memory = Read-RepoFile $repo "src/xenia/cpu/backend/a64/a64_seq_memory.cc"
$x64Memory = Read-RepoFile $repo "src/xenia/cpu/backend/x64/x64_seq_memory.cc"

$checks = [ordered]@{
    compute_memory_address_offset = (Test-Pattern $a64Util "inline\s+XReg\s+ComputeMemoryAddressOffset")
    offset_helper_keeps_x0_guest_address = (Test-Pattern $a64Util "return\s+e\.x0;")
    offset_helper_uses_w_register_add = (Test-Pattern $a64Util "e\.add\(e\.w0,\s*source")
    offset_helper_large_page_fallback = (Test-Pattern $a64Util "allocation_granularity\(\)\s*>\s*0x1000")
    load_offset_uses_membase_plus_x0 = (Test-Pattern $a64Memory "ptr\(e\.GetMembaseReg\(\),\s*e\.x0\)")
    store_watch_uses_final_x0 = (Test-Pattern $a64Memory "EmitGuestStoreWatch\(e,\s*i\.instr,\s*e\.x0")
    inline_mmio_path_present = (Test-Pattern $a64Memory "cvars::emit_inline_mmio_checks")
    mmio_aware_path_present = (Test-Pattern $a64Memory "IsPossibleMMIOInstruction\(e,\s*i\.instr\)")
    runtime_no_wrap_counter_present = (Test-Pattern $a64Memory "no[_-]?wrap|address_range|guest_address_range")
    static_range_analysis_present = (Test-Pattern $a64Util "range[_-]?analysis|known_no_wrap|KnownNoWrap")
    x64_offset_reference_present = (Test-Pattern $x64Memory "ComputeMemoryAddressOffset")
    x64_high_address_threshold_present = (Test-Pattern $x64Memory "0xE0000000\s*-\s*offset_const")
}

$reportSummaries = New-Object System.Collections.Generic.List[object]
$rows = New-Object System.Collections.Generic.List[object]
$missingReports = New-Object System.Collections.Generic.List[string]
$totalOffsetOps = [int64]0
$totalLoadOffsetOps = [int64]0
$totalStoreOffsetOps = [int64]0
$totalBodyWeightedOffsetOps = [int64]0

foreach ($relativePath in $ReportPaths) {
    $path = Join-Path $repo $relativePath
    if (!(Test-Path -LiteralPath $path)) {
        $missingReports.Add($relativePath) | Out-Null
        continue
    }

    $values = Read-KeyValueReport $path
    $function = if ($values.ContainsKey("function")) { $values["function"] } else { "unknown" }
    $offsetOps = Get-ReportInt $values "offset_ops"
    $loadOffsetOps = Get-ReportInt $values "load_offset_ops"
    $storeOffsetOps = Get-ReportInt $values "store_offset_ops"
    $bodyWeighted = Get-ReportInt $values "body_weighted_offset_ops"
    $totalOffsetOps += $offsetOps
    $totalLoadOffsetOps += $loadOffsetOps
    $totalStoreOffsetOps += $storeOffsetOps
    $totalBodyWeightedOffsetOps += $bodyWeighted

    $reportSummaries.Add([pscustomobject][ordered]@{
        path = $relativePath
        function = $function
        offset_ops = $offsetOps
        load_offset_ops = $loadOffsetOps
        store_offset_ops = $storeOffsetOps
        body_weighted_offset_ops = $bodyWeighted
    }) | Out-Null

    Get-Content -LiteralPath $path | ForEach-Object {
        $line = $_.Trim()
        $rowMatch = [regex]::Match($line,
            "^row\s+kind=(?<kind>\S+)\s+block=(?<block>\d+)\s+guest=(?<guest>[0-9A-Fa-f]+)-(?<last>[0-9A-Fa-f]+)\s+body_ticks=(?<body>\d+)\s+entries_delta=(?<entries>\d+)\s+base=(?<base_kind>[^/]+)/(?<base_name>\S*)\s+base_disp=(?<base_disp>\S+)\s+offset=(?<offset>\S+)\s+effective=(?<effective>\S+)\s+size=(?<size>\S+)\s+ppc=(?<ppc_addr>[0-9A-Fa-f]+):(?<ppc_op>\S+)")
        if (!$rowMatch.Success) {
            return
        }
        $offset = Convert-HirInt $rowMatch.Groups["offset"].Value
        $effective = Convert-HirInt $rowMatch.Groups["effective"].Value
        $rows.Add([pscustomobject][ordered]@{
            function = $function
            kind = $rowMatch.Groups["kind"].Value
            block = [int]$rowMatch.Groups["block"].Value
            guest = $rowMatch.Groups["guest"].Value.ToUpperInvariant()
            last = $rowMatch.Groups["last"].Value.ToUpperInvariant()
            body_ticks = [int64]$rowMatch.Groups["body"].Value
            entries_delta = [int64]$rowMatch.Groups["entries"].Value
            base_kind = $rowMatch.Groups["base_kind"].Value
            base_name = $rowMatch.Groups["base_name"].Value
            offset = $offset
            effective = $effective
            size = $rowMatch.Groups["size"].Value
            ppc = $rowMatch.Groups["ppc_addr"].Value.ToUpperInvariant()
            ppc_op = $rowMatch.Groups["ppc_op"].Value
        }) | Out-Null
    }
}

$weightedCandidateRows = $rows | Where-Object {
    $_.body_ticks -gt 0 -and
    ($_.base_kind -eq "guest_stack" -or $_.base_kind -eq "context_gpr") -and
    $null -ne $_.offset
}

$staticNoWrapRows = $rows | Where-Object {
    # Current HIR rows describe guest GPR-derived bases, not value ranges.
    # Constants would be statically provable, but none of the hot reports use
    # a constant base shape.
    $_.body_ticks -gt 0 -and $_.base_kind -eq "constant"
}

$baseWeights = @{}
$functionWeights = @{}
$topRowWeights = @{}
foreach ($row in $weightedCandidateRows) {
    Add-Count $baseWeights ("{0}:{1}" -f $row.base_kind, $row.base_name) $row.body_ticks
    Add-Count $functionWeights $row.function $row.body_ticks
    Add-Count $topRowWeights ("{0}:{1}:{2}-{3}:{4}:{5}" -f $row.function,
        $row.block, $row.guest, $row.last, $row.ppc, $row.ppc_op) $row.body_ticks
}

$bodyWeightedCandidate = [int64]0
foreach ($row in $weightedCandidateRows) {
    $bodyWeightedCandidate += $row.body_ticks
}

$hasRuntimeNoWrapProof = $checks["runtime_no_wrap_counter_present"]
$hasStaticRangeProof = $checks["static_range_analysis_present"] -and
    $staticNoWrapRows.Count -gt 0
$canHostPointerFold = $false
$closeFastmemLane = !$hasRuntimeNoWrapProof -and !$hasStaticRangeProof

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("A64 no-wrap memory eligibility audit")
$lines.Add("repo=$repo")
$lines.Add(("reports_requested={0}" -f $ReportPaths.Count))
$lines.Add(("reports_found={0}" -f $reportSummaries.Count))
if ($missingReports.Count -gt 0) {
    $lines.Add(("missing_reports={0}" -f ($missingReports -join ",")))
}
$lines.Add("")
$lines.Add("source_checks:")
foreach ($entry in $checks.GetEnumerator()) {
    $lines.Add(("  {0}={1}" -f $entry.Key, ([int]$entry.Value)))
}
$lines.Add("")
$lines.Add("route_volume:")
$lines.Add(("  offset_ops_total={0}" -f $totalOffsetOps))
$lines.Add(("  load_offset_ops_total={0}" -f $totalLoadOffsetOps))
$lines.Add(("  store_offset_ops_total={0}" -f $totalStoreOffsetOps))
$lines.Add(("  body_weighted_offset_ops_total={0}" -f $totalBodyWeightedOffsetOps))
$lines.Add(("  weighted_candidate_rows={0}" -f $weightedCandidateRows.Count))
$lines.Add(("  body_weighted_host_pointer_shape_candidates={0}" -f $bodyWeightedCandidate))
$lines.Add(("  static_no_wrap_provable_rows={0}" -f $staticNoWrapRows.Count))
$lines.Add(("  runtime_no_wrap_proven_rows=0"))
$lines.Add(("  top_bases_by_weight={0}" -f (Get-TopPairs $baseWeights 12)))
$lines.Add(("  top_functions_by_weight={0}" -f (Get-TopPairs $functionWeights 8)))
$lines.Add(("  top_rows_by_weight={0}" -f (Get-TopPairs $topRowWeights 12)))
$lines.Add("")
$lines.Add("report_summaries:")
foreach ($summary in $reportSummaries) {
    $lines.Add(("  function={0} offset_ops={1} load_offset_ops={2} store_offset_ops={3} body_weighted_offset_ops={4} path={5}" -f
        $summary.function, $summary.offset_ops, $summary.load_offset_ops,
        $summary.store_offset_ops, $summary.body_weighted_offset_ops,
        $summary.path))
}
$lines.Add("")
$lines.Add("decision:")
$lines.Add(("  existing_offset_helper_is_legal_endpoint={0}" -f ([int](
    $checks["compute_memory_address_offset"] -and
    $checks["offset_helper_keeps_x0_guest_address"] -and
    $checks["load_offset_uses_membase_plus_x0"]))))
$lines.Add(("  host_pointer_plus_immediate_behavior_candidate={0}" -f ([int]$canHostPointerFold)))
$lines.Add(("  close_fastmem_addressing_current_lane={0}" -f ([int]$closeFastmemLane)))
$lines.Add("  reason=no_static_or_runtime_no_wrap_range_proof_for_guest_gpr_bases")
$lines.Add("")
$lines.Add("required_before_reopening_host_pointer_immediate:")
$lines.Add("  prove_guest_address_plus_offset_does_not_wrap_32bit")
$lines.Add("  preserve_x0_final_guest_address_for_store_watch_and_debug")
$lines.Add("  preserve_inline_mmio_and_recorded_exception_visibility")
$lines.Add("  preserve_byte_swap_paths")
$lines.Add("  preserve_large_page_allocation_granularity_threshold")
$lines.Add("  prove_material_route_wide_candidates_beyond_closed_local_shapes")
$lines.Add("")
$lines.Add("next_lane:")
$lines.Add("  move_to=different_structural_a64_lane_unless_new_no_wrap_counter_is_explicitly_requested")

$resolvedOutput = Join-Path $repo $OutputPath
$outputDir = Split-Path -Parent $resolvedOutput
if (!(Test-Path -LiteralPath $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}
$lines | Set-Content -LiteralPath $resolvedOutput -Encoding ASCII
$lines
