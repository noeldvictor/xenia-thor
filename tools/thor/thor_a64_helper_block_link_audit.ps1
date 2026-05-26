param(
    [string]$LogPath = "scratch\thor-debug\20260526-000910-speed-logcat.txt",
    [string]$MetaPath = "",
    [string[]]$HotFunctions = @("82282490", "82281D28", "82287788"),
    [string]$A64EmitterPath = "src\xenia\cpu\backend\a64\a64_emitter.cc",
    [string]$A64BackendPath = "src\xenia\cpu\backend\a64\a64_backend.cc",
    [string]$A64FunctionPath = "src\xenia\cpu\backend\a64\a64_function.h",
    [string]$CodeCacheBasePath = "src\xenia\cpu\backend\code_cache_base.h"
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

function Format-Ratio {
    param(
        [int64]$Numerator,
        [int64]$Denominator
    )

    if ($Denominator -le 0) {
        return "n/a"
    }
    return ("{0:N2}" -f ($Numerator / [double]$Denominator))
}

function Get-MetaValue {
    param(
        [string]$Text,
        [string]$Key
    )

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return ""
    }
    $pattern = "(?m)^" + [regex]::Escape($Key) + "=(.*)$"
    $match = [regex]::Match($Text, $pattern)
    if (!$match.Success) {
        return ""
    }
    return $match.Groups[1].Value.Trim()
}

$resolvedLog = Resolve-RepoPath $LogPath
if (!(Test-Path -LiteralPath $resolvedLog)) {
    throw "LogPath not found: $LogPath"
}

if ([string]::IsNullOrWhiteSpace($MetaPath)) {
    $candidate = $resolvedLog -replace "-speed-logcat\.txt$", "-meta.txt"
    if (Test-Path -LiteralPath $candidate) {
        $MetaPath = $candidate
    }
}

$metaText = ""
if (![string]::IsNullOrWhiteSpace($MetaPath)) {
    $resolvedMeta = Resolve-RepoPath $MetaPath
    if (Test-Path -LiteralPath $resolvedMeta) {
        $metaText = Get-Content -Raw -LiteralPath $resolvedMeta
    }
}

$emitter = Read-RequiredText $A64EmitterPath
$emitterLines = Read-RequiredLines $A64EmitterPath
$backend = Read-RequiredText $A64BackendPath
$backendLines = Read-RequiredLines $A64BackendPath
$a64Function = Read-RequiredText $A64FunctionPath
$codeCache = Read-RequiredText $CodeCacheBasePath
$codeCacheLines = Read-RequiredLines $CodeCacheBasePath

$normalizedHotFunctions = @($HotFunctions | ForEach-Object {
        ([string]$_).Trim().TrimStart("0", "x", "X").PadLeft(8, "0").ToUpperInvariant()
    })

$summaryRows = New-Object System.Collections.Generic.List[object]
$bodyRows = @{}
$callEdgeRows = 0
$callEdgeAuditRows = 0

$summaryPattern = "A64 speed profile summary: funcs=(?<funcs>\d+) entry_delta=(?<entry_delta>\d+) h2g=(?<h2g_delta>\d+)/(?<h2g_total>\d+) g2h=(?<g2h_delta>\d+)/(?<g2h_total>\d+) direct=(?<direct_delta>\d+)/(?<direct_total>\d+) indirect=(?<indirect_delta>\d+)/(?<indirect_total>\d+) extern=(?<extern_delta>\d+)/(?<extern_total>\d+) resolves=(?<resolves_delta>\d+)/(?<resolves_total>\d+) resolve_misses=(?<miss_delta>\d+)/(?<miss_total>\d+)"
$bodyPattern = "A64 speed profile body top \d+: fn (?<fn>[0-9A-Fa-f]{8}) '(?<name>[^']*)' body_ticks_delta=(?<body_delta>\d+) body_ticks_total=(?<body_total>\d+) entries_delta=(?<entries_delta>\d+) ticks_per_entry=(?<tpe>\d+) code_size=(?<code_size>\d+)"

