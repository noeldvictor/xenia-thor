param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,
    [Parameter(Mandatory = $true)]
    [string]$Function,
    [ValidateSet("RawHIR", "OptHIR")]
    [string]$Phase = "OptHIR",
    [string]$BlockProfileLog = "",
    [int]$Top = 20
)

$ErrorActionPreference = "Stop"

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

function Get-TopPairs {
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

function New-BlockRow {
    param(
        [int]$Index,
        [string]$Name
    )
    $guest = "00000000"
    if ($Name -match "^loc_([0-9A-Fa-f]+)") {
        $guest = $Matches[1].ToUpperInvariant()
    }
    return [pscustomobject][ordered]@{
        index = $Index
        name = $Name
        guest = $guest
        instructions = 0
        ppc = @{}
        extract = 0
        extract_dynamic = 0
        extract_constant = 0
        splat = 0
        extract_then_splat = 0
        stvewx = 0
        stvewx_store1 = 0
        stvewx_dynamic_extract = 0
        stvewx_masked_address = 0
        store1 = 0
        mul_add = 0
        permute = 0
        load_vector_shl = 0
        load_vector_shr = 0
        lvx128 = 0
        lvlx = 0
        lvrx = 0
        vspltw = 0
        vmaddfp = 0
        profile_total = 0
        profile_peak_delta = 0
        stvewx_pcs = @{}
        vspltw_pcs = @{}
    }
}

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}

$resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
$functionPattern = [Regex]::Escape($Function.ToUpperInvariant())
$phasePattern = [Regex]::Escape($Phase)
$linePattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"

$blocks = New-Object System.Collections.Generic.List[object]
$current = $null
$currentPpcOp = ""
$currentPpcPc = ""
$stvewxActive = $false
$stvewxSawMask = $false
$stvewxSawDynamicExtract = $false
$lastInstructionWasExtract = $false

Get-Content -LiteralPath $resolvedLog | ForEach-Object {
    if ($_ -notmatch $linePattern) {
        return
    }

    $text = $Matches.text
    if ($text -match "^\s*<entry>:") {
        return
    }
    if ($text -match "^\s*(loc_[0-9A-Fa-f]+):") {
        $current = New-BlockRow $blocks.Count $Matches[1]
        $blocks.Add($current) | Out-Null
        $currentPpcOp = ""
        $currentPpcPc = ""
        $stvewxActive = $false
        $stvewxSawMask = $false
        $stvewxSawDynamicExtract = $false
        $lastInstructionWasExtract = $false
        return
    }
    if ($null -eq $current) {
        return
    }

    if ($text -match "^\s*;\s+([0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+([A-Za-z0-9_\.]+)") {
        $currentPpcPc = $Matches[1].ToUpperInvariant()
        $currentPpcOp = $Matches[2].ToLowerInvariant()
        Add-Count $current.ppc $currentPpcOp
        $stvewxActive = ($currentPpcOp -eq "stvewx")
        $stvewxSawMask = $false
        $stvewxSawDynamicExtract = $false
        $lastInstructionWasExtract = $false

        switch ($currentPpcOp) {
            "stvewx" {
                $current.stvewx += 1
                Add-Count $current.stvewx_pcs $currentPpcPc
            }
            "vspltw" {
                $current.vspltw += 1
                Add-Count $current.vspltw_pcs $currentPpcPc
            }
            "vmaddfp" { $current.vmaddfp += 1 }
            "lvx128" { $current.lvx128 += 1 }
            "lvlx" { $current.lvlx += 1 }
            "lvrx" { $current.lvrx += 1 }
        }
        return
    }
    if ($text -match "^\s*(;|$)") {
        return
    }

    $current.instructions += 1

    if ($text -match "\bload_vector_shl\b") {
        $current.load_vector_shl += 1
    }
    if ($text -match "\bload_vector_shr\b") {
        $current.load_vector_shr += 1
    }
    if ($text -match "\bpermute\b") {
        $current.permute += 1
    }
    if ($text -match "\bmul_add\b") {
        $current.mul_add += 1
    }
    if ($text -match "\bstore\.1\b") {
        $current.store1 += 1
        if ($stvewxActive) {
            $current.stvewx_store1 += 1
        }
    }
    if ($text -match "=\s+and\s+v[0-9]+\.i64<[^>]+>,\s+-4\b") {
        if ($stvewxActive) {
            $stvewxSawMask = $true
            $current.stvewx_masked_address += 1
        }
    }

    $isExtract = $false
    if ($text -match "\bextract\b") {
        $isExtract = $true
        $current.extract += 1
        if ($text -match "\bextract\s+[^,]+,\s+v[0-9]+\.(i8|i16|i32|i64)") {
            $current.extract_dynamic += 1
            if ($stvewxActive) {
                $stvewxSawDynamicExtract = $true
                $current.stvewx_dynamic_extract += 1
            }
        } else {
            $current.extract_constant += 1
        }
    }

    if ($text -match "\bsplat\b") {
        $current.splat += 1
        if ($lastInstructionWasExtract) {
            $current.extract_then_splat += 1
        }
    }

    if ($stvewxActive -and $text -match "\bstore\.1\b") {
        if (!$stvewxSawMask -or !$stvewxSawDynamicExtract) {
            Add-Count $current.stvewx_pcs ("partial:" + $currentPpcPc)
        }
    }

    $lastInstructionWasExtract = $isExtract
}

