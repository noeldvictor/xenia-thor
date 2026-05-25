param(
    [string]$ParentLogPath = "scratch\thor-debug\20260521-170941-speed-logcat.txt",
    [string]$ParentFunction = "82282490",
    [string]$RegisterCacheGapScriptPath = "tools\thor\thor_a64_register_cache_gap_report.ps1",
    [string]$StateSpanScriptPath = "tools\thor\thor_hir_state_span_report.ps1",
    [string]$GprPromotionScriptPath = "tools\thor\thor_hir_gpr_promotion_audit.ps1",
    [string]$ContextPromotionPath = "src\xenia\cpu\compiler\passes\context_promotion_pass.cc",
    [string]$RegisterAllocationPath = "src\xenia\cpu\compiler\passes\register_allocation_pass.cc",
    [string]$PpcTranslatorPath = "src\xenia\cpu\ppc\ppc_translator.cc",
    [string]$A64EmitterPath = "src\xenia\cpu\backend\a64\a64_emitter.cc",
    [string]$A64EmitterHeaderPath = "src\xenia\cpu\backend\a64\a64_emitter.h",
    [string]$A64BackendPath = "src\xenia\cpu\backend\a64\a64_backend.cc",
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
        [string]$Pattern,
        [string]$Default = "-"
    )

    foreach ($line in $Lines) {
        if ($line -match $Pattern) {
            return $line.Trim()
        }
    }
    return $Default
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

