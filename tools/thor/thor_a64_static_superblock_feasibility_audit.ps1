[CmdletBinding()]
param(
    [string[]]$CodegenAuditPaths = @(
        "scratch\thor-debug\20260526-080921-82282490-822825e0-codegen-audit.txt",
        "scratch\thor-debug\20260526-054200-82281d28-hot-block-codegen-audit.txt",
        "scratch\thor-debug\20260526-080921-82282490-8228252c-codegen-audit.txt",
        "scratch\thor-debug\20260526-080921-82287788-822877bc-codegen-audit.txt"
    ),
    [string]$A64FunctionHeaderPath = "src\xenia\cpu\backend\a64\a64_function.h",
    [string]$A64FunctionSourcePath = "src\xenia\cpu\backend\a64\a64_function.cc",
    [string]$A64EmitterPath = "src\xenia\cpu\backend\a64\a64_emitter.cc",
    [string]$A64SeqControlPath = "src\xenia\cpu\backend\a64\a64_seq_control.cc",
    [string]$A64BackendPath = "src\xenia\cpu\backend\a64\a64_backend.cc",
    [string]$A64BackendHeaderPath = "src\xenia\cpu\backend\a64\a64_backend.h",
    [string]$FunctionHeaderPath = "src\xenia\cpu\function.h",
    [string]$FunctionSourcePath = "src\xenia\cpu\function.cc",
    [string]$ScalarContextReportPath = "docs\research\20260526-115700-a64-scalar-context-load-store-lowering.md",
    [string]$FastEntryEmitterPlanningPath = "docs\research\20260526-111500-a64-fast-entry-emitter-planning.md",
    [string]$NoWrapReportPath = "docs\research\20260526-083000-a64-no-wrap-memory-eligibility-audit.md",
    [string]$NonclosedCacheReportPath = "docs\research\20260526-092500-a64-nonclosed-gpr-cache-capture.md",
    [string]$VmxRouteReportPath = "docs\research\20260526-012000-vmx128-route-stabilized-counters.md"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return (Join-Path $repoRoot $Path)
}

function Read-RequiredText {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        throw "Required path not found: $Path"
    }
    return Get-Content -LiteralPath $resolved -Raw
}

function Read-OptionalText {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        return ""
    }
    return Get-Content -LiteralPath $resolved -Raw
}

function Parse-PairLine {
    param([string]$Line)

    $result = @{}
    if ([string]::IsNullOrWhiteSpace($Line) -or $Line -notmatch '=') {
        return $result
    }
    $right = $Line.Substring($Line.IndexOf('=') + 1)
    foreach ($part in ($right -split ',')) {
        $trimmed = $part.Trim()
        if ($trimmed -match '^(?<key>[^:]+):(?<value>\d+)$') {
            $result[$Matches.key.Trim()] = [int64]$Matches.value
        }
    }
    return $result
}

function Sum-Matching {
    param(
        [hashtable]$Table,
        [string]$Pattern
    )

    $sum = [int64]0
    foreach ($entry in $Table.GetEnumerator()) {
        if ($entry.Name -match $Pattern) {
            $sum += [int64]$entry.Value
        }
    }
    return $sum
}

