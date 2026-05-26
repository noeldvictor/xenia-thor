param(
    [string]$A64VectorPath = "src\xenia\cpu\backend\a64\a64_seq_vector.cc",
    [string]$X64VectorPath = "src\xenia\cpu\backend\x64\x64_seq_vector.cc",
    [string]$TestingRoot = "src\xenia\cpu\testing",
    [string]$ResetDocPath = "docs\research\20260525-203000-continual-harness-plan-reset.md",
    [string]$RerankDocPath = "docs\research\20260525-203900-a64-mature-gap-reset-rerank.md",
    [string]$WorklogPath = "docs\worklogs\20260525.md"
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path

function Resolve-RepoPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return (Join-Path $RepoRoot $Path)
}

function Read-RequiredText {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        throw "Path not found: $Path"
    }
    return Get-Content -Raw -LiteralPath $resolved
}

function Read-RequiredLines {
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
            return ($i + 1)
        }
    }
    return 0
}

function Count-Pattern {
    param(
        [string]$Text,
        [string]$Pattern
    )

    return [regex]::Matches(
        $Text,
        $Pattern,
        [System.Text.RegularExpressions.RegexOptions]::Singleline).Count
}

function Test-Pattern {
    param(
        [string]$Text,
        [string]$Pattern
    )

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return $false
    }
    return [regex]::IsMatch(
        $Text,
        $Pattern,
        [System.Text.RegularExpressions.RegexOptions]::Singleline)
}

function Format-Source {
    param(
        [string]$Path,
        [int]$Line
    )

    if ($Line -gt 0) {
        return ("{0}:{1}" -f $Path, $Line)
    }
    return $Path
}

function Get-TestText {
    param([string]$Root)

    $resolved = Resolve-RepoPath $Root
    if (!(Test-Path -LiteralPath $resolved)) {
        throw "TestingRoot not found: $Root"
    }
    $chunks = New-Object System.Collections.Generic.List[string]
    Get-ChildItem -LiteralPath $resolved -Recurse -File -Include *.cc,*.h |
        Sort-Object FullName |
        ForEach-Object {
            $chunks.Add((Get-Content -Raw -LiteralPath $_.FullName)) | Out-Null
        }
    return ($chunks -join "`n")
}

function Count-ActiveTests {
    param(
        [string]$Text,
        [string]$KeywordPattern
    )

    $count = 0
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^\s*TEST_CASE\("' -and $line -match $KeywordPattern) {
            $count += 1
        }
    }
    return $count
}

function Count-CommentedTests {
    param(
        [string]$Text,
        [string]$KeywordPattern
    )

    $count = 0
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^\s*//\s*TEST_CASE\("' -and $line -match $KeywordPattern) {
            $count += 1
        }
    }
    return $count
}

function Emit-Family {
    param(
        [string]$Name,
        [string]$Status,
        [string]$Evidence,
        [string]$Coverage,
        [string]$Source,
        [string]$Next
    )

    Write-Output ("family={0} status={1} evidence={2} coverage={3} source={4} next={5}" -f
        $Name, $Status, $Evidence, $Coverage, $Source, $Next)
}

$a64Vector = Read-RequiredText $A64VectorPath
$a64VectorLines = Read-RequiredLines $A64VectorPath
$x64Vector = Read-RequiredText $X64VectorPath
$testText = Get-TestText $TestingRoot
$resetDoc = Read-RequiredText $ResetDocPath
$rerankDoc = Read-RequiredText $RerankDocPath
$worklog = Read-RequiredText $WorklogPath
$routeEvidence = $resetDoc + "`n" + $rerankDoc + "`n" + $worklog

$tblSites = Count-Pattern $a64Vector '\.tbl\('
$loadConstSites = Count-Pattern $a64Vector 'LoadV128Const\('
$movX0Sites = Count-Pattern $a64Vector 'e\.mov\(e\.x0'
$zipFastpath = Test-Pattern $a64Vector 'arm64_permute_i32_zip_fastpath'
$stvewxClosed = (
    (Test-Pattern $routeEvidence 'Do not.*stvewx') -or
    (Test-Pattern $routeEvidence 'stvewx.*closed'))
$mulAddClosed = Test-Pattern $routeEvidence 'three-PC MUL_ADD_V128'
$edgeClosed = (
    (Test-Pattern $routeEvidence 'segments_survived_no_kill=0') -and
    (Test-Pattern $routeEvidence 'f1_reads_before_kill=0'))
$cpuWall = (
    (Test-Pattern $routeEvidence 'CPU/JIT') -or
    (Test-Pattern $routeEvidence 'A64 generated-code'))