if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    if (!(Test-Path -LiteralPath $BlockProfileLog)) {
        throw "BlockProfileLog not found: $BlockProfileLog"
    }
    $resolvedProfile = (Resolve-Path -LiteralPath $BlockProfileLog).Path
    $profilePattern = "A64 speed profile block top \d+: fn $functionPattern .* block=(?<block>\d+) guest=(?<guest>[0-9A-Fa-f]{8}) delta=(?<delta>\d+) total=(?<total>\d+)"
    Get-Content -LiteralPath $resolvedProfile | ForEach-Object {
        if ($_ -notmatch $profilePattern) {
            return
        }
        $guest = $Matches.guest.ToUpperInvariant()
        $row = $null
        if ($guest -ne "00000000") {
            $row = $blocks | Where-Object { $_.guest -eq $guest } | Select-Object -First 1
        }
        if ($null -eq $row) {
            $index = [int]$Matches.block
            if ($index -lt 0 -or $index -ge $blocks.Count) {
                return
            }
            $row = $blocks[$index]
        }
        $total = [int64]$Matches.total
        $delta = [int64]$Matches.delta
        if ($total -gt $row.profile_total) {
            $row.profile_total = $total
        }
        if ($delta -gt $row.profile_peak_delta) {
            $row.profile_peak_delta = $delta
        }
        if ($row.guest -eq "00000000" -and $guest -ne "00000000") {
            $row.guest = $guest
        }
    }
}

$totalInstructions = ($blocks | Measure-Object -Property instructions -Sum).Sum
$totalExtract = ($blocks | Measure-Object -Property extract -Sum).Sum
$totalExtractDynamic = ($blocks | Measure-Object -Property extract_dynamic -Sum).Sum
$totalExtractConstant = ($blocks | Measure-Object -Property extract_constant -Sum).Sum
$totalSplat = ($blocks | Measure-Object -Property splat -Sum).Sum
$totalExtractThenSplat = ($blocks | Measure-Object -Property extract_then_splat -Sum).Sum
$totalStvewx = ($blocks | Measure-Object -Property stvewx -Sum).Sum
$totalStvewxStore1 = ($blocks | Measure-Object -Property stvewx_store1 -Sum).Sum
$totalStvewxDynamicExtract = ($blocks | Measure-Object -Property stvewx_dynamic_extract -Sum).Sum
$totalMulAdd = ($blocks | Measure-Object -Property mul_add -Sum).Sum
$totalPermute = ($blocks | Measure-Object -Property permute -Sum).Sum
$totalLoadVectorShl = ($blocks | Measure-Object -Property load_vector_shl -Sum).Sum
$totalLoadVectorShr = ($blocks | Measure-Object -Property load_vector_shr -Sum).Sum

Write-Output "# HIR Vector Shape Report"
Write-Output ""
Write-Output "log=$resolvedLog"
if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    Write-Output "block_profile_log=$((Resolve-Path -LiteralPath $BlockProfileLog).Path)"
}
Write-Output "function=$($Function.ToUpperInvariant())"
Write-Output "phase=$Phase"
Write-Output "blocks=$($blocks.Count)"
Write-Output "instructions=$totalInstructions"
Write-Output "extract=$totalExtract"
Write-Output "extract_dynamic=$totalExtractDynamic"
Write-Output "extract_constant=$totalExtractConstant"
Write-Output "splat=$totalSplat"
Write-Output "extract_then_splat=$totalExtractThenSplat"
Write-Output "stvewx=$totalStvewx"
Write-Output "stvewx_store1=$totalStvewxStore1"
Write-Output "stvewx_dynamic_extract=$totalStvewxDynamicExtract"
Write-Output "mul_add=$totalMulAdd"
Write-Output "permute=$totalPermute"
Write-Output "load_vector_shl=$totalLoadVectorShl"
Write-Output "load_vector_shr=$totalLoadVectorShr"

Write-Output ""
Write-Output "## Dynamic Hot Vector Blocks"
$dynamicRows = $blocks | Where-Object { $_.profile_total -gt 0 } |
    Sort-Object -Property @{ Expression = "profile_total"; Descending = $true },
                          @{ Expression = "index"; Ascending = $true } |
    Select-Object -First $Top
