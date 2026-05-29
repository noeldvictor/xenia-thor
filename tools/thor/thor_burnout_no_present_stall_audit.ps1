param(
    [string]$PacketDir = "",
    [string]$LogPath = "",
    [string]$ScreenPath = "",
    [string]$MetaPath = "",
    [string]$OutPath = "",
    [string]$TargetTitleId = "454107DC",
    [int]$FreshStallMinutes = 10,
    [int]$Step = 8,
    [int]$BlackLumaThreshold = 16,
    [double]$BlackShareThreshold = 0.97,
    [int]$IgnoreTopLeftWidth = 260,
    [int]$IgnoreTopLeftHeight = 140
)

$ErrorActionPreference = "Stop"

function Resolve-PacketPath {
    param(
        [string]$ExplicitPath,
        [string]$DefaultName,
        [switch]$Required
    )

    if ($ExplicitPath) {
        if (!(Test-Path -LiteralPath $ExplicitPath)) {
            throw "Path not found: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    if ($PacketDir) {
        $candidate = Join-Path $PacketDir $DefaultName
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    if ($Required) {
        throw "Pass a path or -PacketDir containing $DefaultName."
    }
    return ""
}

function Read-KeyValueFile {
    param([string]$Path)

    $values = @{}
    if (!$Path -or !(Test-Path -LiteralPath $Path)) {
        return $values
    }

    foreach ($line in (Get-Content -LiteralPath $Path)) {
        $split = $line.IndexOf("=")
        if ($split -le 0) {
            continue
        }
        $values[$line.Substring(0, $split)] = $line.Substring($split + 1)
    }
    return $values
}

function Try-ParseMetaTimestamp {
    param([string]$Text)

    if (!$Text) {
        return $null
    }

    $offset = [DateTimeOffset]::MinValue
    if ([DateTimeOffset]::TryParse(
            $Text,
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::AssumeLocal,
            [ref]$offset)) {
        return $offset.LocalDateTime
    }

    $date = [DateTime]::MinValue
    if ([DateTime]::TryParse(
            $Text,
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::AssumeLocal,
            [ref]$date)) {
        return $date
    }

    return $null
}

function Try-ParseLogcatTimestamp {
    param(
        [string]$Line,
        [Nullable[DateTime]]$ReferenceTime
    )

    if ($Line -notmatch '^(?<month>\d{2})-(?<day>\d{2})\s+(?<clock>\d{2}:\d{2}:\d{2}\.\d{3})') {
        return $null
    }

    $year = (Get-Date).Year
    if ($ReferenceTime.HasValue) {
        $year = $ReferenceTime.Value.Year
    }

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
    return $Timestamp.ToString("MM-dd HH:mm:ss.fff", [Globalization.CultureInfo]::InvariantCulture)
}

function Measure-ScreenBlackness {
    param([string]$Path)

    if (!$Path -or !(Test-Path -LiteralPath $Path)) {
        return $null
    }

    Add-Type -AssemblyName System.Drawing
    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $width = $bitmap.Width
        $height = $bitmap.Height
        $safeStep = [Math]::Max(1, $Step)
        $samples = 0
        $nearBlack = 0
        $lumaSum = 0.0
        $nonBlackMinX = $width
        $nonBlackMinY = $height
        $nonBlackMaxX = -1
        $nonBlackMaxY = -1

        for ($y = 0; $y -lt $height; $y += $safeStep) {
            for ($x = 0; $x -lt $width; $x += $safeStep) {
                if ($x -lt $IgnoreTopLeftWidth -and $y -lt $IgnoreTopLeftHeight) {
                    continue
                }

                $pixel = $bitmap.GetPixel($x, $y)
                $luma = (0.2126 * $pixel.R) + (0.7152 * $pixel.G) + (0.0722 * $pixel.B)
                ++$samples
                $lumaSum += $luma
                if ($luma -le $BlackLumaThreshold) {
                    ++$nearBlack
                } else {
                    if ($x -lt $nonBlackMinX) { $nonBlackMinX = $x }
                    if ($y -lt $nonBlackMinY) { $nonBlackMinY = $y }
                    if ($x -gt $nonBlackMaxX) { $nonBlackMaxX = $x }
                    if ($y -gt $nonBlackMaxY) { $nonBlackMaxY = $y }
                }
            }
        }

        $share = 0.0
        $averageLuma = 0.0
        if ($samples -gt 0) {
            $share = [double]$nearBlack / [double]$samples
            $averageLuma = $lumaSum / [double]$samples
        }

        $bbox = "none"
        if ($nonBlackMaxX -ge 0) {
            $bbox = "{0},{1},{2},{3}" -f $nonBlackMinX, $nonBlackMinY, $nonBlackMaxX, $nonBlackMaxY
        }

        return [pscustomobject][ordered]@{
            Width = $width
            Height = $height
            Samples = $samples
            NearBlackPixels = $nearBlack
            NearBlackShare = $share
            AverageLuma = $averageLuma
            NonBlackBbox = $bbox
        }
    } finally {
        $bitmap.Dispose()
    }
}

function Test-LineContainsAny {
    param(
        [string]$Line,
        [string[]]$Needles
    )

    foreach ($needle in $Needles) {
        if ($Line.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            return $true
        }
    }
    return $false
}

if ($PacketDir) {
    $PacketDir = (Resolve-Path -LiteralPath $PacketDir).Path
}

if (!$LogPath -and $PacketDir) {
    $focused = Join-Path $PacketDir "logcat-focused.txt"
    if (Test-Path -LiteralPath $focused) {
        $LogPath = $focused
    } else {
        $LogPath = Join-Path $PacketDir "logcat.txt"
    }
}

$resolvedLogPath = Resolve-PacketPath -ExplicitPath $LogPath -DefaultName "logcat-focused.txt" -Required
$resolvedScreenPath = Resolve-PacketPath -ExplicitPath $ScreenPath -DefaultName "screen.png"
$resolvedMetaPath = Resolve-PacketPath -ExplicitPath $MetaPath -DefaultName "meta.txt"

$meta = Read-KeyValueFile $resolvedMetaPath
$packetTime = $null
if ($meta.ContainsKey("created_at")) {
    $packetTime = Try-ParseMetaTimestamp $meta["created_at"]
}
$screenStats = Measure-ScreenBlackness $resolvedScreenPath

$titleId = ""
$mediaId = ""
$originalPe = ""
$lineCount = 0
$launchModuleCount = 0
$vdswapCount = 0
$firstVdSwapTime = $null
$lastVdSwapTime = $null
$lastVdSwapLine = ""
$crashMarkerCount = 0
$heapMarkerCount = 0
$aaudioOpenCount = 0
$inputRows = New-Object System.Collections.Generic.List[object]
$firstVdSwapLine = ""

$crashNeedles = @(
    "AndroidRuntime",
    "FATAL EXCEPTION",
    "Fatal signal",
    "Abort message:",
    "==== CRASH DUMP",
    "guest has crashed",
    "Xenia has now paused itself",
    "SIGABRT",
    "SIGSEGV"
)
$heapNeedles = @(
    "BaseHeap::Release failed",
    "PhysicalHeap::Release failed",
    "MmFreePhysicalMemory failed",
    "RtlRaiseException"
)
$inputNeedles = @(
    "XeniaInput: key",
    "DEBUG_GAMEPAD_KEY",
    "debug-gamepad"
)

$reader = [System.IO.File]::OpenText($resolvedLogPath)
try {
    while (($line = $reader.ReadLine()) -ne $null) {
        ++$lineCount

        if ($line.IndexOf("VdSwap(", [StringComparison]::Ordinal) -ge 0) {
            ++$vdswapCount
            if (!$firstVdSwapLine) {
                $firstVdSwapLine = $line
            }
            $lastVdSwapLine = $line
            continue
        }

        if (!$titleId -and $line.IndexOf("Title ID:", [StringComparison]::OrdinalIgnoreCase) -ge 0 -and
                $line -match 'Title ID:\s*(?<value>[0-9A-Fa-f]{8})') {
            $titleId = $Matches.value.ToUpperInvariant()
        }
        if (!$mediaId -and $line.IndexOf("Media ID:", [StringComparison]::OrdinalIgnoreCase) -ge 0 -and
                $line -match 'Media ID:\s*(?<value>[0-9A-Fa-f]{8})') {
            $mediaId = $Matches.value.ToUpperInvariant()
        }
        if (!$originalPe -and $line.IndexOf("XEX_HEADER_ORIGINAL_PE_NAME:", [StringComparison]::OrdinalIgnoreCase) -ge 0 -and
                $line -match 'XEX_HEADER_ORIGINAL_PE_NAME:\s*(?<value>.+)$') {
            $originalPe = $Matches.value.Trim()
        }
        if ($line.IndexOf("Launching module", [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            ++$launchModuleCount
        }
        if (Test-LineContainsAny -Line $line -Needles $crashNeedles) {
            ++$crashMarkerCount
        }
        if (Test-LineContainsAny -Line $line -Needles $heapNeedles) {
            ++$heapMarkerCount
        }
        if ($line.IndexOf("AndroidAudioDriver opened AAudio output", [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            ++$aaudioOpenCount
        }
        if (Test-LineContainsAny -Line $line -Needles $inputNeedles) {
            $timestamp = Try-ParseLogcatTimestamp -Line $line -ReferenceTime $packetTime
            $inputRows.Add([pscustomobject][ordered]@{
                    Timestamp = $timestamp
                    Line = $line
                })
        }
    }
} finally {
    $reader.Dispose()
}

if ($firstVdSwapLine) {
    $firstVdSwapTime = Try-ParseLogcatTimestamp -Line $firstVdSwapLine -ReferenceTime $packetTime
}
if ($lastVdSwapLine) {
    $lastVdSwapTime = Try-ParseLogcatTimestamp -Line $lastVdSwapLine -ReferenceTime $packetTime
}

$secondsSinceLastVdSwap = ""
$staleSwap = $false
if ($packetTime -and $lastVdSwapTime) {
    $seconds = ($packetTime - $lastVdSwapTime).TotalSeconds
    if ($seconds -ge 0) {
        $secondsSinceLastVdSwap = $seconds.ToString("0.000", [Globalization.CultureInfo]::InvariantCulture)
        $staleSwap = $seconds -ge ([Math]::Max(1, $FreshStallMinutes) * 60)
    }
}

$blackFrameLikely = $false
if ($screenStats) {
    $blackFrameLikely =
        $screenStats.NearBlackShare -ge $BlackShareThreshold -and
        $screenStats.AverageLuma -le ($BlackLumaThreshold / 2.0)
}

$inputAfterLastSwap = @()
if ($lastVdSwapTime) {
    $inputAfterLastSwap = @($inputRows | Where-Object { $_.Timestamp -and $_.Timestamp -gt $lastVdSwapTime })
}
$lastInputAfterSwap = $inputAfterLastSwap | Select-Object -Last 1

$classification = "burnout_no_present_audit_inconclusive"
$reason = "packet did not match a decisive no-present shape"
$decision = "collect_focused_route_with_thread_snapshots_and_wait_trace"

if ($TargetTitleId -and $titleId -and $titleId -ne $TargetTitleId.ToUpperInvariant()) {
    $classification = "not_target_title"
    $reason = "packet title_id does not match target title"
    $decision = "use_title_specific_audit_or_choose_correct_packet"
} elseif ($crashMarkerCount -gt 0) {
    $classification = "burnout_crash_marker_present"
    $reason = "fatal, AndroidRuntime, native signal, or guest crash marker is present"
    $decision = "triage_crash_marker_before_no_present_lane"
} elseif ($vdswapCount -eq 0) {
    $classification = "burnout_no_vdswap_evidence"
    $reason = "no runtime VdSwap rows were found"
    $decision = "capture_from_launch_with_vdswap_or_present_logging"
} elseif ($staleSwap -and $blackFrameLikely) {
    $classification = "burnout_live_black_no_present_stall_no_scoped_crash"
    $reason = "guest swaps stopped long before the packet screenshot, screen is near-black, and no scoped crash markers were found"
    $decision = "capture_thread_snapshots_wait_trace_and_transition_window"
} elseif ($staleSwap) {
    $classification = "burnout_no_present_stall_no_scoped_crash"
    $reason = "guest swaps stopped long before the packet timestamp and no scoped crash markers were found"
    $decision = "capture_thread_snapshots_wait_trace_and_transition_window"
} else {
    $classification = "burnout_presenting_or_recent_guest_swaps"
    $reason = "runtime VdSwap rows are recent enough for the packet timestamp or packet lacks timestamp"
    $decision = "inspect_visual_rendering_or_frontend_progress"
}

$report = New-Object System.Collections.Generic.List[string]
$report.Add(("classification={0}" -f $classification))
$report.Add(("reason={0}" -f $reason))
$report.Add(("decision={0}" -f $decision))
$report.Add(("packet_dir={0}" -f $PacketDir))
$report.Add(("log_path={0}" -f $resolvedLogPath))
$report.Add(("screen_path={0}" -f $resolvedScreenPath))
$report.Add(("meta_path={0}" -f $resolvedMetaPath))
$report.Add(("packet_created_at={0}" -f $(if ($packetTime) { $packetTime.ToString("yyyy-MM-ddTHH:mm:ss.fffK", [Globalization.CultureInfo]::InvariantCulture) } else { "" })))
$report.Add(("target_title_id={0}" -f $TargetTitleId.ToUpperInvariant()))
$report.Add(("title_id={0}" -f $titleId))
$report.Add(("media_id={0}" -f $mediaId))
$report.Add(("original_pe={0}" -f $originalPe))
$report.Add(("line_count={0}" -f $lineCount))
$report.Add(("launch_module_count={0}" -f $launchModuleCount))
$report.Add(("vdswap_count={0}" -f $vdswapCount))
$report.Add(("first_vdswap_time={0}" -f (Format-TimeOnly $firstVdSwapTime)))
$report.Add(("last_vdswap_time={0}" -f (Format-TimeOnly $lastVdSwapTime)))
$report.Add(("seconds_since_last_vdswap={0}" -f $secondsSinceLastVdSwap))
$report.Add(("fresh_stall_minutes={0}" -f $FreshStallMinutes))
$report.Add(("stale_swap={0}" -f [int]$staleSwap))
$report.Add(("crash_marker_count={0}" -f $crashMarkerCount))
$report.Add(("heap_marker_count={0}" -f $heapMarkerCount))
$report.Add(("aaudio_open_count={0}" -f $aaudioOpenCount))
$report.Add(("input_down_count={0}" -f $inputRows.Count))
$report.Add(("input_after_last_vdswap_count={0}" -f $inputAfterLastSwap.Count))
$report.Add(("last_input_after_last_vdswap_time={0}" -f $(if ($lastInputAfterSwap) { Format-TimeOnly $lastInputAfterSwap.Timestamp } else { "" })))
$report.Add(("last_input_after_last_vdswap_line={0}" -f $(if ($lastInputAfterSwap) { $lastInputAfterSwap.Line } else { "" })))
if ($screenStats) {
    $report.Add(("screen_width={0}" -f $screenStats.Width))
    $report.Add(("screen_height={0}" -f $screenStats.Height))
    $report.Add(("sample_step={0}" -f [Math]::Max(1, $Step)))
    $report.Add(("ignored_top_left={0},{1}" -f $IgnoreTopLeftWidth, $IgnoreTopLeftHeight))
    $report.Add(("near_black_share={0}" -f $screenStats.NearBlackShare.ToString("0.000000", [Globalization.CultureInfo]::InvariantCulture)))
    $report.Add(("average_luma={0}" -f $screenStats.AverageLuma.ToString("0.000", [Globalization.CultureInfo]::InvariantCulture)))
    $report.Add(("non_black_bbox={0}" -f $screenStats.NonBlackBbox))
    $report.Add(("black_frame_likely={0}" -f [int]$blackFrameLikely))
} else {
    $report.Add("black_frame_likely=")
}
$report.Add(("last_vdswap_line={0}" -f $lastVdSwapLine))

if ($OutPath) {
    $parent = Split-Path -Parent $OutPath
    if ($parent -and !(Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    Set-Content -LiteralPath $OutPath -Value $report -Encoding ASCII
}

$report
