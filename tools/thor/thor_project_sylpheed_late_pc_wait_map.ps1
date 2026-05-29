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

function Add-NestedCount {
    param(
        [hashtable]$Table,
        [string]$Outer,
        [string]$Inner
    )

    if (!$Outer) {
        return
    }
    if (!$Inner) {
        $Inner = ""
    }

    $outer = $Outer.ToUpperInvariant()
    $inner = $Inner.ToUpperInvariant()
    if (!$Table.ContainsKey($outer)) {
        $Table[$outer] = @{}
    }
    if (!$Table[$outer].ContainsKey($inner)) {
        $Table[$outer][$inner] = 0
    }
    ++$Table[$outer][$inner]
}

function Join-Keys {
    param([hashtable]$Table)

    return (@($Table.Keys | Sort-Object) -join ",")
}

function Parse-LogHeader {
    param([string]$Line)

    if ($Line -notmatch '^(?<month>\d{2})-(?<day>\d{2})\s+(?<clock>\d{2}:\d{2}:\d{2}\.\d{3})\s+(?<pid>\d+)\s+(?<tid>\d+)\s+') {
        return $null
    }

    $time = Try-ParseLogcatTimestamp $Line
    if (!$time) {
        return $null
    }

    return [pscustomobject][ordered]@{
        Timestamp = $time
        Pid = [int]$Matches.pid
        Tid = [int]$Matches.tid
    }
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

function Format-ChildCounts {
    param(
        [hashtable]$Table,
        [int]$Limit = 4
    )

    if (!$Table -or $Table.Count -eq 0) {
        return ""
    }

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

function Format-HandleSamples {
    param(
        [pscustomobject[]]$Rows,
        [int]$MaxSamples = 4
    )

    $lines = @()
    if (!$Rows -or $Rows.Count -eq 0) {
        return $lines
    }

    $ordered = @($Rows | Sort-Object LineIndex)
    $samples = @()
    $limit = [Math]::Max(1, [Math]::Min($MaxSamples, $ordered.Count))

    $head = @($ordered | Select-Object -First ([Math]::Ceiling($limit / 2)))
    $tailCount = $limit - $head.Count
    $tail = @()
    if ($tailCount -gt 0) {
        $tail = @($ordered | Select-Object -Last $tailCount)
    }

    $seen = @{}
    foreach ($row in $head) {
        if (!$seen.ContainsKey($row.LineIndex)) {
            $samples += $row
            $seen[$row.LineIndex] = $true
        }
    }
    foreach ($row in $tail) {
        if (!$seen.ContainsKey($row.LineIndex)) {
            $samples += $row
            $seen[$row.LineIndex] = $true
        }
    }

    $samples = @($samples | Sort-Object LineIndex)
    foreach ($sample in $samples) {
        $lines += ("line={0} time={1} api={2} phase={3} status={4} thread={5} guest_object={6} name={7}" -f
            $sample.LineIndex,
            (Format-TimeOnly $sample.Timestamp),
            $sample.Api,
            $sample.Phase,
            $sample.Status,
            $sample.ThreadId,
            $sample.GuestObject,
            $sample.Name
        )
    }

    return $lines
}

function Format-HandleLifecycleSamples {
    param(
        [pscustomobject[]]$Rows,
        [int]$MaxSamples = 4
    )

    $lines = @()
    if (!$Rows -or $Rows.Count -eq 0) {
        return $lines
    }

    $ordered = @($Rows | Sort-Object LineIndex)
    $samples = @()
    $limit = [Math]::Max(1, [Math]::Min($MaxSamples, $ordered.Count))

    $head = @($ordered | Select-Object -First ([Math]::Ceiling($limit / 2)))
    $tailCount = $limit - $head.Count
    $tail = @()
    if ($tailCount -gt 0) {
        $tail = @($ordered | Select-Object -Last $tailCount)
    }

    $seen = @{}
    foreach ($row in $head) {
        if (!$seen.ContainsKey($row.LineIndex)) {
            $samples += $row
            $seen[$row.LineIndex] = $true
        }
    }
    foreach ($row in $tail) {
        if (!$seen.ContainsKey($row.LineIndex)) {
            $samples += $row
            $seen[$row.LineIndex] = $true
        }
    }

    $samples = @($samples | Sort-Object LineIndex)
    foreach ($sample in $samples) {
        $lines += ("line={0} time={1} action={2} owner={3} type={4}" -f
            $sample.LineIndex,
            (Format-TimeOnly $sample.Timestamp),
            $sample.Action,
            $sample.Owner,
            $sample.Type
        )
    }

    return $lines
}

function Format-HandleLifecycleSummary {
    param(
        [pscustomobject[]]$Rows
    )

    if (!$Rows -or $Rows.Count -eq 0) {
        return "lifecycle_rows=0"
    }

    $ordered = @($Rows | Sort-Object LineIndex)
    $first = $ordered[0]
    $last = $ordered[$ordered.Count - 1]

    $actions = @{}
    $types = @{}
    $owners = @{}
    foreach ($row in $ordered) {
        Add-Count $actions $row.Action
        Add-Count $types $row.Type
        Add-Count $owners $row.Owner
    }

    return ("lifecycle_rows={0} actions={1} types={2} owners={3} first={4}:{5} last={6}:{7}" -f
        $Rows.Count,
        (Format-ChildCounts $actions),
        (Format-ChildCounts $types),
        (Format-ChildCounts $owners),
        (Format-TimeOnly $first.Timestamp),
        $first.LineIndex,
        (Format-TimeOnly $last.Timestamp),
        $last.LineIndex
    )
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

    $deltaMs = [int][Math]::Round(($FirstWaitTimestamp - $LastFailedFreeTimestamp).TotalMilliseconds)
    if ($deltaMs -le 15000) {
        return 0
    }

    return [int]([Math]::Max(0, $deltaMs - 5000))
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
        Handle = $Matches.handle.ToUpperInvariant()
        GuestObject = $Matches.guest_object.ToUpperInvariant()
        Status = $Matches.status.ToUpperInvariant()
        Lr = $Matches.lr.ToUpperInvariant()
        Ctr = $Matches.ctr.ToUpperInvariant()
        R1 = $Matches.r1.ToUpperInvariant()
        Name = $Matches.name
    }
}

function Parse-HandleLifecycleRow {
    param(
        [string]$Line,
        [int]$LineIndex
    )

    $pattern = "i>\s+(?<owner>[0-9A-Fa-f]{8}) (?<action>Added|Removed) handle:(?<handle>[0-9A-Fa-f]{8}) for (?<type>\S+)"
    if ($Line -notmatch $pattern) {
        return $null
    }

    return [pscustomobject][ordered]@{
        LineIndex = $LineIndex
        Timestamp = Try-ParseLogcatTimestamp $Line
        Owner = $Matches.owner.ToUpperInvariant()
        Action = $Matches.action.ToUpperInvariant()
        Handle = $Matches.handle.ToUpperInvariant()
        Type = $Matches.type
    }
}

function Parse-VdSwapRow {
    param(
        [string]$Line,
        [int]$LineIndex
    )

    $header = Parse-LogHeader $Line
    if (!$header) {
        return $null
    }

    if ($Line -notmatch 'i>\s+(?<caller>[0-9A-Fa-f]{8})\s+VdSwap\(') {
        return $null
    }

    return [pscustomobject][ordered]@{
        LineIndex = $LineIndex
        Timestamp = $header.Timestamp
        Pid = $header.Pid
        Tid = $header.Tid
        Caller = $Matches.caller.ToUpperInvariant()
    }
}

function Parse-NtStatusRow {
    param(
        [string]$Line,
        [int]$LineIndex
    )

    $header = Parse-LogHeader $Line
    if (!$header) {
        return $null
    }

    if ($Line -notmatch 'i>\s+(?<caller>[0-9A-Fa-f]{8})\s+xeRtlNtStatusToDosError\s+(?<status>[0-9A-Fa-f]{1,8})\s+=>\s+(?<mapped>[0-9A-Fa-f]{1,8})') {
        return $null
    }

    return [pscustomobject][ordered]@{
        LineIndex = $LineIndex
        Timestamp = $header.Timestamp
        Pid = $header.Pid
        Tid = $header.Tid
        Caller = $Matches.caller.ToUpperInvariant()
        Status = $Matches.status.ToUpperInvariant().PadLeft(8, "0")
        Mapped = $Matches.mapped.ToUpperInvariant().PadLeft(8, "0")
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

function Convert-HexAddress {
    param([string]$Text)

    try {
        return [Convert]::ToUInt32($Text, 16)
    } catch {
        return $null
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
$targetValues = @{}
foreach ($pc in $TargetPc) {
    if ($pc) {
        $normalizedPc = $pc.ToUpperInvariant()
        $targetSet[$normalizedPc] = $true
        $value = Convert-HexAddress $normalizedPc
        if ($null -ne $value) {
            $targetValues[$normalizedPc] = $value
        }
    }
}

$waitRows = @()
$snapshotRows = @()
$freeRows = @()
$vdSwapRows = @()
$statusRows = @()
$handleLifecycleRows = @()
$filteredDumpSeen = @{}
$postFailedHandleRows = @{}

for ($index = 0; $index -lt $lines.Count; ++$index) {
    $line = $lines[$index]
    $lineNumber = $index + 1

    $handleLifecycleRow = Parse-HandleLifecycleRow -Line $line -LineIndex $lineNumber
    if ($handleLifecycleRow) {
        $handleLifecycleRows += $handleLifecycleRow
        continue
    }

    $waitRow = Parse-WaitRow -Line $line -LineIndex ($index + 1)
    if ($waitRow) {
        $waitRows += $waitRow
        continue
    }

    $vdSwapRow = Parse-VdSwapRow -Line $line -LineIndex $lineNumber
    if ($vdSwapRow) {
        $vdSwapRows += $vdSwapRow
        continue
    }

    $statusRow = Parse-NtStatusRow -Line $line -LineIndex $lineNumber
    if ($statusRow) {
        $statusRows += $statusRow
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

    if ($line -match "Filtered function dump") {
        if ($line -match "Filtered function dump (?<start>[0-9A-Fa-f]{8})-(?<end>[0-9A-Fa-f]{8})") {
            $rangeStart = Convert-HexAddress $Matches.start
            $rangeEnd = Convert-HexAddress $Matches.end
            if ($null -ne $rangeStart -and $null -ne $rangeEnd) {
                if ($rangeStart -gt $rangeEnd) {
                    $tmp = $rangeStart
                    $rangeStart = $rangeEnd
                    $rangeEnd = $tmp
                }
                foreach ($pc in $targetValues.Keys) {
                    $value = [uint32]$targetValues[$pc]
                    if ($value -ge $rangeStart -and $value -le $rangeEnd) {
                        $filteredDumpSeen[$pc] = $true
                    }
                }
            }
        }

        foreach ($pc in $targetSet.Keys) {
            if ($line -match ("\b{0}\b" -f [Regex]::Escape($pc))) {
                $filteredDumpSeen[$pc] = $true
            }
        }
    }
}

$failedFreeRows = @($freeRows | Where-Object { $_.ResultKnown -eq 1 -and $_.Result -eq 0 })
$lastFailedFree = $null
if ($failedFreeRows.Count -gt 0) {
    $lastFailedFree = ($failedFreeRows | Where-Object { $_.Timestamp } | Sort-Object Timestamp | Select-Object -Last 1).Timestamp
}

$postFailedHandleRows = @{}
if ($lastFailedFree) {
    foreach ($row in $waitRows) {
        if (!$row.Timestamp -or $row.Timestamp -le $lastFailedFree) {
            continue
        }
        if (!$targetSet.ContainsKey($row.Lr) -and !$targetSet.ContainsKey($row.Ctr)) {
            continue
        }

        $handle = $row.Handle.ToUpperInvariant()
        if (!$postFailedHandleRows.ContainsKey($handle)) {
            $postFailedHandleRows[$handle] = @()
        }
        $postFailedHandleRows[$handle] += $row
    }
}

$handleLifecycleByHandle = @{}
foreach ($row in $handleLifecycleRows) {
    if (!$handleLifecycleByHandle.ContainsKey($row.Handle)) {
        $handleLifecycleByHandle[$row.Handle] = @()
    }
    $handleLifecycleByHandle[$row.Handle] += $row
}

$targetRows = @()
foreach ($pc in @($targetSet.Keys | Sort-Object)) {
    $role = New-RoleCounts
    $waitThreads = @{}
    $waitApis = @{}
    $waitStatuses = @{}
    $waitHandleRows = @{}
    $waitHandleGuestObjects = @{}
    $waitHandleStatusRows = @{}
    $waitHandleThreadRows = @{}
    $waitHandleNameRows = @{}
    $waitHandlePostFailedRows = @{}
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
            Add-Count $waitHandleRows $row.Handle
            Add-NestedCount $waitHandleGuestObjects $row.Handle $row.GuestObject
            Add-NestedCount $waitHandleStatusRows $row.Handle $row.Status
            Add-NestedCount $waitHandleThreadRows $row.Handle $row.ThreadId
            Add-NestedCount $waitHandleNameRows $row.Handle $row.Name
            if (!$waitHandlePostFailedRows.ContainsKey($row.Handle.ToUpperInvariant())) {
                $waitHandlePostFailedRows[$row.Handle.ToUpperInvariant()] = 0
            }
            if ($lastFailedFree -and $row.Timestamp -and $row.Timestamp -gt $lastFailedFree) {
                ++$waitHandlePostFailedRows[$row.Handle.ToUpperInvariant()]
            }
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
        WaitHandleRows = $waitHandleRows
        WaitHandleGuestObjects = $waitHandleGuestObjects
        WaitHandleStatusRows = $waitHandleStatusRows
        WaitHandleThreadRows = $waitHandleThreadRows
        WaitHandleNameRows = $waitHandleNameRows
        WaitHandlePostFailedRows = $waitHandlePostFailedRows
    }
}

$missingDumpRows = @($targetRows | Where-Object { $_.FilteredDumpPresent -eq 0 -and (($_.WaitLr + $_.WaitCtr + $_.SnapshotLastFn + $_.SnapshotLastRet + $_.SnapshotLr + $_.SnapshotCtr) -gt 0) })
$firstWaitTrace = ($waitRows | Where-Object { $_.Timestamp } | Sort-Object Timestamp | Select-Object -First 1).Timestamp
$waitTraceLast = ($waitRows | Where-Object { $_.Timestamp } | Sort-Object Timestamp | Select-Object -Last 1).Timestamp
$lastVdSwap = $null
if ($vdSwapRows.Count -gt 0) {
    $lastVdSwap = @($vdSwapRows | Sort-Object LineIndex | Select-Object -Last 1)[0]
}
$lastVdSwapBeforeStatus = ""
$lastVdSwapAfterStatus = ""
$secondsFromLastVdSwapToFirstWait = ""
$vdswapToFirstWaitSummary = ""
if ($lastVdSwap -and $lastVdSwap.Timestamp) {
    if ($firstWaitTrace) {
        $secondsFromLastVdSwapToFirstWait = [Math]::Max(0.0, ($firstWaitTrace - $lastVdSwap.Timestamp).TotalSeconds)
    }

    $sameThreadStatus = @($statusRows | Where-Object {
            $_.Pid -eq $lastVdSwap.Pid -and $_.Tid -eq $lastVdSwap.Tid
        } | Sort-Object LineIndex)
    if ($sameThreadStatus.Count -gt 0) {
        $statusBefore = @($sameThreadStatus | Where-Object { $_.LineIndex -le $lastVdSwap.LineIndex } | Sort-Object LineIndex | Select-Object -Last 1)
        $statusAfter = @($sameThreadStatus | Where-Object { $_.LineIndex -ge $lastVdSwap.LineIndex } | Sort-Object LineIndex | Select-Object -First 1)
        if ($statusBefore.Count -gt 0) {
            $delta = ""
            if ($statusBefore[0].Timestamp -and $lastVdSwap.Timestamp) {
                $delta = [Math]::Max(0.0, ($lastVdSwap.Timestamp - $statusBefore[0].Timestamp).TotalMilliseconds)
            }
            $lastVdSwapBeforeStatus = ("caller={0} status={1} mapped={2} line={3} time={4} delta_ms={5}" -f
                $statusBefore[0].Caller, $statusBefore[0].Status, $statusBefore[0].Mapped, $statusBefore[0].LineIndex,
                (Format-TimeOnly $statusBefore[0].Timestamp), $delta.ToString("0.0"))
        }
        if ($statusAfter.Count -gt 0 -and $statusAfter[0].LineIndex -ne $statusBefore[0].LineIndex) {
            $delta = ""
            if ($statusAfter[0].Timestamp -and $lastVdSwap.Timestamp) {
                $delta = [Math]::Max(0.0, ($statusAfter[0].Timestamp - $lastVdSwap.Timestamp).TotalMilliseconds)
            }
            $lastVdSwapAfterStatus = ("caller={0} status={1} mapped={2} line={3} time={4} delta_ms={5}" -f
                $statusAfter[0].Caller, $statusAfter[0].Status, $statusAfter[0].Mapped, $statusAfter[0].LineIndex,
                (Format-TimeOnly $statusAfter[0].Timestamp), $delta.ToString("0.0"))
        }
    }
    $firstWaitAfterVdSwap = @(
            $waitRows |
                Where-Object { $_.Timestamp -and $_.Timestamp -ge $lastVdSwap.Timestamp } |
                Sort-Object Timestamp
        )[0]
    if ($firstWaitAfterVdSwap) {
        $vdswapToFirstWaitSummary = ("pc={0} handle={1} guest_object={2} status={3} thread={4} api={5} line={6} time={7}" -f
            $firstWaitAfterVdSwap.Lr,
            $firstWaitAfterVdSwap.Handle,
            $firstWaitAfterVdSwap.GuestObject,
            $firstWaitAfterVdSwap.Status,
            $firstWaitAfterVdSwap.ThreadId,
            $firstWaitAfterVdSwap.Api,
            $firstWaitAfterVdSwap.LineIndex,
            (Format-TimeOnly $firstWaitAfterVdSwap.Timestamp))
    }
}
$vdswapTime = ""
if ($lastVdSwap -and $lastVdSwap.Timestamp) {
    $vdswapTime = Format-TimeOnly $lastVdSwap.Timestamp
}
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
$recommendedLaunchCommand += " -XboxkrnlPhysicalSuballocationAudit true -XboxkrnlPhysicalSuballocationAuditBudget 512"
$recommendedCaptureCommand = (
    "powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 " +
    "-DeviceSerial c3ca0370 -Mode Screenshot -LogcatTailLines $recommendedRemoteDebugTail"
)

$classification = "project_sylpheed_late_pc_wait_map_needs_capture"
$decision = "capture_filtered_ppc_hir_for_late_pcs_and_extend_wait_trace_into_black_loop"
if ($missingDumpRows.Count -eq 0 -and $targetRows.Count -gt 0) {
    $classification = "project_sylpheed_late_pc_wait_map_has_filtered_dumps"
    $reason = "all target PCs are visible in wait/snapshot evidence, and filtered PPC/HIR dumps are present."
    $decision = "analyze_filtered_late_pc_flows_before_behavior"
}
else {
    $reason = "target PCs are visible in wait/snapshot evidence, but filtered PPC/HIR dumps are still missing for $($missingDumpRows.Count) active target(s)"
}

$report = New-Object System.Collections.Generic.List[string]
$report.Add(("classification={0}" -f $classification))
$report.Add(("reason={0}" -f $reason))
$report.Add(("decision={0}" -f $decision))
$report.Add(("log_path={0}" -f $resolvedLogPath))
$report.Add(("line_count={0}" -f $lines.Count))
$report.Add(("target_pcs={0}" -f (@($targetSet.Keys | Sort-Object) -join ",")))
$report.Add(("recommended_disassemble_function_filter={0}" -f (@($missingDumpRows | ForEach-Object { $_.Pc }) -join ",")))
$report.Add(("wait_trace_count={0}" -f $waitRows.Count))
$report.Add(("wait_trace_first_time={0}" -f (Format-TimeOnly $firstWaitTrace)))
$report.Add(("wait_trace_last_time={0}" -f (Format-TimeOnly $waitTraceLast)))
$report.Add(("vdswap_last_time={0}" -f $vdswapTime))
$report.Add(("vdswap_last_caller={0}" -f $($lastVdSwap.Caller)))
$report.Add(("vdswap_last_status_before={0}" -f $lastVdSwapBeforeStatus))
$report.Add(("vdswap_last_status_after={0}" -f $lastVdSwapAfterStatus))
$report.Add(("seconds_from_last_vdswap_to_first_wait={0}" -f $secondsFromLastVdSwapToFirstWait))
$report.Add(("vdswap_to_first_wait={0}" -f $vdswapToFirstWaitSummary))
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
$report.Add(("post_failed_target_wait_handle_count={0}" -f $postFailedHandleRows.Count))

if ($postFailedHandleRows.Count -gt 0) {
    $postFailedHandleSummaryRows = @()
    foreach ($handle in $postFailedHandleRows.Keys) {
        $postFailedHandleSummaryRows += [pscustomobject]@{
            Handle = $handle
            Count = [int]$postFailedHandleRows[$handle].Count
        }
    }

    $postFailedHandleOrder = @(
        $postFailedHandleSummaryRows |
        Sort-Object -Property @{ Expression = "Count"; Descending = $true }, @{ Expression = "Handle"; Descending = $false } |
        ForEach-Object { $_.Handle }
    )

    $targetHandleIndex = 0
    foreach ($handle in $postFailedHandleOrder) {
        $rowsForHandle = $postFailedHandleRows[$handle]
        $threads = @{}
        $statuses = @{}
        $apis = @{}
        $phases = @{}
        $firstRow = $null
        $lastRow = $null
        $handleLifeRows = @()
        if ($handleLifecycleByHandle.ContainsKey($handle)) {
            $handleLifeRows = @($handleLifecycleByHandle[$handle])
        }
        $handleLifeSummary = Format-HandleLifecycleSummary -Rows $handleLifeRows

        foreach ($row in $rowsForHandle) {
            Add-Count $threads $row.ThreadId
            Add-Count $statuses $row.Status
            Add-Count $apis $row.Api
            Add-Count $phases $row.Phase
            if (!$firstRow -or $row.LineIndex -lt $firstRow.LineIndex) {
                $firstRow = $row
            }
            if (!$lastRow -or $row.LineIndex -gt $lastRow.LineIndex) {
                $lastRow = $row
            }
        }

        $report.Add((
                "post_failed_target_handle[{0}]=handle={1} rows={2} threads={3} statuses={4} apis={5} phases={6} first={7}:{8} last={9}:{10} {11}" -f
                $targetHandleIndex,
                $handle,
                $rowsForHandle.Count,
                (Join-Keys $threads),
                (Format-ChildCounts $statuses),
                (Join-Keys $apis),
                (Join-Keys $phases),
                (Format-TimeOnly $firstRow.Timestamp),
                $firstRow.LineIndex,
                (Format-TimeOnly $lastRow.Timestamp),
                $lastRow.LineIndex,
                $handleLifeSummary
            ))

        $sampleRows = Format-HandleSamples -Rows $rowsForHandle -MaxSamples 4
        $sampleIndex = 0
        foreach ($sampleLine in $sampleRows) {
            $report.Add(("post_failed_target_handle[{0}]_sample[{1}]={2}" -f $targetHandleIndex, $sampleIndex, $sampleLine))
            ++$sampleIndex
        }

        $handleLifeSamples = Format-HandleLifecycleSamples -Rows $handleLifeRows -MaxSamples 4
        $lifeSampleIndex = 0
        foreach ($sampleLine in $handleLifeSamples) {
            $report.Add(("post_failed_target_handle[{0}]_lifecycle_sample[{1}]={2}" -f $targetHandleIndex, $lifeSampleIndex, $sampleLine))
            ++$lifeSampleIndex
        }
        ++$targetHandleIndex
    }
}

$rowIndex = 0
foreach ($row in $targetRows) {
    $handleRows = @()
    if ($row.WaitHandleRows -and $row.WaitHandleRows.Count -gt 0) {
        $handleRows = @($row.WaitHandleRows.Keys | Sort-Object {
                $row.WaitHandleRows[$_]
            } -Descending)
    }

    $report.Add((("target_pc[{0}]_handle_count={1}" -f $rowIndex, $handleRows.Count)))
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

    $handleIndex = 0
    foreach ($handle in $handleRows) {
        $guestObjectSummary = ""
        if ($row.WaitHandleGuestObjects.ContainsKey($handle)) {
            $guestObjectSummary = Format-ChildCounts $row.WaitHandleGuestObjects[$handle]
        }
        $statusSummary = ""
        if ($row.WaitHandleStatusRows.ContainsKey($handle)) {
            $statusSummary = Format-ChildCounts $row.WaitHandleStatusRows[$handle]
        }
        $threadSummary = ""
        if ($row.WaitHandleThreadRows.ContainsKey($handle)) {
            $threadSummary = Format-ChildCounts $row.WaitHandleThreadRows[$handle]
        }
        $nameSummary = ""
        if ($row.WaitHandleNameRows.ContainsKey($handle)) {
            $nameSummary = Format-ChildCounts $row.WaitHandleNameRows[$handle]
        }
        $handleLifeRows = @()
        if ($handleLifecycleByHandle.ContainsKey($handle)) {
            $handleLifeRows = @($handleLifecycleByHandle[$handle])
        }
        $handleLifeSummary = Format-HandleLifecycleSummary -Rows $handleLifeRows
        $report.Add((
                "target_pc[{0}]_wait_handle[{1}]=handle={2} rows={3} guest_objects={4} statuses={5} threads={6} names={7} post_failed_rows={8} {9}" -f
                $rowIndex,
                $handleIndex,
                $handle,
                $row.WaitHandleRows[$handle],
                $guestObjectSummary,
                $statusSummary,
                $threadSummary,
                $nameSummary,
                $row.WaitHandlePostFailedRows[$handle],
                $handleLifeSummary
            ))
        $handleLifeSamples = Format-HandleLifecycleSamples -Rows $handleLifeRows -MaxSamples 4
        $sampleIndex = 0
        foreach ($sampleLine in $handleLifeSamples) {
            $report.Add(("target_pc[{0}]_wait_handle[{1}]_lifecycle_sample[{2}]={3}" -f $rowIndex, $handleIndex, $sampleIndex, $sampleLine))
            ++$sampleIndex
        }
        ++$handleIndex
    }
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
