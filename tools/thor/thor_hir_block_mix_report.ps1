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
        [int]$Amount = 1
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

function Get-ContextOffsetName {
    param([int]$Offset)

    if ($Offset -eq 0) { return "thread_state" }
    if ($Offset -eq 8) { return "virtual_membase" }
    if ($Offset -eq 16) { return "lr" }
    if ($Offset -eq 24) { return "ctr" }
    if ($Offset -ge 32 -and $Offset -lt 288) {
        $index = [Math]::Floor(($Offset - 32) / 8)
        $lane = ($Offset - 32) % 8
        if ($lane -eq 0) { return ("r[{0}]" -f $index) }
        return ("r[{0}]+{1}" -f $index, $lane)
    }
    if ($Offset -ge 288 -and $Offset -lt 544) {
        $index = [Math]::Floor(($Offset - 288) / 8)
        $lane = ($Offset - 288) % 8
        if ($lane -eq 0) { return ("f[{0}]" -f $index) }
        return ("f[{0}]+{1}" -f $index, $lane)
    }
    if ($Offset -ge 544 -and $Offset -lt 2592) {
        $index = [Math]::Floor(($Offset - 544) / 16)
        $lane = ($Offset - 544) % 16
        if ($lane -eq 0) { return ("v[{0}]" -f $index) }
        return ("v[{0}]+{1}" -f $index, $lane)
    }
    if ($Offset -ge 2592 -and $Offset -lt 2596) {
        $xerNames = @("xer_ca", "xer_ov", "xer_so", "xer_pad")
        return $xerNames[$Offset - 2592]
    }
    if ($Offset -ge 2596 -and $Offset -lt 2628) {
        $crIndex = [Math]::Floor(($Offset - 2596) / 4)
        $fieldIndex = ($Offset - 2596) % 4
        $fieldNames = @(
            @("lt", "gt", "eq", "so"),
            @("fx", "fex", "vx", "ox"),
            @("0", "1", "2", "3"),
            @("0", "1", "2", "3"),
            @("0", "1", "2", "3"),
            @("0", "1", "2", "3"),
            @("all_equal", "1", "none_equal", "3"),
            @("0", "1", "2", "3")
        )
        return ("cr{0}.{1}" -f $crIndex, $fieldNames[$crIndex][$fieldIndex])
    }
    if ($Offset -ge 2628 -and $Offset -lt 2632) { return "fpscr" }
    if ($Offset -eq 2632) { return "vscr_sat" }
    if ($Offset -eq 2636) { return "thread_id" }
    if ($Offset -ge 2640) { return "runtime_or_reservation" }
    return "unknown"
}

function Get-ContextOffsetClass {
    param([int]$Offset)

    $name = Get-ContextOffsetName $Offset
    if ($name -match "^r\[") { return "GPR" }
    if ($name -match "^f\[") { return "FPR" }
    if ($name -match "^v\[") { return "VMX" }
    if ($name -match "^cr") { return "CR" }
    if ($name -match "^xer") { return "XER" }
    if ($name -in @("lr", "ctr")) { return "LR/CTR" }
    if ($name -in @("fpscr", "vscr_sat")) { return "FP/VMX_STATUS" }
    if ($name -in @("thread_state", "virtual_membase", "thread_id", "runtime_or_reservation")) {
        return "RUNTIME"
    }
    return "OTHER"
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
        context_loads = 0
        context_stores = 0
        gpr_loads = 0
        gpr_stores = 0
        vmx_loads = 0
        vmx_stores = 0
        cr_loads = 0
        cr_stores = 0
        fpr_loads = 0
        fpr_stores = 0
        permutes = 0
        dot4 = 0
        byte_swaps = 0
        memory_loads = 0
        memory_stores = 0
        branches = 0
        calls = 0
        barriers = 0
        ppc = @{}
        permute_controls = @{}
        profile_total = 0
        profile_peak_delta = 0
        profile_body_total = 0
        profile_body_peak_delta = 0
        profile_body_peak_tpe = 0
    }
}

