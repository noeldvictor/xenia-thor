param(
    [string]$ParentLogPath = "scratch\thor-debug\20260521-170941-speed-logcat.txt",
    [string]$ParentFunction = "82282490",
    [string]$ParentCallPc = "82282598",
    [string]$CalleeLogPath = "scratch\thor-debug\20260524-041413-speed-logcat.txt",
    [string]$CalleeFunction = "82287788",
    [string]$EdgeProfileLog = "scratch\thor-debug\20260524-030450-speed-logcat.txt",
    [string]$A64EmitterHeaderPath = "src\xenia\cpu\backend\a64\a64_emitter.h",
    [string]$A64BackendPath = "src\xenia\cpu\backend\a64\a64_backend.cc",
    [string]$A64EmitterPath = "src\xenia\cpu\backend\a64\a64_emitter.cc",
    [string]$RegisterAllocationPath = "src\xenia\cpu\compiler\passes\register_allocation_pass.cc",
    [string]$TranslatorPath = "src\xenia\cpu\ppc\ppc_translator.cc",
    [int]$Top = 12
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

function Get-FirstMatch {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )

    foreach ($line in $Lines) {
        if ($line -match $Pattern) {
            return $Matches
        }
    }
    return $null
}

function Get-FirstLine {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )

    foreach ($line in $Lines) {
        if ($line -match $Pattern) {
            return $line.Trim()
        }
    }
    return "-"
}

function Get-ClassCount {
    param(
        [string[]]$Lines,
        [string]$Name
    )

    $match = Get-FirstMatch $Lines ("^{0}\s+([0-9]+)$" -f [regex]::Escape($Name))
    if ($null -eq $match) {
        return 0
    }
    return [int64]$match[1]
}

function Get-DocEvidence {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Fallback
    )

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        return $Fallback
    }
    $text = Get-Content -Raw -LiteralPath $resolved
    $match = [regex]::Match($text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if ($match.Success) {
        return ($match.Value -replace '\s+', ' ').Trim()
    }
    return $Fallback
}

$parentLog = Resolve-RepoPath $ParentLogPath
$calleeLog = Resolve-RepoPath $CalleeLogPath
$edgeLog = Resolve-RepoPath $EdgeProfileLog
foreach ($path in @($parentLog, $calleeLog, $edgeLog)) {
    if (!(Test-Path -LiteralPath $path)) {
        throw "Required input not found: $path"
    }
}

$stateScript = Resolve-RepoPath "tools\thor\thor_hir_state_span_report.ps1"
$gprScript = Resolve-RepoPath "tools\thor\thor_hir_gpr_promotion_audit.ps1"
$roundtripScript = Resolve-RepoPath "tools\thor\thor_hir_interproc_state_roundtrip_audit.ps1"
foreach ($script in @($stateScript, $gprScript, $roundtripScript)) {
    if (!(Test-Path -LiteralPath $script)) {
        throw "Script not found: $script"
    }
}

$stateOutput = @(& $stateScript -LogPath $parentLog -Function $ParentFunction -Phase OptHIR -Top $Top)
$gprOutput = @(& $gprScript -LogPath $parentLog -Function $ParentFunction -Phase OptHIR -Top $Top)
$roundtripOutput = @(& $roundtripScript `
    -ParentLogPath $parentLog `
    -ParentFunction $ParentFunction `
    -ParentCallPc $ParentCallPc `
    -CalleeLogPath $calleeLog `
    -CalleeFunction $CalleeFunction `
    -EdgeProfileLog $edgeLog `
    -Offsets "56,296,2628,16" `
    -Top 24)

$emitterHeaderText = Read-RequiredText $A64EmitterHeaderPath
$emitterHeaderLines = Read-RequiredLines $A64EmitterHeaderPath
$backendText = Read-RequiredText $A64BackendPath
$backendLines = Read-RequiredLines $A64BackendPath
$emitterText = Read-RequiredText $A64EmitterPath
$emitterLines = Read-RequiredLines $A64EmitterPath
$regAllocLines = Read-RequiredLines $RegisterAllocationPath
$translatorLines = Read-RequiredLines $TranslatorPath

$reservedLine = Find-Line $emitterHeaderLines 'Reserved: sp, x19'
$gprCountLine = Find-Line $emitterHeaderLines 'GPR_COUNT\s*=\s*7'
$vecCountLine = Find-Line $emitterHeaderLines 'VEC_COUNT\s*=\s*28'
$guestToHostLine = Find-Line $backendLines 'GPRs x19-x28 are callee-saved'
$directCallLine = Find-Line $emitterLines 'guest return'
$spillLine = Find-Line $regAllocLines 'None available! Spill required'
$localSlotLine = Find-Line $regAllocLines 'builder->AllocLocal'
$raLastLine = Find-Line $translatorLines 'Register allocation for the target backend'