Get-Content -LiteralPath $resolvedLog | ForEach-Object {
    if ($_ -match $summaryPattern) {
        $summaryRows.Add([pscustomobject][ordered]@{
            funcs = [int64]$Matches.funcs
            entry_delta = [int64]$Matches.entry_delta
            h2g_delta = [int64]$Matches.h2g_delta
            h2g_total = [int64]$Matches.h2g_total
            g2h_delta = [int64]$Matches.g2h_delta
            g2h_total = [int64]$Matches.g2h_total
            direct_delta = [int64]$Matches.direct_delta
            direct_total = [int64]$Matches.direct_total
            indirect_delta = [int64]$Matches.indirect_delta
            indirect_total = [int64]$Matches.indirect_total
            extern_delta = [int64]$Matches.extern_delta
            extern_total = [int64]$Matches.extern_total
            resolves_delta = [int64]$Matches.resolves_delta
            resolves_total = [int64]$Matches.resolves_total
            miss_delta = [int64]$Matches.miss_delta
            miss_total = [int64]$Matches.miss_total
        }) | Out-Null
        return
    }

    if ($_ -match $bodyPattern) {
        $fn = $Matches.fn.ToUpperInvariant()
        if ($normalizedHotFunctions -contains $fn) {
            $row = [pscustomobject][ordered]@{
                function = $fn
                name = $Matches.name
                body_ticks_delta = [int64]$Matches.body_delta
                body_ticks_total = [int64]$Matches.body_total
                entries_delta = [int64]$Matches.entries_delta
                ticks_per_entry = [int64]$Matches.tpe
                code_size = [int64]$Matches.code_size
            }
            if (!$bodyRows.ContainsKey($fn) -or
                $row.body_ticks_total -ge $bodyRows[$fn].body_ticks_total) {
                $bodyRows[$fn] = $row
            }
        }
        return
    }

    if ($_ -match "A64 speed profile call edge top") {
        $callEdgeRows += 1
        return
    }
    if ($_ -match "A64 call-edge compile audit") {
        $callEdgeAuditRows += 1
        return
    }
}

if ($summaryRows.Count -eq 0) {
    throw "No A64 speed profile summary rows found in $resolvedLog"
}

$finalSummary = $summaryRows[$summaryRows.Count - 1]
$dispatchDelta = [int64]$finalSummary.direct_delta +
    [int64]$finalSummary.indirect_delta +
    [int64]$finalSummary.extern_delta
$dispatchTotal = [int64]$finalSummary.direct_total +
    [int64]$finalSummary.indirect_total +
    [int64]$finalSummary.extern_total

$callLine = Find-Line $emitterLines 'void A64Emitter::Call'
$callIndirectLine = Find-Line $emitterLines 'void A64Emitter::CallIndirect'
$callExternLine = Find-Line $emitterLines 'void A64Emitter::CallExtern'
$pushStackpointLine = Find-Line $emitterLines 'PushStackpoint\(\);'
$syncStackLine = Find-Line $emitterLines 'synchronize_stack_on_next_instruction_ = true;'
$resolveThunkLine = Find-Line $backendLines 'ResolveFunctionThunk A64HelperEmitter::EmitResolveFunctionThunk'
$resolveFunctionLine = Find-Line $backendLines '^uint64_t ResolveFunction'
$recordResolveLine = Find-Line $backendLines 'void A64Backend::RecordResolveFunction'
$callEdgeSetupLine = Find-Line $emitterLines 'SetupProfileCallEdges'
$maybeCallEdgeStartLine = Find-Line $emitterLines 'MaybeEmitCallEdgeProfileStart'
$addIndirectionLine = Find-Line $codeCacheLines 'void AddIndirection\(uint32_t guest_address, uint32_t host_address\)'

$directCompiledCall = Test-Pattern $emitter 'if \(fn->machine_code\(\)\).*?mov\(x9, reinterpret_cast<uint64_t>\(fn->machine_code\(\)\)\);.*?blr\(x9\);'
$uncompiledIndirection = Test-Pattern $emitter 'code_cache_->has_indirection_table\(\).*?ldr\(w9, ptr\(x16'
$directRuntimeResolve = Test-Pattern $emitter 'mov\(x9, reinterpret_cast<uint64_t>\(&ResolveFunction\)\);'
$indirectDispatch = Test-Pattern $emitter 'void A64Emitter::CallIndirect.*?ldr\(w9, ptr\(x16'
$externThunk = Test-Pattern $emitter 'void A64Emitter::CallExtern.*?guest_to_host_thunk'
$resolveThunk = Test-Pattern $backend 'ResolveFunctionThunk A64HelperEmitter::EmitResolveFunctionThunk'
$resolveCounts = Test-Pattern $backend 'RecordResolveFunction\(true\)' -and Test-Pattern $backend 'speed_profile_resolve_calls_'
$singleMachineCodePointer = Test-Pattern $a64Function 'std::atomic<uint8_t\*> machine_code_'
$guestAddressIndirection = Test-Pattern $codeCache 'void AddIndirection\(uint32_t guest_address, uint32_t host_address\).*?guest_address - kIndirectionTableBaseLow'
$callEdgeSupport = (Test-Pattern $emitter 'SetupProfileCallEdges') -and
    (Test-Pattern $emitter 'MaybeEmitCallEdgeProfileStart') -and
    (Test-Pattern $backend 'A64 speed profile call edge top')

