param(
    [string]$RouteDocPath = "docs\research\20260525-134556-82287788-fpscr-cfg-carrier-design.md",
    [string]$MaturePatternsDocPath = "docs\research\20260525-143937-mature-a64-emulator-backend-patterns.md",
    [string]$WorklogPath = "docs\worklogs\20260525.md",
    [string]$A64EmitterHeaderPath = "src\xenia\cpu\backend\a64\a64_emitter.h",
    [string]$A64EmitterPath = "src\xenia\cpu\backend\a64\a64_emitter.cc",
    [string]$A64BackendPath = "src\xenia\cpu\backend\a64\a64_backend.cc",
    [string]$A64SequencesPath = "src\xenia\cpu\backend\a64\a64_sequences.cc",
    [string]$A64MemoryPath = "src\xenia\cpu\backend\a64\a64_seq_memory.cc",
    [string]$A64CodeCachePath = "src\xenia\cpu\backend\a64\a64_code_cache.cc",
    [string]$ContextPromotionPath = "src\xenia\cpu\compiler\passes\context_promotion_pass.cc",
    [string]$RegisterAllocationPath = "src\xenia\cpu\compiler\passes\register_allocation_pass.cc"
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

function Emit-Check {
    param(
        [string]$Subsystem,
        [string]$Status,
        [string]$Evidence,
        [string]$Source,
        [string]$Next
    )

    Write-Output ("gap subsystem={0} status={1} evidence={2} source={3} next={4}" -f
        $Subsystem, $Status, $Evidence, $Source, $Next)
}

$routeDoc = Read-RequiredText $RouteDocPath
$matureDoc = Read-RequiredText $MaturePatternsDocPath
$worklog = Read-RequiredText $WorklogPath
$routeEvidence = $routeDoc + "`n" + $worklog
$emitterHeader = Read-RequiredText $A64EmitterHeaderPath
$emitterHeaderLines = Read-RequiredLines $A64EmitterHeaderPath
$emitter = Read-RequiredText $A64EmitterPath
$emitterLines = Read-RequiredLines $A64EmitterPath
$backend = Read-RequiredText $A64BackendPath
$sequences = Read-RequiredText $A64SequencesPath
$memory = Read-RequiredText $A64MemoryPath
$codeCache = Read-RequiredText $A64CodeCachePath
$contextPromotion = Read-RequiredText $ContextPromotionPath
$registerAllocation = Read-RequiredText $RegisterAllocationPath

$contextRegLine = Find-Line $emitterHeaderLines 'GetContextReg\(\)\s+const\s+\{\s+return x20;'
$membaseRegLine = Find-Line $emitterHeaderLines 'GetMembaseReg\(\)\s+const\s+\{\s+return x21;'
$backendCtxLine = Find-Line $emitterHeaderLines 'GetBackendCtxReg\(\)\s+const\s+\{\s+return x19;'
$allocatableLine = Find-Line $emitterHeaderLines 'Available GPRs for register allocator: x22-x28'
$callLine = Find-Line $emitterLines 'void A64Emitter::Call'
$stackpointLine = Find-Line $emitterLines 'PushStackpoint\(\);'
$directMachineCodeLine = Find-Line $emitterLines 'fn->machine_code\(\)'
$resolveLine = Find-Line $emitterLines 'ResolveFunction'
$membaseLoadLine = Find-Line (Read-RequiredLines $A64MemoryPath) 'e\.ldr\(i\.dest, ptr\(e\.GetMembaseReg\(\), addr\)\)'
$mmioLine = Find-Line (Read-RequiredLines $A64MemoryPath) 'emit_inline_mmio_checks'
$codeCacheInitLine = Find-Line (Read-RequiredLines $A64CodeCachePath) 'CodeCacheBase::Initialize'
$contextCacheLine = Find-Line $emitterLines 'class A64ContextValueCache'
$pinnedR1Line = Find-Line $emitterLines 'pinned_r1'
$contextPromotionLine = Find-Line (Read-RequiredLines $ContextPromotionPath) 'arm64_context_promotion_gpr_livein_r1'
$regAllocLine = Find-Line (Read-RequiredLines $RegisterAllocationPath) 'Spill required'

$hasStaticRegs = $backendCtxLine -gt 0 -and $contextRegLine -gt 0 -and $membaseRegLine -gt 0
$hasSmallAllocSet = $allocatableLine -gt 0
$hasContextCache = Test-Pattern $emitter 'class A64ContextValueCache'
$hasLiveInR1 = Test-Pattern $contextPromotion 'arm64_context_promotion_gpr_livein_r1'
$hasSpillAllocator = Test-Pattern $registerAllocation 'Spill required'
$hasDirectCall = Test-Pattern $emitter 'fn->machine_code\(\).*?blr\(x9\);'
$hasResolveCall = Test-Pattern $emitter 'ResolveFunction'
$hasDirectMemory = Test-Pattern $memory 'ptr\(e\.GetMembaseReg\(\), addr\)'
$hasInlineMmio = Test-Pattern $memory 'emit_inline_mmio_checks'
$hasFpcrTracking = Test-Pattern $emitter 'ChangeFpcrMode' -and Test-Pattern $sequences 'FlushDenormals_V128'
$hasFpscrCfgAudit = Test-Pattern $sequences 'EmitBlueDragonFpscrCfgWritebackLoadAudit'
$hasCodeCacheOnlyBase = Test-Pattern $codeCache 'CodeCacheBase::Initialize\(\)'
$hasRouteCpuWall = (
    (Test-Pattern $routeEvidence 'Main Thread.*92\.3%') -or
    (Test-Pattern $routeEvidence 'CPU/JIT') -or
    (Test-Pattern $routeEvidence '82282490=33811022'))

Write-Output "A64 Mature Backend Gap Report"
Write-Output ("repo={0}" -f $RepoRoot)
Write-Output ("route_doc={0}" -f $RouteDocPath)
Write-Output ("worklog={0}" -f $WorklogPath)
Write-Output ("mature_patterns_doc={0}" -f $MaturePatternsDocPath)
Write-Output ("route_cpu_wall={0}" -f $hasRouteCpuWall)
Write-Output ""

Emit-Check `
    -Subsystem "static_host_register_contract" `
    -Status $(if ($hasStaticRegs -and $hasSmallAllocSet) { "partial" } else { "missing" }) `
    -Evidence "x19_backend_ctx=$($backendCtxLine -gt 0); x20_context=$($contextRegLine -gt 0); x21_membase=$($membaseRegLine -gt 0); allocatable_gprs=x22-x28_only=$hasSmallAllocSet" `
    -Source (Format-Source $A64EmitterHeaderPath $allocatableLine) `
    -Next "Keep x19/x20/x21, but audit whether only seven allocatable GPRs plus scratch-heavy helpers are forcing avoidable context spills in 82282490/82287788."

Emit-Check `
    -Subsystem "guest_register_cache" `
    -Status $(if ($hasContextCache -and $hasLiveInR1) { "weak_negative" } else { "missing" }) `
    -Evidence "emit_time_context_cache=$hasContextCache; livein_r1_pass=$hasLiveInR1; allocator_spills_supported=$hasSpillAllocator; previous r1/cache probes were not speed wins" `
    -Source (Format-Source $A64EmitterPath $contextCacheLine) `
    -Next "Build a structural register-cache report over hot HIR spans before another local store/load fold; candidate should operate before/with register allocation, not as an emit-time one-slot patch."

Emit-Check `
    -Subsystem "helper_abi_and_stackpoints" `
    -Status "partial_hot" `
    -Evidence "direct_call_site=$($callLine -gt 0); stackpoint_push=$($stackpointLine -gt 0); helper_inlining_sites=$(Count-Pattern $emitter 'TryEmit[A-Za-z0-9]+HelperCall'); direct_call_abi_guest_return_only=$hasDirectCall" `
    -Source (Format-Source $A64EmitterPath $callLine) `
    -Next "Audit call-clobber and save/restore pressure for the 82282490 -> 82287788 edge; prefer caller-local/edge-variant storage only after proving payload lifetime and fallbacks."

Emit-Check `
    -Subsystem "block_linking_and_dispatch" `
    -Status $(if ($hasDirectCall -and $hasResolveCall) { "partial_blocked_by_singleton_entry" } else { "missing" }) `
    -Evidence "direct_machine_code_calls=$hasDirectCall; resolve_function_path=$hasResolveCall; latest edge-variant audits say normal entry and indirection are guest-address singleton paths" `
    -Source (Format-Source $A64EmitterPath $directMachineCodeLine) `
    -Next "Return to caller-local or side-table edge-variant storage for 82282490:82282598 -> 82287788 if the maturity report chooses block-linking over fpscr-local caching."

Emit-Check `
    -Subsystem "fastmem_and_fault_fallback" `
    -Status $(if ($hasDirectMemory -and $hasInlineMmio) { "partial" } else { "missing" }) `
    -Evidence "direct_membase_load_store=$hasDirectMemory; inline_mmio_flag=$hasInlineMmio; code_cache_base_only=$hasCodeCacheOnlyBase" `
    -Source (Format-Source $A64MemoryPath $membaseLoadLine) `
    -Next "Do not assume memory is the Blue Dragon wall yet; add a hot-route memory/MMIO/fault fallback counter only if body-time points at memory lowering."

Emit-Check `
    -Subsystem "vector_fp_fpscr_lowering" `
    -Status $(if ($hasFpcrTracking -and $hasFpscrCfgAudit) { "hot_partial" } else { "missing" }) `
    -Evidence "fpcr_mode_tracking=$hasFpcrTracking; fpscr_cfg_audit=$hasFpscrCfgAudit; latest fpscr rows show millions of loads/stores but behavior patch is blocked by CFG writebacks" `
    -Source (Format-Source $A64SequencesPath 0) `
    -Next "Keep fpscr behavior unchanged until a carrier design proves normal-entry fallback, required writebacks, and payload materialization; no quiet A/B before nonzero intended payload."

Emit-Check `
    -Subsystem "deterministic_backend_harness" `
    -Status "missing_for_mature_changes" `
    -Evidence "offline Thor reports exist, but no focused A64 backend unit harness is tied to register-cache/block-linking/fpscr carrier behavior yet" `
    -Source $MaturePatternsDocPath `
    -Next "For structural backend patches, add a compile/offline deterministic test or verifier before the Thor route capture."

Write-Output ""
Write-Output "ranked_next_lanes:"
Write-Output "1. register_cache_gap_report_for_82282490_82287788: largest maturity gap; current emit-time/local-slot probes were negative, so design before code."
Write-Output "2. edge_variant_storage_design_for_82282490_82282598_to_82287788: directly matches hot edge, but needs caller-local/side-table storage and fallback proof."
Write-Output "3. fpscr_cfg_carrier_skeleton: viable only as default-off counter/payload proof; behavior and quiet A/B remain blocked."
Write-Output "4. fastmem_or_vulkan: not next unless fresh captures move the wall away from Main Thread/A64 generated code."