$permuteLine = Find-Line $a64VectorLines 'struct PERMUTE_V128'
$loadShlLine = Find-Line $a64VectorLines 'struct LOAD_VECTOR_SHL_I8'
$loadShrLine = Find-Line $a64VectorLines 'struct LOAD_VECTOR_SHR_I8'
$extractLine = Find-Line $a64VectorLines 'struct EXTRACT_I32'
$splatLine = Find-Line $a64VectorLines 'struct SPLAT_I32'
$packLine = Find-Line $a64VectorLines 'struct PACK'
$unpackLine = Find-Line $a64VectorLines 'struct UNPACK'
$lvlLine = Find-Line $a64VectorLines 'struct LVL_V128'
$lvrLine = Find-Line $a64VectorLines 'struct LVR_V128'

$permuteTests = Count-ActiveTests $testText 'PERMUTE'
$loadVectorTests = Count-ActiveTests $testText 'LOAD_VECTOR'
$extractTests = Count-ActiveTests $testText 'EXTRACT'
$splatTests = Count-ActiveTests $testText 'SPLAT'
$packTests = Count-ActiveTests $testText '"PACK_'
$unpackTests = Count-ActiveTests $testText '"UNPACK_'
$commentedUnpackTests = Count-CommentedTests $testText 'UNPACK_'
$shiftTests = Count-ActiveTests $testText 'VECTOR_SH(L|R)'
$compareTests = Count-ActiveTests $testText 'VECTOR_COMPARE'

$expectedPackModes = @(
    "D3DCOLOR",
    "FLOAT16_2",
    "FLOAT16_4",
    "SHORT_2",
    "SHORT_4",
    "UINT_2101010",
    "ULONG_4202020",
    "8_IN_16",
    "16_IN_32"
)

$packCoverage = New-Object System.Collections.Generic.List[string]
$unpackCoverage = New-Object System.Collections.Generic.List[string]
$packMissing = New-Object System.Collections.Generic.List[string]
$unpackMissing = New-Object System.Collections.Generic.List[string]
foreach ($mode in $expectedPackModes) {
    $packModeTests = Count-ActiveTests $testText ('"PACK_' + [regex]::Escape($mode))
    $unpackModeTests = Count-ActiveTests $testText ('"UNPACK_' + [regex]::Escape($mode))
    $packCoverage.Add(("{0}:{1}" -f $mode, $packModeTests)) | Out-Null
    $unpackCoverage.Add(("{0}:{1}" -f $mode, $unpackModeTests)) | Out-Null
    if ($packModeTests -eq 0) {
        $packMissing.Add($mode) | Out-Null
    }
    if ($unpackModeTests -eq 0) {
        $unpackMissing.Add($mode) | Out-Null
    }
}

$x64PackLineCount = Count-Pattern $x64Vector 'struct PACK'
$x64UnpackLineCount = Count-Pattern $x64Vector 'struct UNPACK'
$packMissingText = if ($packMissing.Count -gt 0) { $packMissing -join "," } else { "none" }
$unpackMissingText = if ($unpackMissing.Count -gt 0) { $unpackMissing -join "," } else { "none" }
$packUnpackCoverageComplete = ($packMissing.Count -eq 0) -and ($unpackMissing.Count -eq 0)
$packUnpackStatus = if ($packUnpackCoverageComplete) {
    "coverage_complete_needs_route_volume"
} else {
    "broad_candidate_but_test_coverage_first"
}
$packUnpackNext = if ($packUnpackCoverageComplete) {
    "PACK/UNPACK unit coverage is complete; run fresh route-volume counters before any behavior work."
} else {
    "Before optimizing PACK/UNPACK, close missing test modes pack=[$packMissingText] unpack=[$unpackMissingText]; then audit route volume."
}
$packUnpackRankedNext = if ($packUnpackCoverageComplete) {
    "1. pack_unpack_route_volume: unit coverage is complete for $($expectedPackModes.Count) of $($expectedPackModes.Count) pack modes and $($expectedPackModes.Count) of $($expectedPackModes.Count) unpack modes; next proof is route dynamic counts before behavior work."
} else {
    "1. pack_unpack_test_coverage: widest semantic surface, current active tests cover $packTests of $($expectedPackModes.Count) pack modes and $unpackTests of $($expectedPackModes.Count) unpack modes; remaining pack=[$packMissingText] unpack=[$unpackMissingText]."
}

Write-Output "# A64 VMX128/NEON Opcode-Family Audit"
Write-Output ("repo={0}" -f $RepoRoot)
Write-Output ("a64_vector={0}" -f $A64VectorPath)
Write-Output ("x64_vector={0}" -f $X64VectorPath)
Write-Output ("testing_root={0}" -f $TestingRoot)
Write-Output ("reset_doc={0}" -f $ResetDocPath)
Write-Output ("rerank_doc={0}" -f $RerankDocPath)
Write-Output ("cpu_wall_current={0}" -f $cpuWall)
Write-Output ("edge_payload_closed={0}" -f $edgeClosed)
Write-Output ("stvewx_closed={0}" -f $stvewxClosed)
Write-Output ("mul_add_three_pc_closed={0}" -f $mulAddClosed)
Write-Output ("a64_tbl_sites={0}" -f $tblSites)
Write-Output ("a64_load_v128_const_sites={0}" -f $loadConstSites)
Write-Output ("a64_vector_mov_x0_sites={0}" -f $movX0Sites)
Write-Output ""