$callEdgeFilter = Get-MetaValue $metaText "arm64_speed_profile_call_edge_filter"
$callEdgeAuditOnly = Get-MetaValue $metaText "arm64_speed_profile_call_edge_audit_only"
$bodyTimeFilter = Get-MetaValue $metaText "arm64_speed_profile_body_time_filter"
$captureHead = Get-MetaValue $metaText "head"
$apkSha = Get-MetaValue $metaText "apk_sha256"

Write-Output "# A64 Helper / Block-Link Audit"
Write-Output ""
Write-Output ("repo={0}" -f $RepoRoot)
Write-Output ("log={0}" -f $resolvedLog)
if (![string]::IsNullOrWhiteSpace($MetaPath)) {
    Write-Output ("meta={0}" -f (Resolve-RepoPath $MetaPath))
}
if (![string]::IsNullOrWhiteSpace($captureHead)) {
    Write-Output ("capture_head={0}" -f $captureHead)
}
if (![string]::IsNullOrWhiteSpace($apkSha)) {
    Write-Output ("apk_sha256={0}" -f $apkSha)
}
Write-Output ("hot_functions={0}" -f ($normalizedHotFunctions -join ","))
Write-Output ("body_time_filter={0}" -f $bodyTimeFilter)
Write-Output ("call_edge_filter={0}" -f $(if ([string]::IsNullOrWhiteSpace($callEdgeFilter)) { "<empty>" } else { $callEdgeFilter }))
Write-Output ("call_edge_audit_only={0}" -f $(if ([string]::IsNullOrWhiteSpace($callEdgeAuditOnly)) { "<empty>" } else { $callEdgeAuditOnly }))
Write-Output ""

Write-Output "## Route Dispatch Summary"
Write-Output ("summary_rows={0}" -f $summaryRows.Count)
Write-Output ("final funcs={0} entry_delta={1} direct_delta={2} direct_total={3} indirect_delta={4} indirect_total={5} extern_delta={6} extern_total={7} resolves_delta={8} resolves_total={9} resolve_misses_delta={10} resolve_misses_total={11}" -f
    $finalSummary.funcs, $finalSummary.entry_delta,
    $finalSummary.direct_delta, $finalSummary.direct_total,
    $finalSummary.indirect_delta, $finalSummary.indirect_total,
    $finalSummary.extern_delta, $finalSummary.extern_total,
    $finalSummary.resolves_delta, $finalSummary.resolves_total,
    $finalSummary.miss_delta, $finalSummary.miss_total)
Write-Output ("dispatch_delta_total={0} dispatch_total={1} dispatch_delta_per_entry_delta={2}" -f
    $dispatchDelta, $dispatchTotal, (Format-Ratio $dispatchDelta $finalSummary.entry_delta))
Write-Output ("call_edge_rows={0} call_edge_audit_rows={1}" -f $callEdgeRows, $callEdgeAuditRows)
Write-Output ""

Write-Output "## Hot Body-Time Rows"
foreach ($fn in $normalizedHotFunctions) {
    if ($bodyRows.ContainsKey($fn)) {
        $row = $bodyRows[$fn]
        Write-Output ("fn={0} body_ticks_total={1} body_ticks_delta={2} entries_delta={3} ticks_per_entry={4} code_size={5}" -f
            $row.function, $row.body_ticks_total, $row.body_ticks_delta,
            $row.entries_delta, $row.ticks_per_entry, $row.code_size)
    } else {
        Write-Output ("fn={0} body_time_row=missing" -f $fn)
    }
}
Write-Output ""