$contextBarriers = [int64]((Get-FirstMatch $stateOutput '^context_barriers=([0-9]+)$')[1])
$gprAccesses = Get-ClassCount $stateOutput "GPR"
$vmxAccesses = Get-ClassCount $stateOutput "VMX"
$crAccesses = Get-ClassCount $stateOutput "CR"
$fprAccesses = Get-ClassCount $stateOutput "FPR"
$lrCtrAccesses = Get-ClassCount $stateOutput "LR/CTR"
$fpscrAccesses = Get-ClassCount $stateOutput "FP/VMX_STATUS"

$gprSummary = Get-FirstMatch $gprOutput '^whole_gpr_loads=([0-9]+)$'
$wholeGprLoads = if ($null -ne $gprSummary) { [int64]$gprSummary[1] } else { 0 }
$wholeGprStores = [int64]((Get-FirstMatch $gprOutput '^whole_gpr_stores=([0-9]+)$')[1])
$singlePredBlocks = [int64]((Get-FirstMatch $gprOutput '^dominated_single_pred_blocks=([0-9]+)$')[1])
$multiPredBlocks = [int64]((Get-FirstMatch $gprOutput '^multi_pred_blocks=([0-9]+)$')[1])
$firstLoads = [int64]((Get-FirstMatch $gprOutput '^first_gpr_loads_in_dominated_single_pred_blocks=([0-9]+)$')[1])
$promotionGate = Get-FirstLine $gprOutput '^promotion_gate='

$topCacheLine = Get-FirstLine $stateOutput '^\+40 r\[1\] score='
$secondCacheLine = Get-FirstLine $stateOutput '^\+120 r\[11\] score='
$topPromotionLine = Get-FirstLine $gprOutput '^\+40 r\[1\] score='
$r3PromotionLine = Get-FirstLine $gprOutput '^\+56 r\[3\] score='

$edgeMatch = Get-FirstMatch $roundtripOutput '^edge block=([0-9A-F]+) calls_total=([0-9]+).*body_ticks_total=([0-9]+).*ticks_per_call=([0-9]+)'
$edgeBlock = if ($null -ne $edgeMatch) { $edgeMatch[1] } else { "-" }
$edgeCalls = if ($null -ne $edgeMatch) { [int64]$edgeMatch[2] } else { 0 }
$edgeBody = if ($null -ne $edgeMatch) { [int64]$edgeMatch[3] } else { 0 }
$edgeTpc = if ($null -ne $edgeMatch) { [int64]$edgeMatch[4] } else { 0 }

$f1Plan = Get-FirstLine $roundtripOutput '^plan field=f\[1\]'
$fpscrPlan = Get-FirstLine $roundtripOutput '^plan field=fpscr'
$r3Plan = Get-FirstLine $roundtripOutput '^plan field=r\[3\]'
$lrPlan = Get-FirstLine $roundtripOutput '^plan field=lr'

$contextValueCacheEvidence = Get-DocEvidence `
    "docs\research\20260521-212700-a64-gpr-cache-barrier-negative.md" `
    'loads/hits=546/0 stores/cached=562/463[^`]*' `
    "context value cache negative doc missing"
$localSlotEvidence = Get-DocEvidence `
    "docs\research\20260522-113012-gpr-local-slot-promotion-probe.md" `
    'code size increased from `87168` to `87660`' `
    "local slot probe doc missing"
$r1LiveInEvidence = Get-DocEvidence `
    "docs\research\20260522-162135-r1-livein-quiet-ab.md" `
    'This is not a speed win\.[^.]*\.' `
    "r1 live-in quiet A/B doc present but summary pattern not found"
$f1CarrierEvidence = Get-DocEvidence `
    "docs\research\20260524-110545-blue-dragon-f1-carrier-fastpath-ab.md" `
    'It is not\s+a speed win in the quiet A/B\.' `
    "f1 carrier A/B doc present but summary pattern not found"
$fpscrEvidence = Get-DocEvidence `
    "docs\research\20260525-134556-82287788-fpscr-cfg-carrier-design.md" `
    'payload_materializations=0/0[^`]*' `
    "fpscr design doc missing"

Write-Output "A64 Register Cache Gap Report"
Write-Output ("repo={0}" -f $RepoRoot)
Write-Output ("parent_function={0} parent_log={1}" -f $ParentFunction, $parentLog)
Write-Output ("callee_function={0} callee_log={1}" -f $CalleeFunction, $calleeLog)
Write-Output ("edge_profile_log={0}" -f $edgeLog)
Write-Output ""

Write-Output "parent_context_traffic:"
Write-Output ("  accesses gpr={0} vmx={1} cr={2} fpr={3} lr_ctr={4} fp_vmx_status={5} context_barriers={6}" -f `
    $gprAccesses, $vmxAccesses, $crAccesses, $fprAccesses, $lrCtrAccesses, $fpscrAccesses, $contextBarriers)
