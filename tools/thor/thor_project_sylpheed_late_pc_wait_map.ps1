param(
    [string]$PacketDir = "",
    [string]$LogPath = "",
    [string]$OutPath = "",
    [string[]]$TargetPc = @("824AB214", "824AC578", "8272D048", "82762158")
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

function Try-ParseLogcatTimestamp {
    param([string]$Line)

    if ($Line -notmatch '^(?<month>\d{2})-(?<day>\d{2})\s+(?<clock>\d{2}:\d{2}:\d{2}\.\d{3})') {
        return $null
    }

    $year = (Get-Date).Year
    $text = "{0:D4}-{1}-{2} {3}" -f $year, $Matches.month, $Matches.day, $Matches.clock
    $parsed = [DateTime]::MinValue
    if ([DateTime]::TryParseExact(
            $text,
            "yyyy-MM-dd HH:mm:ss.fff",
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::AssumeLocal,
            [ref]$parsed)) {
        return $parsed
    }
    return $null
}

function Format-TimeOnly {
    param($Timestamp)

    if (!$Timestamp) {
        return ""
    }
    return $Timestamp.ToString("HH:mm:ss.fff", [Globalization.CultureInfo]::InvariantCulture)
}

function Add-Count {
    param(
        [hashtable]$Table,
        [string]$Key
    )

    if (!$Key) {
        return
    }
    $normalized = $Key.ToUpperInvariant()
    if (!$Table.ContainsKey($normalized)) {
        $Table[$normalized] = 0
    }
    ++$Table[$normalized]
}

function Join-Keys {
    param([hashtable]$Table)

    return (@($Table.Keys | Sort-Object) -join ",")
}

function Top-Counts {
    param(
        [hashtable]$Table,
        [int]$Limit = 4
    )

    $rows = @()
    foreach ($key in $Table.Keys) {
        $rows += [pscustomobject]@{
            Key = $key
            Count = [int]$Table[$key]
        }
    }

    return (@(
            $rows |
                Sort-Object -Property @{ Expression = "Count"; Descending = $true }, @{ Expression = "Key"; Descending = $false } |
                Select-Object -First $Limit |
                ForEach-Object { "{0}:{1}" -f $_.Key, $_.Count }
        ) -join ",")
}

function Add-CsvKeys {
    param(
        [hashtable]$Table,
        [string]$Csv
    )

    if (!$Csv) {
        return
    }
    foreach ($rawKey in ($Csv -split ",")) {
        $key = $rawKey.Trim().ToUpperInvariant()
        if (!$key) {
            continue
        }
        if (!$Table.ContainsKey($key)) {
            $Table[$key] = 0
        }
        ++$Table[$key]
    }
}

function Join-RecommendedThreadIds {
    param(
        [hashtable]$Table,
        [int]$Limit = 8
    )

    $rows = @()
    foreach ($key in $Table.Keys) {
        $rows += [pscustomobject]@{
            Key = $key
            Count = [int]$Table[$key]
        }
    }

    return (@(
            $rows |
                Sort-Object -Property @{ Expression = "Count"; Descending = $true }, @{ Expression = "Key"; Descending = $false } |
                Select-Object -First $Limit |
                ForEach-Object { $_.Key }
        ) -join ",")
}

function Get-RoundedTraceDelayMs {
    param(
        $FirstWaitTimestamp,
        $LastFailedFreeTimestamp
    )

    if (!$FirstWaitTimestamp -or !$LastFailedFreeTimestamp) {
        return 0
    }

    $deltaMs = [int][Math]::Round(($LastFailedFreeTimestamp - $FirstWaitTimestamp).TotalMilliseconds)
    if ($deltaMs -le 15000) {
        return 0
    }

    # Start a few seconds before the failed-free/black-loop window, and round
    # down so repeated captures use stable command lines.
    $candidate = [Math]::Max(0, $deltaMs - 5000)
    return [int]([Math]::Floor($candidate / 5000) * 5000)
}

function Parse-WaitRow {
    param(
        [string]$Line,
        [int]$LineIndex
    )

    $pattern = "Xboxkrnl wait trace (?<api>\S+) (?<phase>\S+) thid (?<thid>[0-9A-Fa-f]{8}) handle (?<handle>[0-9A-Fa-f]{8}) guest_object (?<guest_object>[0-9A-Fa-f]{8}) type (?<type>\S+) count (?<count>\d+) wait_type (?<wait_type>\d+) alertable (?<alertable>\d+) timeout (?<timeout>[0-9A-Fa-f]{16}) status (?<status>[0-9A-Fa-f]{8}) lr (?<lr>[0-9A-Fa-f]{8}) ctr (?<ctr>[0-9A-Fa-f]{8}) r1 (?<r1>[0-9A-Fa-f]{8}) name '(?<name>[^']*)'"
    if ($Line -notmatch $pattern) {
        return $null
    }

    return [pscustomobject][ordered]@{
        LineIndex = $LineIndex
        Timestamp = Try-ParseLogcatTimestamp $Line
        Api = $Matches.api
        Phase = $Matches.phase
        ThreadId = $Matches.thid.ToUpperInvariant()
        Status = $Matches.status.ToUpperInvariant()
        Lr = $Matches.lr.ToUpperInvariant()
        Ctr = $Matches.ctr.ToUpperInvariant()
        R1 = $Matches.r1.ToUpperInvariant()
        Name = $Matches.name
    }
}

function Parse-A64SnapshotRow {
    param(
        [string]$Line,
        [int]$LineIndex
    )

    $pattern = "A64 thread snapshot tid=(?<tid>[0-9A-Fa-f]+) handle=(?<handle>[0-9A-Fa-f]+) native=(?<native>[0-9A-Fa-f]+) state=(?<state>\w+) last_fn=(?<last_fn>[0-9A-Fa-f]+) last_ret=(?<last_ret>[0-9A-Fa-f]+) lr=(?<lr>[0-9A-Fa-f]+) ctr=(?<ctr>[0-9A-Fa-f]+) r1=(?<r1>[0-9A-Fa-f]+) r3=(?<r3>[0-9A-Fa-f]+) r4=(?<r4>[0-9A-Fa-f]+)"
    if ($Line -notmatch $pattern) {
        return $null
    }

    return [pscustomobject][ordered]@{
        LineIndex = $LineIndex
        Timestamp = Try-ParseLogcatTimestamp $Line
        ThreadId = $Matches.tid.ToUpperInvariant().PadLeft(8, "0")
        Handle = $Matches.handle.ToUpperInvariant()
        State = $Matches.state
        LastFn = $Matches.last_fn.ToUpperInvariant()
        LastRet = $Matches.last_ret.ToUpperInvariant()
        Lr = $Matches.lr.ToUpperInvariant()
        Ctr = $Matches.ctr.ToUpperInvariant()
        R1 = $Matches.r1.ToUpperInvariant()
        R3 = $Matches.r3.ToUpperInvariant()
        R4 = $Matches.r4.ToUpperInvariant()
    }
}

function Parse-PhysicalFreeRow {
    param(
        [string]$Line,
        [int]$LineIndex
    )

    $pattern = "Xboxkrnl physical memory audit free phase (?<phase>\S+) thid (?<thid>[0-9A-Fa-f]{8}) lr (?<lr>[0-9A-Fa-f]{8}) ctr (?<ctr>[0-9A-Fa-f]{8}) .* base_address (?<base>[0-9A-Fa-f]{8}).* result_known (?<result_known>\d+) result (?<result>\d+)"
    if ($Line -notmatch $pattern) {
        return $null
    }

    return [pscustomobject][ordered]@{
        LineIndex = $LineIndex
        Timestamp = Try-ParseLogcatTimestamp $Line
        Phase = $Matches.phase
        ThreadId = $Matches.thid.ToUpperInvariant()
        Lr = $Matches.lr.ToUpperInvariant()
        Ctr = $Matches.ctr.ToUpperInvariant()
        BaseAddress = $Matches.base.ToUpperInvariant()
        ResultKnown = [int]$Matches.result_known
        Result = [int]$Matches.result
    }
}

function New-RoleCounts {
    return [ordered]@{
        WaitLr = 0
        WaitCtr = 0
        SnapshotLastFn = 0
        SnapshotLastRet = 0
        SnapshotLr = 0
        SnapshotCtr = 0
        PostFailedSnapshotLastRet = 0
        FilteredDumpPresent = 0
    }
}

$resolvedLogPath = Resolve-LogPath -ExplicitPath $LogPath -PacketPath $PacketDir
$lines = Get-Content -LiteralPath $resolvedLogPath

$targetSet = @{}
foreach ($pc in $TargetPc) {
    if ($pc) {
        $targetSet[$pc.ToUpperInvariant()] = $true
    }
}

$waitRows = @()
$snapshotRows = @()
$freeRows = @()
$filteredDumpSeen = @{}

for ($index = 0; $index -lt $lines.Count; ++$index) {
    $line = $lines[$index]

    $waitRow = Parse-WaitRow -Line $line -LineIndex ($index + 1)
    if ($waitRow) {
        $waitRows += $waitRow
        continue
    }

    $snapshotRow = Parse-A64SnapshotRow -Line $line -LineIndex ($index + 1)
    if ($snapshotRow) {
        $snapshotRows += $snapshotRow
        continue
    }

    $freeRow = Parse-PhysicalFreeRow -Line $line -LineIndex ($index + 1)
    if ($freeRow) {
        $freeRows += $freeRow
        continue
    }

    foreach ($pc in $targetSet.Keys) {
        if ($line -match ("Filtered function dump {0}" -f $pc)) {
            $filteredDumpSeen[$pc] = $true
        }
    }
}

$failedFreeRows = @($freeRows | Where-Object { $_.ResultKnown -eq 1 -and $_.Result -eq 0 })
$lastFailedFree = $null
if ($failedFreeRows.Count -gt 0) {
    $lastFailedFree = ($failedFreeRows | Where-Object { $_.Timestamp } | Sort-Object Timestamp | Select-Object -Last 1).Timestamp
}

$targetRows = @()
foreach ($pc in @($targetSet.Keys | Sort-Object)) {
    $role = New-RoleCounts
    $waitThreads = @{}
    $waitApis = @{}
    $waitStatuses = @{}
    $snapshotThreads = @{}
    $snapshotLastFns = @{}
    $snapshotCtrs = @{}
    $firstSeen = $null
    $lastSeen = $null

    foreach ($row in $waitRows) {
        $matched = $false
        if ($row.Lr -eq $pc) {
            ++$role.WaitLr
            $matched = $true
        }
        if ($row.Ctr -eq $pc) {
            ++$role.WaitCtr
            $matched = $true
        }
        if ($matched) {
            Add-Count $waitThreads $row.ThreadId
            Add-Count $waitApis $row.Api
            Add-Count $waitStatuses $row.Status
            if ($row.Timestamp) {
                if (!$firstSeen -or $row.Timestamp -lt $firstSeen) {
                    $firstSeen = $row.Timestamp
                }
                if (!$lastSeen -or $row.Timestamp -gt $lastSeen) {
                    $lastSeen = $row.Timestamp
                }
            }
        }
    }

    foreach ($row in $snapshotRows) {
        $matched = $false
        if ($row.LastFn -eq $pc) {
            ++$role.SnapshotLastFn
            $matched = $true
        }
        if ($row.LastRet -eq $pc) {
            ++$role.SnapshotLastRet
            $matched = $true
            if ($lastFailedFree -and $row.Timestamp -and $row.Timestamp -gt $lastFailedFree) {
                ++$role.PostFailedSnapshotLastRet
            }
        }
        if ($row.Lr -eq $pc) {
            ++$role.SnapshotLr
            $matched = $true
        }
        if ($row.Ctr -eq $pc) {
            ++$role.SnapshotCtr
            $matched = $true
        }
        if ($matched) {
            Add-Count $snapshotThreads $row.ThreadId
            Add-Count $snapshotLastFns $row.LastFn
            Add-Count $snapshotCtrs $row.Ctr
            if ($row.Timestamp) {
                if (!$firstSeen -or $row.Timestamp -lt $firstSeen) {
                    $firstSeen = $row.Timestamp
                }
                if (!$lastSeen -or $row.Timestamp -gt $lastSeen) {
                    $lastSeen = $row.Timestamp
                }
            }
        }
    }

    if ($filteredDumpSeen.ContainsKey($pc)) {
        $role.FilteredDumpPresent = 1
    }

    $targetRows += [pscustomobject][ordered]@{
        Pc = $pc
        WaitLr = $role.WaitLr
        WaitCtr = $role.WaitCtr
        SnapshotLastFn = $role.SnapshotLastFn
        SnapshotLastRet = $role.SnapshotLastRet
        SnapshotLr = $role.SnapshotLr
        SnapshotCtr = $role.SnapshotCtr
        PostFailedSnapshotLastRet = $role.PostFailedSnapshotLastRet
        FilteredDumpPresent = $role.FilteredDumpPresent
        FirstSeen = $firstSeen
        LastSeen = $lastSeen
        WaitThreads = Join-Keys $waitThreads
        WaitApis = Join-Keys $waitApis
        WaitStatuses = Top-Counts $waitStatuses
        SnapshotThreads = Join-Keys $snapshotThreads
        SnapshotLastFns = Top-Counts $snapshotLastFns
        SnapshotCtrs = Top-Counts $snapshotCtrs
    }
}

$missingDumpRows = @($targetRows | Where-Object { $_.FilteredDumpPresent -eq 0 -and (($_.WaitLr + $_.WaitCtr + $_.SnapshotLastFn + $_.SnapshotLastRet + $_.SnapshotLr + $_.SnapshotCtr) -gt 0) })
$firstWaitTrace = ($waitRows | Where-Object { $_.Timestamp } | Sort-Object Timestamp | Select-Object -First 1).Timestamp
$waitTraceLast = ($waitRows | Where-Object { $_.Timestamp } | Sort-Object Timestamp | Select-Object -Last 1).Timestamp
$lateSnapshotRows = @()
if ($lastFailedFree) {
    $lateSnapshotRows = @($snapshotRows | Where-Object { $_.Timestamp -and $_.Timestamp -gt $lastFailedFree })
}

$recommendedThreadIds = @{}
foreach ($row in $targetRows) {
    if (($row.WaitLr + $row.WaitCtr) -gt 0) {
        Add-CsvKeys $recommendedThreadIds $row.WaitThreads
    }
    if ($row.SnapshotLastFn -gt 0) {
        Add-CsvKeys $recommendedThreadIds $row.SnapshotThreads
    }
}
if ($recommendedThreadIds.Count -eq 0) {
    foreach ($row in $targetRows) {
        Add-CsvKeys $recommendedThreadIds $row.SnapshotThreads
    }
}
$recommendedThreadIdList = Join-RecommendedThreadIds $recommendedThreadIds
$recommendedAfterMs = Get-RoundedTraceDelayMs -FirstWaitTimestamp $firstWaitTrace -LastFailedFreeTimestamp $lastFailedFree
$recommendedWaitTraceBudget = 2048
$recommendedRemoteDebugTail = 200000
$recommendedFilter = (@($missingDumpRows | ForEach-Object { $_.Pc }) -join ",")
if (!$recommendedFilter) {
    $recommendedFilter = (@($targetSet.Keys | Sort-Object) -join ",")
}
$recommendedLaunchCommand = (
    "powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 " +
    "-DeviceSerial c3ca0370 -Mode LaunchLauncher " +
    "-DisassembleFunctionFilter ""$recommendedFilter"" " +
    "-Arm64SpeedProfileIntervalMs 5000 " +
    "-Arm64SpeedProfileThreadSnapshot true " +
    "-Arm64SpeedProfileThreadSnapshotOnIdle true " +
    "-XboxkrnlThreadWaitTrace true " +
    "-XboxkrnlThreadWaitTraceBudget $recommendedWaitTraceBudget " +
    "-XboxkrnlThreadWaitTraceAfterMs $recommendedAfterMs"
)
if ($recommendedThreadIdList) {
    $recommendedLaunchCommand += " -XboxkrnlThreadWaitTraceGuestTids ""$recommendedThreadIdList"""
}
$recommendedLaunchCommand += " -XboxkrnlPhysicalMemoryAudit true -XboxkrnlPhysicalMemoryAuditBudget 512"
$recommendedCaptureCommand = (
    "powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 " +
    "-DeviceSerial c3ca0370 -Mode Screenshot -LogcatTailLines $recommendedRemoteDebugTail"
)

$classification = "project_sylpheed_late_pc_wait_map_needs_capture"
$decision = "capture_filtered_ppc_hir_for_late_pcs_and_extend_wait_trace_into_black_loop"
if ($missingDumpRows.Count -eq 0 -and $targetRows.Count -gt 0) {
    $classification = "project_sylpheed_late_pc_wait_map_has_filtered_dumps"
    $decision = "analyze_filtered_late_pc_flows_before_behavior"
}

$report = New-Object System.Collections.Generic.List[string]
$report.Add(("classification={0}" -f $classification))
$report.Add(("reason=target PCs are visible in wait/snapshot evidence, but filtered PPC/HIR dumps are still missing for {0} active target(s)" -f $missingDumpRows.Count))
$report.Add(("decision={0}" -f $decision))
$report.Add(("log_path={0}" -f $resolvedLogPath))
$report.Add(("line_count={0}" -f $lines.Count))
$report.Add(("target_pcs={0}" -f (@($targetSet.Keys | Sort-Object) -join ",")))
$report.Add(("recommended_disassemble_function_filter={0}" -f (@($missingDumpRows | ForEach-Object { $_.Pc }) -join ",")))
$report.Add(("wait_trace_count={0}" -f $waitRows.Count))
$report.Add(("wait_trace_first_time={0}" -f (Format-TimeOnly $firstWaitTrace)))
$report.Add(("wait_trace_last_time={0}" -f (Format-TimeOnly $waitTraceLast)))
$report.Add(("physical_free_audit_rows={0}" -f $freeRows.Count))
$report.Add(("failed_physical_free_rows={0}" -f $failedFreeRows.Count))
$report.Add(("last_failed_free_time={0}" -f (Format-TimeOnly $lastFailedFree)))
$report.Add(("a64_snapshot_count={0}" -f $snapshotRows.Count))
$report.Add(("a64_snapshots_after_last_failed_free={0}" -f $lateSnapshotRows.Count))
$report.Add(("recommended_wait_trace_guest_tids={0}" -f $recommendedThreadIdList))
$report.Add(("recommended_wait_trace_after_ms={0}" -f $recommendedAfterMs))
$report.Add(("recommended_wait_trace_budget={0}" -f $recommendedWaitTraceBudget))
$report.Add(("recommended_remote_debug_logcat_tail_lines={0}" -f $recommendedRemoteDebugTail))
$report.Add(("recommended_launcher_command={0}" -f $recommendedLaunchCommand))
$report.Add(("recommended_capture_command={0}" -f $recommendedCaptureCommand))

$rowIndex = 0
foreach ($row in $targetRows) {
    $report.Add((
            "target_pc[{0}]={1} wait_lr={2} wait_ctr={3} snapshot_last_fn={4} snapshot_last_ret={5} snapshot_lr={6} snapshot_ctr={7} post_failed_snapshot_last_ret={8} filtered_dump_present={9} first={10} last={11} wait_threads={12} wait_apis={13} wait_statuses={14} snapshot_threads={15} snapshot_last_fns={16} snapshot_ctrs={17}" -f
            $rowIndex,
            $row.Pc,
            $row.WaitLr,
            $row.WaitCtr,
            $row.SnapshotLastFn,
            $row.SnapshotLastRet,
            $row.SnapshotLr,
            $row.SnapshotCtr,
            $row.PostFailedSnapshotLastRet,
            $row.FilteredDumpPresent,
            (Format-TimeOnly $row.FirstSeen),
            (Format-TimeOnly $row.LastSeen),
            $row.WaitThreads,
            $row.WaitApis,
            $row.WaitStatuses,
            $row.SnapshotThreads,
            $row.SnapshotLastFns,
            $row.SnapshotCtrs
        ))
    ++$rowIndex
}

if ($OutPath) {
    $parent = Split-Path -Parent $OutPath
    if ($parent -and !(Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    Set-Content -LiteralPath $OutPath -Value $report -Encoding ASCII
}

$report