Emit-Family `
    -Name "permute" `
    -Status "hot_broad_candidate_needs_dynamic_count" `
    -Evidence "tbl_sites=$tblSites; i32_zip_fastpath=$zipFastpath; int8/int16/v128 lower through tbl/control-vector work" `
    -Coverage "active_tests=$permuteTests" `
    -Source (Format-Source $A64VectorPath $permuteLine) `
    -Next "Count route dynamic PERMUTE by function/block, then add broad constant-control or repeated-control fastpaths only if volume is outside closed stvewx/MUL_ADD lanes."

Emit-Family `
    -Name "load_vector_shift" `
    -Status "candidate_with_tests_needs_route_volume" `
    -Evidence "load_vector_shl_line=$loadShlLine; load_vector_shr_line=$loadShrLine; current lowering rebuilds base control vectors with scalar mov/fmov/ins per op" `
    -Coverage "active_tests=$loadVectorTests" `
    -Source (Format-Source $A64VectorPath $loadShlLine) `
    -Next "Use HIR vector-shape reports on current hot functions before patching; candidate is shared constant/control materialization, not a Blue-Dragon-only PC fold."

Emit-Family `
    -Name "extract_splat" `
    -Status "extract_hot_but_splat_test_gap" `
    -Evidence "extract_i32_line=$extractLine; splat_i32_line=$splatLine; dynamic extract builds scalar TBL controls; stvewx lane-fold is closed" `
    -Coverage "extract_tests=$extractTests; splat_tests=$splatTests" `
    -Source (Format-Source $A64VectorPath $extractLine) `
    -Next "Do not reopen exact stvewx. Add SPLAT unit coverage or find non-stvewx dynamic EXTRACT/SPLAT route volume before behavior work."

Emit-Family `
    -Name "pack_unpack" `
    -Status $packUnpackStatus `
    -Evidence "pack_line=$packLine; unpack_line=$unpackLine; x64_pack_structs=$x64PackLineCount; x64_unpack_structs=$x64UnpackLineCount; A64 has scalar-heavy 20-bit/half paths and NEON pack/unpack paths" `
    -Coverage ("pack_tests={0}/{1} [{2}]; unpack_tests={3}/{1} [{4}]; commented_unpack_tests={5}" -f
        $packTests, $expectedPackModes.Count, ($packCoverage -join ","),
        $unpackTests, ($unpackCoverage -join ","), $commentedUnpackTests) `
    -Source (Format-Source $A64VectorPath $packLine) `
    -Next $packUnpackNext

Emit-Family `
    -Name "lvl_lvr_vector_loads" `
    -Status "candidate_after_route_shape_count" `
    -Evidence "lvl_line=$lvlLine; lvr_line=$lvrLine; vector load-left/right use TBL and address-offset control; previous 822824F0 local-only peephole is closed" `
    -Coverage "load_vector_tests=$loadVectorTests" `
    -Source (Format-Source $A64VectorPath $lvlLine) `
    -Next "Only pursue as an opcode-family lowering if hot-route HIR shows many LVL/LVR or load-vector-shift joins outside the closed local 822824F0 peephole."

Emit-Family `
    -Name "shift_compare_minmax" `
    -Status "covered_lower_priority" `
    -Evidence "vector_shift_tests=$shiftTests; compare_tests=$compareTests; current reset ranking favors permute/load-shift/extract/pack first" `
    -Coverage "shift_tests=$shiftTests; compare_tests=$compareTests" `
    -Source $A64VectorPath `
    -Next "Keep as fallback broad NEON work after higher-priority route volume is measured."

Write-Output ""
Write-Output "ranked_vmx128_neon_next:"
Write-Output $packUnpackRankedNext
Write-Output "2. permute_route_volume_audit: TBL-heavy and broad; requires current route dynamic counts before adding more fastpaths."
Write-Output "3. load_vector_shift_control_materialization: current lowering rebuilds constants per op; route volume must be proven before codegen work."
Write-Output "4. extract_splat_non_stvewx_volume: dynamic extract is costly, but exact stvewx lane-fold is closed and SPLAT has no active unit tests."
Write-Output "5. lvl_lvr_family: possible broad load-left/right target only if fresh HIR shows volume outside closed local peepholes."
Write-Output ""
Write-Output "decision=do_not_patch_vmx128_behavior_next_without fresh route-volume counters proving broad non-closed usage; complete unit coverage alone is not speed evidence."