function Get-TopPairs {
    param(
        [hashtable]$Table,
        [int]$Limit = 5
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

if (!(Test-Path -LiteralPath $LogPath)) {
    throw "LogPath not found: $LogPath"
}

$resolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
$functionPattern = [Regex]::Escape($Function.ToUpperInvariant())
$phasePattern = [Regex]::Escape($Phase)
$linePattern = "Filtered function dump $functionPattern $phasePattern`:\s+(?<text>.*)$"
$blocks = New-Object System.Collections.Generic.List[object]
$current = $null

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
        return
    }
    if ($null -eq $current) {
        return
    }

    if ($text -match "^\s*;\s+([0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s+([A-Za-z0-9_\.]+)") {
        Add-Count $current.ppc $Matches[2].ToLowerInvariant()
        return
    }
    if ($text -match "^\s*(;|$)") {
        return
    }

    $current.instructions += 1
    if ($text -match "load_context \+([0-9]+)") {
        $offset = [int]$Matches[1]
        $current.context_loads += 1
        switch (Get-ContextOffsetClass $offset) {
            "GPR" { $current.gpr_loads += 1 }
            "VMX" { $current.vmx_loads += 1 }
            "CR" { $current.cr_loads += 1 }
            "FPR" { $current.fpr_loads += 1 }
        }
    }
    if ($text -match "store_context \+([0-9]+)") {
        $offset = [int]$Matches[1]
        $current.context_stores += 1
        switch (Get-ContextOffsetClass $offset) {
            "GPR" { $current.gpr_stores += 1 }
            "VMX" { $current.vmx_stores += 1 }
            "CR" { $current.cr_stores += 1 }
            "FPR" { $current.fpr_stores += 1 }
        }
    }
    if ($text -match "permute") {
        $current.permutes += 1
    }
    if ($text -match "permute\.2\s+([0-9A-Fa-f]+)") {
        Add-Count $current.permute_controls ("0x" + $Matches[1].ToUpperInvariant())
    }
    if ($text -match "dot_product_4") {
        $current.dot4 += 1
    }
    if ($text -match "byte_swap") {
        $current.byte_swaps += 1
    }
    if ($text -match "\b(load|load_offset|load_vector_sh[lr])(\.[0-9]+)?\b") {
        $current.memory_loads += 1
    }
    if ($text -match "\b(store|store_offset|store_vector_sh[lr])(\.[0-9]+)?\b") {
        $current.memory_stores += 1
    }
    if ($text -match "\bbranch(_true|_false)?\b") {
        $current.branches += 1
    }
    if ($text -match "\bcall(_true|_indirect|_indirect_true|_extern)?\b") {
        $current.calls += 1
    }
    if ($text -match "\bcontext_barrier\b") {
        $current.barriers += 1
    }
}

if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    if (!(Test-Path -LiteralPath $BlockProfileLog)) {
        throw "BlockProfileLog not found: $BlockProfileLog"
    }
    $resolvedProfile = (Resolve-Path -LiteralPath $BlockProfileLog).Path
    $profilePattern = "A64 speed profile block top \d+: fn $functionPattern .* block=(?<block>\d+) guest=(?<guest>[0-9A-Fa-f]{8}) delta=(?<delta>\d+) total=(?<total>\d+)"
    $bodyProfilePattern = "A64 speed profile block body top \d+: fn $functionPattern .* block=(?<block>\d+) guest=(?<guest>[0-9A-Fa-f]{8}) body_ticks_delta=(?<delta>\d+) body_ticks_total=(?<total>\d+) entries_delta=(?<entries>\d+) ticks_per_entry=(?<tpe>\d+)"
    Get-Content -LiteralPath $resolvedProfile | ForEach-Object {
        if ($_ -match $profilePattern) {
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
            if ($row.guest -eq "00000000" -and $Matches.guest -ne "00000000") {
                $row.guest = $Matches.guest.ToUpperInvariant()
            }
            return
        }

        if ($_ -match $bodyProfilePattern) {
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
            $tpe = [int64]$Matches.tpe
            if ($total -gt $row.profile_body_total) {
                $row.profile_body_total = $total
            }
            if ($delta -gt $row.profile_body_peak_delta) {
                $row.profile_body_peak_delta = $delta
            }
            if ($tpe -gt $row.profile_body_peak_tpe) {
                $row.profile_body_peak_tpe = $tpe
            }
            if ($row.guest -eq "00000000" -and $Matches.guest -ne "00000000") {
                $row.guest = $Matches.guest.ToUpperInvariant()
            }
            return
        }
    }
}

$totalInstructions = ($blocks | Measure-Object -Property instructions -Sum).Sum
$totalContext = (($blocks | Measure-Object -Property context_loads -Sum).Sum +
    ($blocks | Measure-Object -Property context_stores -Sum).Sum)
$totalPermutes = ($blocks | Measure-Object -Property permutes -Sum).Sum
$totalDot4 = ($blocks | Measure-Object -Property dot4 -Sum).Sum

Write-Output "# HIR Block Mix Report"
Write-Output ""
Write-Output "log=$resolvedLog"
if (![string]::IsNullOrWhiteSpace($BlockProfileLog)) {
    Write-Output "block_profile_log=$((Resolve-Path -LiteralPath $BlockProfileLog).Path)"
}
Write-Output "function=$($Function.ToUpperInvariant())"
Write-Output "phase=$Phase"
Write-Output "blocks=$($blocks.Count)"
Write-Output "instructions=$totalInstructions"
Write-Output "context_accesses=$totalContext"
Write-Output "permutes=$totalPermutes"
Write-Output "dot_product_4=$totalDot4"