Write-Output ("  whole_gpr_loads={0} whole_gpr_stores={1} dominated_single_pred_blocks={2} multi_pred_blocks={3} first_gpr_loads_in_dominated_single_pred_blocks={4}" -f `
    $wholeGprLoads, $wholeGprStores, $singlePredBlocks, $multiPredBlocks, $firstLoads)
Write-Output ("  top_state_cache_candidate={0}" -f $topCacheLine)
Write-Output ("  second_state_cache_candidate={0}" -f $secondCacheLine)
Write-Output ("  top_pre_ra_promotion_candidate={0}" -f $topPromotionLine)
Write-Output ("  r3_pre_ra_promotion_candidate={0}" -f $r3PromotionLine)
Write-Output ("  promotion_gate={0}" -f $promotionGate)
Write-Output ""

Write-Output "source_register_contract:"
Write-Output ("  fixed_regs source={0} evidence=reserved_x19_backend_x20_context_x21_membase={1}" -f `
    (Format-Source $A64EmitterHeaderPath $reservedLine),
    ([regex]::IsMatch($emitterHeaderText, 'Reserved: sp, x19.*x20.*x21')))
Write-Output ("  allocatable_regs source={0} gpr_count_7={1} vec_count_28={2}" -f `
    (Format-Source $A64EmitterHeaderPath $gprCountLine),
    ($gprCountLine -gt 0), ($vecCountLine -gt 0))
Write-Output ("  guest_to_host_abi source={0} gprs_x19_x28_callee_saved={1}" -f `
    (Format-Source $A64BackendPath $guestToHostLine),
    ($guestToHostLine -gt 0))
Write-Output ("  direct_call_payload source={0} evidence=guest_return_x0_only={1}" -f `
    (Format-Source $A64EmitterPath $directCallLine),
    ([regex]::IsMatch($emitterText, 'guest return')))
Write-Output ("  register_allocator source={0};{1};{2} evidence=ra_last_before_finalization_and_spills_to_locals" -f `
    (Format-Source $TranslatorPath $raLastLine),
    (Format-Source $RegisterAllocationPath $spillLine),
    (Format-Source $RegisterAllocationPath $localSlotLine))
Write-Output ""

Write-Output "edge_roundtrip:"
Write-Output ("  edge block={0} calls_total={1} body_ticks_total={2} ticks_per_call={3}" -f `
    $edgeBlock, $edgeCalls, $edgeBody, $edgeTpc)
Write-Output ("  plan_r3={0}" -f $r3Plan)
Write-Output ("  plan_f1={0}" -f $f1Plan)
Write-Output ("  plan_fpscr={0}" -f $fpscrPlan)
Write-Output ("  plan_lr={0}" -f $lrPlan)
Write-Output ""

Write-Output "negative_probe_guardrails:"
Write-Output ("  context_value_cache={0}" -f $contextValueCacheEvidence)
Write-Output ("  gpr_local_slot_probe={0}" -f $localSlotEvidence)
Write-Output ("  r1_livein_quiet_ab={0}" -f $r1LiveInEvidence)
Write-Output ("  f1_carrier_quiet_ab={0}" -f $f1CarrierEvidence)
Write-Output ("  fpscr_cfg_design={0}" -f $fpscrEvidence)
Write-Output ""

Write-Output "ranked_next_lanes:"
Write-Output "1. structural_guest_state_cache_design: start from 82282490 GPR/VMX/CR traffic and integrate with the HIR/RA boundary; emit-time one-slot caching already had zero load hits."
Write-Output "2. caller_local_edge_variant_storage: if attacking 82282490:82282598 -> 82287788, model caller-local or side-table payload state with normal-entry fallback before changing behavior."
Write-Output "3. fpscr_cfg_counter_skeleton: useful only as counter/payload materialization proof; no speed A/B while payload_materializations remains zero."
Write-Output "4. narrow_pc_fastpaths: deprioritized unless backed by a larger state-cache/vector plan; stvewx and f1 carrier shrank code but did not prove route speed."
Write-Output ""
Write-Output "decision=no_generated_behavior_patch_from_this_report"
Write-Output "reason=the hot path has enough state traffic to justify a mature register-cache design, but previous local probes show the patch must be structural and visibility-aware rather than another isolated context load fold."
Write-Output "next_slice=write a default-off audit/design skeleton for guest-state register caching or edge-variant payload storage, with counters and normal-entry fallback, before any Thor quiet speed A/B."