if (!$dynamicRows) {
    Write-Output "(no block profile data supplied)"
} else {
    $dynamicRows | ForEach-Object {
        Write-Output ("block={0} guest={1} total={2} peak_delta={3} instr={4} stvewx={5}/{6}/{7} extract={8}/{9}/{10} splat={11} x2s={12} mul_add={13} perm={14} lv_shl={15} lv_shr={16} ppc={17}" -f
            $_.index, $_.guest, $_.profile_total, $_.profile_peak_delta,
            $_.instructions, $_.stvewx, $_.stvewx_store1,
            $_.stvewx_dynamic_extract, $_.extract, $_.extract_dynamic,
            $_.extract_constant, $_.splat, $_.extract_then_splat,
            $_.mul_add, $_.permute, $_.load_vector_shl,
            $_.load_vector_shr, (Get-TopPairs $_.ppc 8))
    }
}

Write-Output ""
Write-Output "## Stvewx Lowering Blocks"
$stvewxRows = $blocks | Where-Object { $_.stvewx -gt 0 } |
    Sort-Object -Property @{ Expression = "profile_total"; Descending = $true },
                          @{ Expression = "stvewx"; Descending = $true },
                          @{ Expression = "index"; Ascending = $true } |
    Select-Object -First $Top
if (!$stvewxRows) {
    Write-Output "(no stvewx rows)"
} else {
    $stvewxRows | ForEach-Object {
        Write-Output ("block={0} guest={1} total={2} peak_delta={3} stvewx={4} store1={5} dynamic_extract={6} masked_addr={7} pcs={8} ppc={9}" -f
            $_.index, $_.guest, $_.profile_total, $_.profile_peak_delta,
            $_.stvewx, $_.stvewx_store1, $_.stvewx_dynamic_extract,
            $_.stvewx_masked_address, (Get-TopPairs $_.stvewx_pcs 8),
            (Get-TopPairs $_.ppc 8))
    }
}

Write-Output ""
Write-Output "## Extract/Splat Blocks"
$extractRows = $blocks | Where-Object { ($_.extract + $_.splat) -gt 0 } |
    Sort-Object -Property @{ Expression = { $_.profile_total }; Descending = $true },
                          @{ Expression = { $_.extract + $_.splat }; Descending = $true },
                          @{ Expression = "index"; Ascending = $true } |
    Select-Object -First $Top
if (!$extractRows) {
    Write-Output "(no extract/splat rows)"
} else {
    $extractRows | ForEach-Object {
        Write-Output ("block={0} guest={1} total={2} peak_delta={3} extract={4}/{5}/{6} splat={7} x2s={8} vspltw={9} pcs={10} ppc={11}" -f
            $_.index, $_.guest, $_.profile_total, $_.profile_peak_delta,
            $_.extract, $_.extract_dynamic, $_.extract_constant, $_.splat,
            $_.extract_then_splat, $_.vspltw, (Get-TopPairs $_.vspltw_pcs 8),
            (Get-TopPairs $_.ppc 8))
    }
}

Write-Output ""
Write-Output "## Static Vector Shape Blocks"
$blocks |
    Sort-Object -Property @{ Expression = { $_.stvewx + $_.extract + $_.splat + $_.mul_add + $_.permute + $_.load_vector_shl + $_.load_vector_shr }; Descending = $true },
                          @{ Expression = "index"; Ascending = $true } |
    Select-Object -First $Top |
    ForEach-Object {
        Write-Output ("block={0} guest={1} instr={2} stvewx={3}/{4}/{5} extract={6}/{7}/{8} splat={9} x2s={10} mul_add={11} perm={12} lv_shl={13} lv_shr={14} ppc={15}" -f
            $_.index, $_.guest, $_.instructions, $_.stvewx,
            $_.stvewx_store1, $_.stvewx_dynamic_extract, $_.extract,
            $_.extract_dynamic, $_.extract_constant, $_.splat,
            $_.extract_then_splat, $_.mul_add, $_.permute,
            $_.load_vector_shl, $_.load_vector_shr, (Get-TopPairs $_.ppc 8))
    }

Write-Output ""
Write-Output "## A64 Codegen Audit Anchors"
Write-Output "extract_dynamic_path=src/xenia/cpu/backend/a64/a64_seq_vector.cc EXTRACT_I32 non-constant index builds TBL control with scalar ops before tbl+umov"
Write-Output "splat_i32_path=src/xenia/cpu/backend/a64/a64_seq_vector.cc SPLAT_I32 variable source lowers to dup"
Write-Output "store_i32_path=src/xenia/cpu/backend/a64/a64_seq_memory.cc STORE_I32 handles byte-swap with rev+str and store watches after address computation"
Write-Output "stvewx_hir_shape=PPC stvewx generally appears as masked word address + byte-lane index + dynamic extract + store.1"