Write-Output ""
Write-Output "## Dynamic Hot Blocks With HIR Mix"
$dynamicRows = $blocks | Where-Object { $_.profile_total -gt 0 } |
    Sort-Object -Property @{ Expression = "profile_total"; Descending = $true },
                          @{ Expression = "index"; Ascending = $true } |
    Select-Object -First $Top
if (!$dynamicRows) {
    Write-Output "(no block profile data supplied)"
} else {
    $dynamicRows | ForEach-Object {
        Write-Output ("block={0} guest={1} total={2} peak_delta={3} body_total={4} body_peak_delta={5} body_peak_tpe={6} instr={7} ctx={8}/{9} gpr={10}/{11} vmx={12}/{13} cr={14}/{15} mem={16}/{17} perm={18} dot4={19} bswap={20} branches={21} calls={22} barriers={23} ppc={24} controls={25}" -f
            $_.index, $_.guest, $_.profile_total, $_.profile_peak_delta,
            $_.profile_body_total, $_.profile_body_peak_delta,
            $_.profile_body_peak_tpe,
            $_.instructions, $_.context_loads, $_.context_stores,
            $_.gpr_loads, $_.gpr_stores, $_.vmx_loads, $_.vmx_stores,
            $_.cr_loads, $_.cr_stores, $_.memory_loads, $_.memory_stores,
            $_.permutes, $_.dot4, $_.byte_swaps, $_.branches, $_.calls,
            $_.barriers, (Get-TopPairs $_.ppc 6), (Get-TopPairs $_.permute_controls 4))
    }
}

Write-Output ""
Write-Output "## Dynamic Body-Time Blocks With HIR Mix"
$bodyRows = $blocks | Where-Object { $_.profile_body_total -gt 0 } |
    Sort-Object -Property @{ Expression = "profile_body_total"; Descending = $true },
                          @{ Expression = "index"; Ascending = $true } |
    Select-Object -First $Top
if (!$bodyRows) {
    Write-Output "(no block body-time profile data supplied)"
} else {
    $bodyRows | ForEach-Object {
        Write-Output ("block={0} guest={1} body_total={2} body_peak_delta={3} body_peak_tpe={4} entry_total={5} entry_peak_delta={6} instr={7} ctx={8}/{9} gpr={10}/{11} vmx={12}/{13} cr={14}/{15} mem={16}/{17} perm={18} dot4={19} bswap={20} branches={21} calls={22} barriers={23} ppc={24} controls={25}" -f
            $_.index, $_.guest, $_.profile_body_total,
            $_.profile_body_peak_delta, $_.profile_body_peak_tpe,
            $_.profile_total, $_.profile_peak_delta,
            $_.instructions, $_.context_loads, $_.context_stores,
            $_.gpr_loads, $_.gpr_stores, $_.vmx_loads, $_.vmx_stores,
            $_.cr_loads, $_.cr_stores, $_.memory_loads, $_.memory_stores,
            $_.permutes, $_.dot4, $_.byte_swaps, $_.branches, $_.calls,
            $_.barriers, (Get-TopPairs $_.ppc 6), (Get-TopPairs $_.permute_controls 4))
    }
}

Write-Output ""
Write-Output "## Static Context-Heavy Blocks"
$blocks |
    Sort-Object -Property @{ Expression = { $_.context_loads + $_.context_stores }; Descending = $true },
                          @{ Expression = "index"; Ascending = $true } |
    Select-Object -First $Top |
    ForEach-Object {
        Write-Output ("block={0} guest={1} instr={2} ctx={3}/{4} gpr={5}/{6} vmx={7}/{8} cr={9}/{10} branches={11} calls={12} barriers={13} ppc={14}" -f
            $_.index, $_.guest, $_.instructions, $_.context_loads,
            $_.context_stores, $_.gpr_loads, $_.gpr_stores, $_.vmx_loads,
            $_.vmx_stores, $_.cr_loads, $_.cr_stores, $_.branches, $_.calls,
            $_.barriers, (Get-TopPairs $_.ppc 6))
    }

Write-Output ""
Write-Output "## Static Vector-Heavy Blocks"
$blocks |
    Sort-Object -Property @{ Expression = { $_.permutes + $_.dot4 + $_.vmx_loads + $_.vmx_stores }; Descending = $true },
                          @{ Expression = "index"; Ascending = $true } |
    Select-Object -First $Top |
    ForEach-Object {
        Write-Output ("block={0} guest={1} instr={2} vmx={3}/{4} perm={5} dot4={6} bswap={7} mem={8}/{9} ppc={10} controls={11}" -f
            $_.index, $_.guest, $_.instructions, $_.vmx_loads, $_.vmx_stores,
            $_.permutes, $_.dot4, $_.byte_swaps, $_.memory_loads,
            $_.memory_stores, (Get-TopPairs $_.ppc 6),
            (Get-TopPairs $_.permute_controls 4))
    }