function Format-TopPairs {
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

function Parse-CodegenAudit {
    param([string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (!(Test-Path -LiteralPath $resolved)) {
        throw "Codegen audit not found: $Path"
    }

    $lines = Get-Content -LiteralPath $resolved
    $function = ""
    $slice = ""
    $bodyTicks = [int64]0
    $estimatedFloor = [int64]0
    $ppcOps = @{}
    $hirOps = @{}
    $contextStores = @{}
    $contextBarriers = [int64]0
    $calls = [int64]0
    $branches = [int64]0

    foreach ($line in $lines) {
        if ($line -match '^function=(?<value>[0-9A-Fa-f]+)') {
            $function = $Matches.value.ToUpperInvariant()
            continue
        }
        if ($line -match '^slice=(?<value>[0-9A-Fa-f]+-[0-9A-Fa-f]+)') {
            $slice = $Matches.value.ToUpperInvariant()
            continue
        }
        if ($line -match '^body_ticks_total=(?<value>\d+)') {
            $bodyTicks = [int64]$Matches.value
            continue
        }
        if ($line -match '^estimated_floor_total=(?<value>\d+)') {
            $estimatedFloor = [int64]$Matches.value
            continue
        }
        if ($line -match '^ppc_ops=') {
            $ppcOps = Parse-PairLine $line
            continue
        }
        if ($line -match '^hir_ops=') {
            $hirOps = Parse-PairLine $line
            continue
        }
        if ($line -match '^context_stores=') {
            $contextStores = Parse-PairLine $line
            continue
        }
        if ($line -match '^context_barrier_note=count:(?<value>\d+)') {
            $contextBarriers = [int64]$Matches.value
            continue
        }
        if ($line -match '^context_barriers=(?<barriers>\d+)\s+calls=(?<calls>\d+)\s+branches=(?<branches>\d+)') {
            $contextBarriers = [int64]$Matches.barriers
            $calls = [int64]$Matches.calls
            $branches = [int64]$Matches.branches
            continue
        }
    }

    if ([string]::IsNullOrWhiteSpace($function) -or
        [string]::IsNullOrWhiteSpace($slice) -or
        $bodyTicks -le 0) {
        throw "Could not parse required fields from $Path"
    }

    $loadContext = Sum-Matching $hirOps '^load_context$'
    $storeContext = Sum-Matching $hirOps '^store_context$'
    $loadOffset = Sum-Matching $hirOps '^load_offset\.'
    $storeOffset = Sum-Matching $hirOps '^store_offset\.'
    $loadMemory = Sum-Matching $hirOps '^load\.'
    $storeMemory = Sum-Matching $hirOps '^store\.'
    $compare = Sum-Matching $hirOps '^compare'
    $branch = Sum-Matching $hirOps '^branch'
    if ($branches -gt 0) {
        $branch = $branches
    }
    $call = Sum-Matching $hirOps '^call$'
    if ($calls -gt 0) {
        $call = $calls
    }
    $crStores = Sum-Matching $contextStores '^cr'
    $gprStores = Sum-Matching $contextStores '^r\['
    $lrStores = Sum-Matching $contextStores '^lr$'
    $vmxOps =
        (Sum-Matching $hirOps '^mul_add_v128$') +
        (Sum-Matching $hirOps '^extract') +
        (Sum-Matching $hirOps '^splat') +
        (Sum-Matching $hirOps '^permute$') +
        (Sum-Matching $hirOps '^load_vector')

    $status = "requires_ir_level_visibility_design"
    $reason = "block has guest-visible state or control-flow contracts that are not represented by a static-superblock entry"
    if ($function -eq "82281D28" -and $slice -match '^8228233C-8228237') {
        $status = "blocked_by_direct_guest_calls_stackpoints_and_dirty_state"
        $reason = "hot block contains two direct guest calls, context barriers, guest-stack loads, and call live-in context stores"
    } elseif ($function -eq "82282490" -and $slice -eq "8228252C-822825C4") {
        $status = "closed_local_vmx_shape_not_static_superblock_candidate"
        $reason = "block is the already closed stvewx/MUL_ADD_V128/fpscr local VMX shape"
    } elseif ($crStores -gt 0 -and $branch -gt 0 -and $contextBarriers -gt 0) {
        $status = "blocked_by_cr_branch_barrier_visibility"
        $reason = "CR store/branch across context barriers is covered by prior crash-gated closure, not a safe superblock shortcut"
    }

    return [pscustomobject][ordered]@{
        path = $Path
        function = $function
        slice = $slice
        body_ticks_total = $bodyTicks
        estimated_floor_total = $estimatedFloor
        load_context = $loadContext
        store_context = $storeContext
        load_offset = $loadOffset
        store_offset = $storeOffset
        load_memory = $loadMemory
        store_memory = $storeMemory
        compare = $compare
        branch = $branch
        call = $call
        context_barriers = $contextBarriers
        cr_stores = $crStores
        gpr_stores = $gprStores
        lr_stores = $lrStores
        vmx_ops = $vmxOps
        ppc_ops = Format-TopPairs $ppcOps 12
        hir_ops = Format-TopPairs $hirOps 12
        status = $status
        reason = $reason
    }
}

$a64FunctionHeader = Read-RequiredText $A64FunctionHeaderPath
$a64FunctionSource = Read-RequiredText $A64FunctionSourcePath
$a64Emitter = Read-RequiredText $A64EmitterPath
$a64SeqControl = Read-RequiredText $A64SeqControlPath
$a64Backend = Read-RequiredText $A64BackendPath
$a64BackendHeader = Read-RequiredText $A64BackendHeaderPath
$functionHeader = Read-RequiredText $FunctionHeaderPath
$functionSource = Read-RequiredText $FunctionSourcePath
$a64Source = $a64FunctionHeader + "`n" + $a64FunctionSource + "`n" +
    $a64Emitter + "`n" + $a64SeqControl + "`n" + $a64Backend + "`n" +
    $a64BackendHeader

$scalarContextText = Read-OptionalText $ScalarContextReportPath
$fastEntryText = Read-OptionalText $FastEntryEmitterPlanningPath
$noWrapText = Read-OptionalText $NoWrapReportPath
$nonclosedText = Read-OptionalText $NonclosedCacheReportPath
$vmxText = Read-OptionalText $VmxRouteReportPath

$blocks = @()
foreach ($path in $CodegenAuditPaths) {
    $blocks += Parse-CodegenAudit $path
}

$sourceChecks = [ordered]@{
    normal_machine_code_single_entry_present = (
        $a64FunctionHeader -match 'uint8_t\*\s+machine_code\(\)\s+const\s+override\s*\{\s*return\s+machine_code_\.load'
    )
    fast_entry_storage_source_only_present = (
        $a64FunctionHeader -match 'guest_call_fast_entry_code_' -and
        $a64FunctionHeader -match 'SetupGuestCallFastEntryStubSkeleton'
    )
    fast_entry_codegen_behavior_absent = (
        $fastEntryText -match 'emitter_planning_source_ready_behavior_blocked' -and
        $a64Emitter -notmatch 'guest_call_fast_entry_code\(\)' -and
        $a64SeqControl -notmatch 'GuestCallFastEntry'
    )
    direct_guest_calls_use_normal_machine_code = (
        $a64Emitter -match 'fn->machine_code\(\)' -and
        $a64Emitter -match 'Direct call' -and
        $a64Emitter -match 'blr\(x9\)'
    )
    direct_guest_call_payload_is_only_return_address = (
        $a64Emitter -match "Pass the next call's guest return address in x0" -and
        $a64Emitter -match 'GUEST_CALL_RET_ADDR'
    )
    unresolved_targets_resolve_to_machine_code = (
        $a64Backend -match 'ResolveFunction' -and
        $a64Backend -match 'guest_fn->machine_code\(\)'
    )
    stackpoint_longjmp_visibility_present = (
        $a64BackendHeader -match 'A64BackendStackpoint' -and
        $a64Backend -match 'EmitGuestAndHostSynchronizeStackHelper' -and
        $a64Emitter -match 'EnsureSynchronizedGuestAndHostStack'
    )
    source_map_debug_exception_visibility_present = (
        $functionHeader -match 'SourceMapEntry' -and
        $functionSource -match 'MapGuestAddressToMachineCode' -and
        $a64Emitter -match 'MarkSourceOffset' -and
        $a64Emitter -match 'source_map_arena_' -and
        $a64SeqControl -match 'DebugBreak' -and
        $a64SeqControl -match 'Trap' -and
        $a64Backend -match 'ExceptionCallback'
    )
    static_superblock_storage_absent = (
        $a64Source -notmatch 'A64StaticSuperblock' -and
        $a64Source -notmatch 'multi_block_entry' -and
        $a64Source -notmatch 'static_superblock'
    )
    closed_micro_lanes_present = (
        $scalarContextText -match 'decision=close_scalar_context_load_store_behavior_for_current_route' -and
        $noWrapText -match 'runtime_no_wrap_proven_rows=0' -and
        $nonclosedText -match 'clean_hits_possible=0' -and
        ($vmxText -match 'closes broad PERMUTE' -or $vmxText -match 'Do not patch broad VMX128')
    )
}

$totalBodyTicks = [int64](($blocks | Measure-Object -Property body_ticks_total -Sum).Sum)
$totalEstimatedFloor = [int64](($blocks | Measure-Object -Property estimated_floor_total -Sum).Sum)
$totalCalls = [int64](($blocks | Measure-Object -Property call -Sum).Sum)
$totalBranches = [int64](($blocks | Measure-Object -Property branch -Sum).Sum)
$totalBarriers = [int64](($blocks | Measure-Object -Property context_barriers -Sum).Sum)
$totalCrStores = [int64](($blocks | Measure-Object -Property cr_stores -Sum).Sum)
$totalGprStores = [int64](($blocks | Measure-Object -Property gpr_stores -Sum).Sum)
$totalVmxOps = [int64](($blocks | Measure-Object -Property vmx_ops -Sum).Sum)

Write-Output "audit=a64_static_superblock_feasibility"
Write-Output ("input_blocks={0} total_body_ticks={1} total_estimated_floor={2}" -f `
    $blocks.Count,
    $totalBodyTicks,
    $totalEstimatedFloor)

foreach ($entry in $sourceChecks.GetEnumerator()) {
    Write-Output ("source_check {0}={1}" -f $entry.Key, $entry.Value.ToString().ToLowerInvariant())
}

foreach ($block in ($blocks | Sort-Object -Property @{ Expression = "body_ticks_total"; Descending = $true })) {
    Write-Output ("block function={0} slice={1} body_ticks_total={2} estimated_floor_total={3} calls={4} branches={5} context_barriers={6} cr_stores={7} gpr_stores={8} lr_stores={9} load_context={10} store_context={11} load_offset={12} store_offset={13} load_memory={14} store_memory={15} vmx_ops={16} status={17} reason={18} ppc_ops={19} hir_ops={20}" -f `
        $block.function,
        $block.slice,
        $block.body_ticks_total,
        $block.estimated_floor_total,
        $block.call,
        $block.branch,
        $block.context_barriers,
        $block.cr_stores,
        $block.gpr_stores,
        $block.lr_stores,
        $block.load_context,
        $block.store_context,
        $block.load_offset,
        $block.store_offset,
        $block.load_memory,
        $block.store_memory,
        $block.vmx_ops,
        $block.status,
        $block.reason,
        $block.ppc_ops,
        $block.hir_ops)
}

Write-Output ("route_totals calls={0} branches={1} context_barriers={2} cr_stores={3} gpr_stores={4} vmx_ops={5}" -f `
    $totalCalls,
    $totalBranches,
    $totalBarriers,
    $totalCrStores,
    $totalGprStores,
    $totalVmxOps)

$allSourceChecksPass = $true
foreach ($entry in $sourceChecks.GetEnumerator()) {
    if (-not [bool]$entry.Value) {
        $allSourceChecksPass = $false
        break
    }
}

$blockedBlockCount = @($blocks | Where-Object {
        $_.status -ne "requires_ir_level_visibility_design" -or
        $_.call -gt 0 -or $_.branch -gt 0 -or $_.context_barriers -gt 0
    }).Count

if ($allSourceChecksPass -and $blockedBlockCount -eq $blocks.Count) {
    Write-Output "decision=static_superblock_behavior_blocked_pending_ir_level_design"
    Write-Output "safe_next_patch=run_continual_harness_rerank_or_switch_structural_class"
    Write-Output "required_contracts=per_guest_pc_source_map;normal_entry_fallback;late_bound_target_fallback;stackpoint_resume;debug_exception_trap_visibility;dirty_flush_for_context_cr_fpscr_gpr_vmx;guest_stack_longjmp_recovery;no_hidden_guest_visible_state"
    Write-Output "do_not_patch=multi_block_fusion;alternate_entry_behavior;direct_call_fast_entry;global_indirection_rewrite;context_barrier_fusion;cr_branch_barrier_fusion;state_payload_materialization;speed_ab"
} else {
    Write-Output "decision=needs_manual_review_before_static_superblock_design"
    Write-Output "safe_next_patch=inspect_failed_source_checks_or_missing_block_evidence"
}