Write-Output "## Source Checks"
Write-Output ("check direct_compiled_call={0} source={1}" -f $directCompiledCall, (Format-Source $A64EmitterPath $callLine))
Write-Output ("check uncompiled_direct_indirection={0} source={1}" -f $uncompiledIndirection, (Format-Source $A64EmitterPath $callLine))
Write-Output ("check direct_runtime_resolve_fallback={0} source={1}" -f $directRuntimeResolve, (Format-Source $A64EmitterPath $callLine))
Write-Output ("check indirect_dispatch_indirection={0} source={1}" -f $indirectDispatch, (Format-Source $A64EmitterPath $callIndirectLine))
Write-Output ("check extern_guest_to_host_thunk={0} source={1}" -f $externThunk, (Format-Source $A64EmitterPath $callExternLine))
Write-Output ("check push_stackpoint_present={0} source={1}" -f ($pushStackpointLine -gt 0), (Format-Source $A64EmitterPath $pushStackpointLine))
Write-Output ("check post_call_stack_sync_present={0} source={1}" -f ($syncStackLine -gt 0), (Format-Source $A64EmitterPath $syncStackLine))
Write-Output ("check resolve_thunk_present={0} source={1}" -f $resolveThunk, (Format-Source $A64BackendPath $resolveThunkLine))
Write-Output ("check resolve_function_records={0} source={1}" -f $resolveCounts, (Format-Source $A64BackendPath $recordResolveLine))
Write-Output ("check singleton_machine_code_pointer={0} source={1}" -f $singleMachineCodePointer, $A64FunctionPath)
Write-Output ("check guest_address_indirection_slot={0} source={1}" -f $guestAddressIndirection, (Format-Source $CodeCacheBasePath $addIndirectionLine))
Write-Output ("check call_edge_profile_support={0} source={1}" -f $callEdgeSupport, (Format-Source $A64EmitterPath $callEdgeSetupLine))
Write-Output ("check call_edge_timer_site={0} source={1}" -f ($maybeCallEdgeStartLine -gt 0), (Format-Source $A64EmitterPath $maybeCallEdgeStartLine))
Write-Output ("check resolve_function_source={0}" -f (Format-Source $A64BackendPath $resolveFunctionLine))
Write-Output ""

Write-Output "## Decision"
if ($finalSummary.miss_total -eq 0 -and $finalSummary.resolves_delta -le 10) {
    Write-Output "resolver_wall=false"
    Write-Output "reason=final interval has near-zero resolves and zero resolve misses; warmed route is not dominated by ResolveFunction."
} else {
    Write-Output "resolver_wall=possible"
    Write-Output "reason=resolve activity remains visible; inspect unresolved/compile path before block-link behavior."
}

if ($dispatchDelta -gt 1000000 -and $callEdgeRows -eq 0) {
    Write-Output "next_runtime_counter=arm64_speed_profile_call_edge_filter"
    Write-Output ("reason=final interval has {0} direct/indirect/extern dispatch events but no dynamic call-edge rows; profile the hot cluster before behavior changes." -f $dispatchDelta)
} elseif ($callEdgeRows -gt 0) {
    Write-Output "next_runtime_counter=analyze_existing_call_edge_rows"
    Write-Output "reason=dynamic call-edge rows already exist in this log."
} else {
    Write-Output "next_runtime_counter=not_dispatch_first"
    Write-Output "reason=dispatch volume is not clearly material in this route."
}

if ($guestAddressIndirection -and $singleMachineCodePointer) {
    Write-Output "variant_entry_model=blocked_by_singleton"
    Write-Output "reason=A64 function entry and code-cache indirection are keyed by guest address only; edge-specific variants need a general entry/fallback design, not a one-edge machine_code replacement."
}

if ($directCompiledCall -and $callEdgeSupport) {
    Write-Output "preferred_next_slice=route_capture_with_call_edge_filter_82282490_82281D28_82287788"
    Write-Output "capture_hint=-Arm64SpeedProfileCallEdgeFilter ""82282490,82281D28,82287788"" -Arm64SpeedProfileBodyTimeFilter ""82282490,82281D28,82287788"" -Arm64SpeedProfileBodyTimeAfterMs 120000"
} else {
    Write-Output "preferred_next_slice=source_fix_before_capture"
    Write-Output "capture_hint=call-edge support or direct-call source checks failed; inspect source before Thor."
}
