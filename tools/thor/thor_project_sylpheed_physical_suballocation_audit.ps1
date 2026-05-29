param(
    [string]$PacketDir = "",
    [string]$LogPath = "",
    [string]$OutPath = "",
    [int]$Top = 8
)

$ErrorActionPreference = "Stop"

function Resolve-LogPath {
    param(
        [string]$ExplicitPath,
        [string]$PacketPath
    )

    if ($ExplicitPath) {
        if (!(Test-Path -LiteralPath $ExplicitPath)) {
            throw "LogPath not found: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    if ($PacketPath) {
        $candidate = Join-Path $PacketPath "logcat.txt"
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Pass -LogPath or -PacketDir containing logcat.txt."
}

function Convert-Int {
    param([string]$Text)

    if (!$Text) {
        return 0
    }
    if ($Text -match '^[0-9A-Fa-f]+$') {
        return [Int64]([Convert]::ToInt64($Text, 16))
    }
    $parsed = 0
    if ([Int32]::TryParse($Text, [ref]$parsed)) {
        return $parsed
    }
    return 0
}

function Add-Count {
    param(
        [hashtable]$Table,
        [string]$Key,
        [int]$Amount = 1
    )

    if (!$Key) {
        $Key = "-"
    }
    if (!$Table.ContainsKey($Key)) {
        $Table[$Key] = 0
    }
    $Table[$Key] += $Amount
}

function Get-TopPairs {
    param(
        [hashtable]$Table,
        [int]$Limit
    )

    if ($Table.Count -eq 0) {
        return "-"
    }

    return (($Table.GetEnumerator() |
            Sort-Object @{ Expression = "Value"; Descending = $true },
                @{ Expression = "Name"; Descending = $false } |
            Select-Object -First $Limit |
            ForEach-Object { "{0}:{1}" -f $_.Name, $_.Value }) -join ",")
}

function Read-KeyValues {
    param([string]$Line)

    $values = @{}
    foreach ($match in [regex]::Matches($Line, '(?<key>[A-Za-z_][A-Za-z0-9_]*)\s+(?<value>[^\s]+)')) {
        $values[$match.Groups["key"].Value] = $match.Groups["value"].Value
    }
    return $values
}

$resolvedLogPath = Resolve-LogPath $LogPath $PacketDir
$rows = New-Object System.Collections.Generic.List[object]
$lineIndex = 0

foreach ($line in (Get-Content -LiteralPath $resolvedLogPath)) {
    ++$lineIndex
    if ($line -notmatch 'Xboxkrnl physical suballocation audit free phase') {
        continue
    }

    $values = Read-KeyValues $line
    $rows.Add([pscustomobject][ordered]@{
            Line = $lineIndex
            Time = if ($line -match '^(?<time>\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3})') { $Matches.time } else { "" }
            Thread = [string]$values["thid"]
            Lr = [string]$values["lr"]
            Ctr = [string]$values["ctr"]
            Type = [string]$values["type"]
            BaseAddress = [string]$values["base_address"]
            OwnerPhysicalBase = [string]$values["owner_physical_base"]
            OwnerParentBase = [string]$values["owner_parent_base"]
            OwnerSize = [string]$values["owner_size"]
            RequestOffset = [string]$values["request_offset"]
            RequestInterior = Convert-Int ([string]$values["request_interior"])
            RequestRegionStart = Convert-Int ([string]$values["request_region_start"])
            RequestPageAligned = Convert-Int ([string]$values["request_page_aligned"])
            LedgerInteriorFreeCount = Convert-Int ([string]$values["ledger_interior_free_count"])
            LedgerInteriorUniqueCount = Convert-Int ([string]$values["ledger_interior_unique_count"])
            LedgerInteriorDuplicateCount = Convert-Int ([string]$values["ledger_interior_duplicate_count"])
            LedgerMinOffset = [string]$values["ledger_min_offset"]
            LedgerMaxOffset = [string]$values["ledger_max_offset"]
            LedgerBoundsSpan = [string]$values["ledger_bounds_span"]
            LedgerOwnerFullyReleasedByBounds = Convert-Int ([string]$values["ledger_owner_fully_released_by_bounds"])
            CurrentParentReleaseWouldSucceed = Convert-Int ([string]$values["current_parent_release_would_succeed"])
            CurrentPhysicalReleaseWouldSucceed = Convert-Int ([string]$values["current_physical_release_would_succeed"])
            CurrentCallbackWouldFire = Convert-Int ([string]$values["current_callback_would_fire"])
            CallbackQueryUnsafeForChild = Convert-Int ([string]$values["callback_query_unsafe_for_child"])
            BehaviorChanged = Convert-Int ([string]$values["behavior_changed"])
        })
}

$ownerTable = @{}
$ctrTable = @{}
$lrTable = @{}
$interiorRows = @($rows | Where-Object { $_.RequestInterior -eq 1 })
$regionStartRows = @($rows | Where-Object { $_.RequestRegionStart -eq 1 })
$callbackUnsafeRows = @($rows | Where-Object { $_.CallbackQueryUnsafeForChild -eq 1 })
$behaviorChangedRows = @($rows | Where-Object { $_.BehaviorChanged -ne 0 })
$ownerFullyReleasedRows = @($rows | Where-Object { $_.LedgerOwnerFullyReleasedByBounds -ne 0 })
$currentParentSuccessRows = @($rows | Where-Object { $_.CurrentParentReleaseWouldSucceed -ne 0 })
$currentPhysicalSuccessRows = @($rows | Where-Object { $_.CurrentPhysicalReleaseWouldSucceed -ne 0 })
$currentCallbackRows = @($rows | Where-Object { $_.CurrentCallbackWouldFire -ne 0 })

foreach ($row in $rows) {
    Add-Count $ownerTable ("{0}:{1}" -f $row.OwnerPhysicalBase, $row.OwnerSize)
    Add-Count $ctrTable $row.Ctr
    Add-Count $lrTable $row.Lr
}

$ownerSummaries = New-Object System.Collections.Generic.List[object]
foreach ($group in ($rows | Group-Object OwnerPhysicalBase, OwnerSize)) {
    $groupRows = @($group.Group)
    $groupInteriorRows = @($groupRows | Where-Object { $_.RequestInterior -eq 1 })
    $lastRow = @($groupRows | Select-Object -Last 1)[0]
    $ownerSummaries.Add([pscustomobject][ordered]@{
            Owner = ("{0}:{1}" -f $lastRow.OwnerPhysicalBase, $lastRow.OwnerSize)
            Rows = $groupRows.Count
            InteriorRows = $groupInteriorRows.Count
            RegionStartRows = @($groupRows | Where-Object { $_.RequestRegionStart -eq 1 }).Count
            LastUniqueChildren = $lastRow.LedgerInteriorUniqueCount
            LastDuplicateChildren = $lastRow.LedgerInteriorDuplicateCount
            LastMinOffset = $lastRow.LedgerMinOffset
            LastMaxOffset = $lastRow.LedgerMaxOffset
            LastBoundsSpan = $lastRow.LedgerBoundsSpan
            FullyReleasedByBoundsRows = @($groupRows | Where-Object { $_.LedgerOwnerFullyReleasedByBounds -ne 0 }).Count
            ParentReleaseWouldSucceedRows = @($groupRows | Where-Object { $_.CurrentParentReleaseWouldSucceed -ne 0 }).Count
            PhysicalReleaseWouldSucceedRows = @($groupRows | Where-Object { $_.CurrentPhysicalReleaseWouldSucceed -ne 0 }).Count
            CallbackWouldFireRows = @($groupRows | Where-Object { $_.CurrentCallbackWouldFire -ne 0 }).Count
            CallbackUnsafeRows = @($groupRows | Where-Object { $_.CallbackQueryUnsafeForChild -ne 0 }).Count
        })
}

$classification = "project_sylpheed_physical_suballocation_no_rows"
$decision = "rerun_with_xboxkrnl_physical_suballocation_audit_enabled"
$reason = "no physical suballocation audit rows were found"
if ($rows.Count -gt 0) {
    if ($interiorRows.Count -gt 0 -and $ownerFullyReleasedRows.Count -eq 0) {
        $classification = "project_sylpheed_suballocation_children_seen_no_owner_release"
        $reason = "interior child frees are recorded, but bounds never prove the owner allocation is fully released"
        $decision = "do_not_round_down_or_hide_physical_frees; design explicit owner_child_contract_or_prove_presentation_independence"
    } elseif ($ownerFullyReleasedRows.Count -gt 0) {
        $classification = "project_sylpheed_suballocation_owner_full_release_candidate"
        $reason = "one or more ledger rows say child bounds cover an owner allocation"
        $decision = "review_owner_full_release_rows_before_any_behavior_patch"
    } else {
        $classification = "project_sylpheed_physical_suballocation_rows_no_interior_children"
        $reason = "audit rows are present, but no interior child free rows were captured"
        $decision = "check_route_or_title_state_before_behavior"
    }
}

$report = New-Object System.Collections.Generic.List[string]
$report.Add(("classification={0}" -f $classification))
$report.Add(("reason={0}" -f $reason))
$report.Add(("decision={0}" -f $decision))
$report.Add(("log_path={0}" -f $resolvedLogPath))
$report.Add(("audit_row_count={0}" -f $rows.Count))
$report.Add(("interior_request_count={0}" -f $interiorRows.Count))
$report.Add(("region_start_request_count={0}" -f $regionStartRows.Count))
$report.Add(("callback_query_unsafe_for_child_count={0}" -f $callbackUnsafeRows.Count))
$report.Add(("ledger_owner_fully_released_by_bounds_count={0}" -f $ownerFullyReleasedRows.Count))
$report.Add(("current_parent_release_would_succeed_count={0}" -f $currentParentSuccessRows.Count))
$report.Add(("current_physical_release_would_succeed_count={0}" -f $currentPhysicalSuccessRows.Count))
$report.Add(("current_callback_would_fire_count={0}" -f $currentCallbackRows.Count))
$report.Add(("behavior_changed_count={0}" -f $behaviorChangedRows.Count))
$report.Add(("owner_top={0}" -f (Get-TopPairs $ownerTable $Top)))
$report.Add(("ctr_top={0}" -f (Get-TopPairs $ctrTable $Top)))
$report.Add(("lr_top={0}" -f (Get-TopPairs $lrTable $Top)))
$report.Add("")
$report.Add("## Owner Summary")
foreach ($summary in ($ownerSummaries |
        Sort-Object @{ Expression = "InteriorRows"; Descending = $true },
            @{ Expression = "Rows"; Descending = $true },
            @{ Expression = "Owner"; Descending = $false } |
        Select-Object -First $Top)) {
    $report.Add((
            "owner={0} rows={1} interior={2} region_start={3} unique_children={4} duplicates={5} min_offset={6} max_offset={7} bounds_span={8} fully_released_by_bounds={9} parent_release_ok={10} physical_release_ok={11} callback_would_fire={12} callback_unsafe={13}" -f
            $summary.Owner,
            $summary.Rows,
            $summary.InteriorRows,
            $summary.RegionStartRows,
            $summary.LastUniqueChildren,
            $summary.LastDuplicateChildren,
            $summary.LastMinOffset,
            $summary.LastMaxOffset,
            $summary.LastBoundsSpan,
            $summary.FullyReleasedByBoundsRows,
            $summary.ParentReleaseWouldSucceedRows,
            $summary.PhysicalReleaseWouldSucceedRows,
            $summary.CallbackWouldFireRows,
            $summary.CallbackUnsafeRows))
}

$report.Add("")
$report.Add("## Last Interior Rows")
foreach ($row in ($interiorRows | Select-Object -Last $Top)) {
    $report.Add((
            "time={0} thread={1} lr={2} ctr={3} base={4} owner={5}:{6} offset={7} unique_children={8} duplicates={9} bounds_span={10} fully_released_by_bounds={11} callback_unsafe={12}" -f
            $row.Time,
            $row.Thread,
            $row.Lr,
            $row.Ctr,
            $row.BaseAddress,
            $row.OwnerPhysicalBase,
            $row.OwnerSize,
            $row.RequestOffset,
            $row.LedgerInteriorUniqueCount,
            $row.LedgerInteriorDuplicateCount,
            $row.LedgerBoundsSpan,
            $row.LedgerOwnerFullyReleasedByBounds,
            $row.CallbackQueryUnsafeForChild))
}

if ($OutPath) {
    $outDirectory = Split-Path -Parent $OutPath
    if ($outDirectory) {
        New-Item -ItemType Directory -Force -Path $outDirectory | Out-Null
    }
    $report | Set-Content -LiteralPath $OutPath -Encoding UTF8
}

$report | Write-Output