function Get-Value {
    param(
        [string[]]$Lines,
        [string]$Pattern,
        [int64]$Default = 0
    )

    $match = Get-FirstMatch $Lines $Pattern
    if ($null -eq $match) {
        return $Default
    }
    return [int64]$match[1]
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

$parentLog = Resolve-RepoPath $ParentLogPath
foreach ($path in @(
        $parentLog,
        (Resolve-RepoPath $RegisterCacheGapScriptPath),
        (Resolve-RepoPath $StateSpanScriptPath),
        (Resolve-RepoPath $GprPromotionScriptPath))) {
    if (!(Test-Path -LiteralPath $path)) {
        throw "Required input not found: $path"
    }
}

$gapScript = Resolve-RepoPath $RegisterCacheGapScriptPath
$stateScript = Resolve-RepoPath $StateSpanScriptPath
$gprScript = Resolve-RepoPath $GprPromotionScriptPath

$gapOutput = @(& $gapScript -ParentLogPath $parentLog -ParentFunction $ParentFunction -Top $Top)
$stateOutput = @(& $stateScript -LogPath $parentLog -Function $ParentFunction -Phase OptHIR -Top $Top)
$gprOutput = @(& $gprScript -LogPath $parentLog -Function $ParentFunction -Phase OptHIR -Top $Top)

$contextPromotionText = Read-RequiredText $ContextPromotionPath
$contextPromotionLines = Read-RequiredLines $ContextPromotionPath
$registerAllocationText = Read-RequiredText $RegisterAllocationPath
$registerAllocationLines = Read-RequiredLines $RegisterAllocationPath
$translatorText = Read-RequiredText $PpcTranslatorPath
$translatorLines = Read-RequiredLines $PpcTranslatorPath
$emitterText = Read-RequiredText $A64EmitterPath
$emitterLines = Read-RequiredLines $A64EmitterPath
$emitterHeaderText = Read-RequiredText $A64EmitterHeaderPath
$emitterHeaderLines = Read-RequiredLines $A64EmitterHeaderPath
$backendText = Read-RequiredText $A64BackendPath
$backendLines = Read-RequiredLines $A64BackendPath

$contextPromotionRunLine = Find-Line $contextPromotionLines 'bool ContextPromotionPass::Run'
$promoteBlockLine = Find-Line $contextPromotionLines 'Process each block independently'
$localSlotLine = Find-Line $contextPromotionLines 'PromoteDominatedGprLocalSlots'
$liveInLine = Find-Line $contextPromotionLines 'PromoteGprLiveInR1'
$removeDeadStoresLine = Find-Line $contextPromotionLines 'RemoveDeadStoresBlock'
$raRunLine = Find-Line $registerAllocationLines 'bool RegisterAllocationPass::Run'
$raBlockLine = Find-Line $registerAllocationLines 'Registers do not move across blocks'
$raSpillLine = Find-Line $registerAllocationLines 'Spill required'
$translatorRaLine = Find-Line $translatorLines 'Register allocation for the target backend'
$emitterCacheLine = Find-Line $emitterLines 'A64ContextValueCache context_value_cache'
$emitterResetLine = Find-Line $emitterLines 'context_value_cache.ResetBlock'
$emitterAuditLine = Find-Line $emitterLines 'A64 context value cache: fn'
$gprCountLine = Find-Line $emitterHeaderLines 'GPR_COUNT\s*=\s*7'
$fixedRegLine = Find-Line $emitterHeaderLines 'Reserved: sp, x19'
$guestToHostLine = Find-Line $backendLines 'GPRs x19-x28 are callee-saved'

$gprAccesses = Get-ClassCount $stateOutput "GPR"
$vmxAccesses = Get-ClassCount $stateOutput "VMX"
$crAccesses = Get-ClassCount $stateOutput "CR"
$fprAccesses = Get-ClassCount $stateOutput "FPR"
$lrCtrAccesses = Get-ClassCount $stateOutput "LR/CTR"
$fpscrAccesses = Get-ClassCount $stateOutput "FP/VMX_STATUS"
$contextBarriers = Get-Value $stateOutput '^context_barriers=([0-9]+)$'
$wholeGprLoads = Get-Value $gprOutput '^whole_gpr_loads=([0-9]+)$'
$wholeGprStores = Get-Value $gprOutput '^whole_gpr_stores=([0-9]+)$'
$dominatedBlocks = Get-Value $gprOutput '^dominated_single_pred_blocks=([0-9]+)$'
$multiPredBlocks = Get-Value $gprOutput '^multi_pred_blocks=([0-9]+)$'
$firstLoads = Get-Value $gprOutput '^first_gpr_loads_in_dominated_single_pred_blocks=([0-9]+)$'
$promotionGate = Get-FirstLine $gprOutput '^promotion_gate='
$topStateCandidate = Get-FirstLine $stateOutput '^\+40 r\[1\] score='
$secondStateCandidate = Get-FirstLine $stateOutput '^\+120 r\[11\] score='
$topPreRaCandidate = Get-FirstLine $gprOutput '^\+40 r\[1\] score='

$edgeLine = Get-FirstLine $gapOutput '^  edge block='
$r3Plan = Get-FirstLine $gapOutput '^  plan_r3='
$f1Plan = Get-FirstLine $gapOutput '^  plan_f1='
$fpscrPlan = Get-FirstLine $gapOutput '^  plan_fpscr='
$lrPlan = Get-FirstLine $gapOutput '^  plan_lr='

$structuralCacheCvarExists = (
    (Test-Pattern $contextPromotionText 'arm64_guest_state_register_cache') -or
    (Test-Pattern $emitterText 'arm64_guest_state_register_cache') -or
    (Test-Pattern $backendText 'arm64_guest_state_register_cache'))
$emitTimeCacheExists = Test-Pattern $emitterText 'class A64ContextValueCache'
$emitTimeCacheResetsPerBlock = $emitterResetLine -gt 0
$contextPassPerBlock = $promoteBlockLine -gt 0
$raPerBlock = $raBlockLine -gt 0
$raCanSpill = $raSpillLine -gt 0
$hasFixedRegs = Test-Pattern $emitterHeaderText 'Reserved: sp, x19.*x20.*x21'
$hasSevenGprs = $gprCountLine -gt 0
$helperCallSites = Count-Pattern $emitterText 'TryEmit[A-Za-z0-9]+HelperCall'

Write-Output "A64 Guest-State Register Cache Design"
Write-Output ("repo={0}" -f $RepoRoot)
Write-Output ("parent_function={0} parent_log={1}" -f $ParentFunction, $parentLog)
Write-Output ("structural_cache_cvar_exists={0}" -f $structuralCacheCvarExists)
Write-Output ""

Write-Output "hot_route_state_traffic:"
Write-Output ("  accesses gpr={0} vmx={1} cr={2} fpr={3} lr_ctr={4} fp_vmx_status={5} context_barriers={6}" -f `
    $gprAccesses, $vmxAccesses, $crAccesses, $fprAccesses, $lrCtrAccesses, $fpscrAccesses, $contextBarriers)
Write-Output ("  whole_gpr_loads={0} whole_gpr_stores={1} dominated_single_pred_blocks={2} multi_pred_blocks={3} first_gpr_loads_in_dominated_single_pred_blocks={4}" -f `
    $wholeGprLoads, $wholeGprStores, $dominatedBlocks, $multiPredBlocks, $firstLoads)
Write-Output ("  top_state_candidate={0}" -f $topStateCandidate)
Write-Output ("  second_state_candidate={0}" -f $secondStateCandidate)
Write-Output ("  top_pre_ra_candidate={0}" -f $topPreRaCandidate)
Write-Output ("  promotion_gate={0}" -f $promotionGate)
Write-Output ("  {0}" -f $edgeLine.Trim())
Write-Output ""

Write-Output "source_boundary_checks:"
Write-Output ("  context_promotion source={0};{1};{2} per_block_mem2reg={3} local_slot_probe_exists={4} livein_probe_exists={5}" -f `
    (Format-Source $ContextPromotionPath $contextPromotionRunLine),
    (Format-Source $ContextPromotionPath $promoteBlockLine),
    (Format-Source $ContextPromotionPath $removeDeadStoresLine),
    $contextPassPerBlock, ($localSlotLine -gt 0), ($liveInLine -gt 0))
Write-Output ("  translator_order source={0} register_allocation_last_before_finalization={1}" -f `
    (Format-Source $PpcTranslatorPath $translatorRaLine),
    ($translatorRaLine -gt 0))
Write-Output ("  register_allocator source={0};{1};{2} per_block={3} spills_to_locals={4}" -f `
    (Format-Source $RegisterAllocationPath $raRunLine),
    (Format-Source $RegisterAllocationPath $raBlockLine),
    (Format-Source $RegisterAllocationPath $raSpillLine),
    $raPerBlock, $raCanSpill)
Write-Output ("  a64_emit_time_cache source={0};{1};{2} exists={3} resets_per_block={4}" -f `
    (Format-Source $A64EmitterPath $emitterCacheLine),
    (Format-Source $A64EmitterPath $emitterResetLine),
    (Format-Source $A64EmitterPath $emitterAuditLine),
    $emitTimeCacheExists, $emitTimeCacheResetsPerBlock)
Write-Output ("  a64_host_register_contract source={0};{1};{2} fixed_x19_x20_x21={3} allocatable_gprs_x22_x28={4}" -f `
    (Format-Source $A64EmitterHeaderPath $fixedRegLine),
    (Format-Source $A64EmitterHeaderPath $gprCountLine),
    (Format-Source $A64BackendPath $guestToHostLine),
    $hasFixedRegs, $hasSevenGprs)
Write-Output ("  helper_call_surface try_emit_helper_call_sites={0}" -f $helperCallSites)
Write-Output ""

Write-Output "candidate_payload_risks:"
Write-Output ("  {0}" -f $r3Plan.Trim())
Write-Output ("  {0}" -f $f1Plan.Trim())
Write-Output ("  {0}" -f $fpscrPlan.Trim())
Write-Output ("  {0}" -f $lrPlan.Trim())
Write-Output ""

Write-Output "design_options:"
Write-Output "  option name=hir_context_promotion_cfg_cache verdict=best_next_audit reason=sees LoadContext/StoreContext before RA and DCE; can count possible replacements without changing A64 emission; must add CFG dataflow and explicit flush/kills before behavior"
Write-Output "  option name=register_allocator_reserved_state_regs verdict=defer reason=RA is per-block and only has seven allocatable GPRs; pinning state registers here risks increasing spills unless a prior audit proves net pressure"
Write-Output "  option name=emit_time_context_value_cache verdict=rejected_for_next reason=existing A64ContextValueCache resets per block and prior 82282490 capture had 546 eligible loads with zero hits"
Write-Output "  option name=caller_local_edge_variant_payload verdict=parallel_lane reason=targets hot 82282490:82282598 -> 82287788 edge, but normal entry and indirection singleton contracts require caller-local or side-table storage with fallback counters"
Write-Output ""

Write-Output "counter_only_skeleton_contract:"
Write-Output "  cvar=arm64_guest_state_register_cache_audit default=false behavior_change=0 function_filter_required=true"
Write-Output "  first_fields=clean_int64_gpr_r1_and_r11_only"
Write-Output "  store_elision_allowed=0"
Write-Output "  context_load_replacement_allowed=0"
Write-Output "  payload_materialization_allowed=0"
Write-Output "  counters=candidate_loads,candidate_stores,clean_hits_possible,dirty_hits_possible,miss_no_entry,miss_multi_pred,miss_volatile,miss_overlap,flush_call,flush_helper,flush_branch,flush_label,flush_return,flush_trap,flush_external_visibility,estimated_spill_pressure,normal_fallback"
Write-Output "  flush_model=reset_on_calls_helpers_branches_labels_returns_traps_volatile_ops_overlapping_context_writes_and_external_context_visibility"
Write-Output "  audit_row=A64 guest-state register-cache audit: fn <guest> candidates=<delta>/<total> clean_hits_possible=<delta>/<total> flush_call=<delta>/<total> normal_fallback=<delta>/<total>"
Write-Output ""

Write-Output "decision=no_generated_behavior_patch"
Write-Output "reason=the correct next backend move is a no-behavior structural audit at the HIR/A64 register-allocation boundary; previous emit-time, local-slot, pinned-r1, f1, and fpscr behavior probes did not prove route speed."
Write-Output "next_slice=implement default-off counter-only guest-state register-cache audit skeleton, or take the parallel caller-local edge-variant payload storage skeleton if choosing the edge lane."
