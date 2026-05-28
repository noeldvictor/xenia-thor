param(
    [string]$PacketDir = "",
    [string]$ScreenPath = "",
    [string]$LogPath = "",
    [string]$StatusPath = "",
    [string]$OutPath = "",
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
        throw "Pass -$DefaultName path or -PacketDir containing $DefaultName."
    }
    return ""
}

function Count-Matches {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )
    return @($Lines | Select-String -Pattern $Pattern).Count
}

function First-Index {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )
    for ($i = 0; $i -lt $Lines.Count; ++$i) {
        if ($Lines[$i] -match $Pattern) {
            return $i
        }
    }
    return -1
}

function Count-AfterIndex {
    param(
        [string[]]$Lines,
        [int]$Index,
        [string]$Pattern
    )
    if ($Index -lt 0 -or $Index -ge $Lines.Count) {
        return 0
    }
    $count = 0
    for ($i = $Index + 1; $i -lt $Lines.Count; ++$i) {
        if ($Lines[$i] -match $Pattern) {
            ++$count
        }
    }
    return $count
}

function Read-KeyValueReport {
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
        $key = $line.Substring(0, $split)
        $value = $line.Substring($split + 1)
        $values[$key] = $value
    }
    return $values
}

function Measure-ScreenBlackness {
    param([string]$Path)

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

$screen = Resolve-PacketPath $ScreenPath "screen.png" -Required
$log = Resolve-PacketPath $LogPath "logcat.txt"
$status = Resolve-PacketPath $StatusPath "status-report.txt"
if ($PacketDir) {
    $PacketDir = (Resolve-Path -LiteralPath $PacketDir).Path
}

$screenStats = Measure-ScreenBlackness $screen
$logLines = @()
if ($log) {
    $logLines = Get-Content -LiteralPath $log
}
$statusValues = Read-KeyValueReport $status

$firstLongjmpIndex = First-Index $logLines "longjmp_reenter\s+1"
$vdSwapCount = Count-Matches $logLines "VdSwap\("
$vdSwapAfterLongjmp = Count-AfterIndex $logLines $firstLongjmpIndex "VdSwap\("
$heapReleaseCount = Count-Matches $logLines "BaseHeap::Release failed|PhysicalHeap::Release failed|MmFreePhysicalMemory failed"
$heapReleaseAfterLongjmp = Count-AfterIndex $logLines $firstLongjmpIndex "BaseHeap::Release failed|PhysicalHeap::Release failed|MmFreePhysicalMemory failed"
$longjmpReenterCount = Count-Matches $logLines "longjmp_reenter\s+1"
$throwsExceptionCount = Count-Matches $logLines "throws_exception\s+1"
$nativeAbortCount = Count-Matches $logLines "Abort message:|Fatal signal|SIGABRT|AndroidRuntime|FATAL EXCEPTION"
$nopAudioCount = Count-Matches $logLines "NopAudioSystem created silent audio driver"

$blackFrameLikely =
    $screenStats.NearBlackShare -ge $BlackShareThreshold -and
    $screenStats.AverageLuma -le ($BlackLumaThreshold / 2.0)

$classification = "visual_not_black_frame"
$reason = "screen has visible non-black content"
if ($blackFrameLikely) {
    $classification = "black_frame_likely"
    $reason = "screen is almost entirely near-black after ignoring the FPS OSD corner"
}
if ($blackFrameLikely -and $longjmpReenterCount -gt 0 -and $nativeAbortCount -eq 0) {
    $classification = "project_sylpheed_black_frame_after_reenter_no_crash"
    $reason = "black screen after Android reenter longjmp with no native abort marker"
}
if ($classification -eq "project_sylpheed_black_frame_after_reenter_no_crash" -and
        $vdSwapAfterLongjmp -gt 0) {
    $classification = "project_sylpheed_live_black_frame_after_reenter"
    $reason = "black screen after Android reenter longjmp with continued VdSwap activity and no native abort marker"
}

$statusClassification = ""
if ($statusValues.ContainsKey("classification")) {
    $statusClassification = $statusValues["classification"]
}

$decision = "inspect_visual_issue"
if ($classification -eq "project_sylpheed_black_frame_after_reenter_no_crash") {
    $decision = "classify_heap_release_and_black_frame_next"
}
if ($classification -eq "project_sylpheed_live_black_frame_after_reenter") {
    $decision = "join_vd_swap_present_and_heap_release_next"
}
if ($classification -eq "visual_not_black_frame") {
    $decision = "packet_is_visual_control_or_non_black_screen"
}

$report = @(
    "classification=$classification",
    "reason=$reason",
    "decision=$decision",
    "packet_dir=$PacketDir",
    "screen_path=$screen",
    "log_path=$log",
    "status_path=$status",
    "status_classification=$statusClassification",
    "screen_width=$($screenStats.Width)",
    "screen_height=$($screenStats.Height)",
    "sample_step=$([Math]::Max(1, $Step))",
    "ignored_top_left=$IgnoreTopLeftWidth,$IgnoreTopLeftHeight",
    "near_black_pixels=$($screenStats.NearBlackPixels)",
    "sample_count=$($screenStats.Samples)",
    "near_black_share=$($screenStats.NearBlackShare.ToString('0.000000'))",
    "average_luma=$($screenStats.AverageLuma.ToString('0.000'))",
    "non_black_bbox=$($screenStats.NonBlackBbox)",
    "black_frame_likely=$([int]$blackFrameLikely)",
    "longjmp_reenter_count=$longjmpReenterCount",
    "throws_exception_count=$throwsExceptionCount",
    "vd_swap_count=$vdSwapCount",
    "vd_swap_after_longjmp=$vdSwapAfterLongjmp",
    "heap_release_count=$heapReleaseCount",
    "heap_release_after_longjmp=$heapReleaseAfterLongjmp",
    "native_abort_marker_count=$nativeAbortCount",
    "nop_audio_count=$nopAudioCount"
)

if ($OutPath) {
    $parent = Split-Path -Parent $OutPath
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $report | Out-File -Encoding utf8 -FilePath $OutPath
}

$report
