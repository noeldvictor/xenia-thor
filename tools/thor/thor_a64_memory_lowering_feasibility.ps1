param(
    [string]$RepoRoot = ".",
    [string]$OffsetAuditReport = "scratch/thor-debug/20260526-063600-82281d28-load-store-offset-audit.txt",
    [string]$OutputPath = "scratch/thor-debug/20260526-070000-a64-memory-lowering-feasibility.txt"
)

$ErrorActionPreference = "Stop"

function Read-RepoFile {
    param(
        [string]$Root,
        [string]$RelativePath
    )
    $path = Join-Path $Root $RelativePath
    if (!(Test-Path -LiteralPath $path)) {
        throw "Required source file not found: $RelativePath"
    }
    return Get-Content -LiteralPath $path -Raw
}

function Test-Pattern {
    param(
        [string]$Text,
        [string]$Pattern
    )
    return [regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
}

function Read-KeyValueReport {
    param([string]$Path)
    $values = @{}
    if (!(Test-Path -LiteralPath $Path)) {
        return $values
    }
    Get-Content -LiteralPath $Path | ForEach-Object {
        $line = $_.Trim()
        if ($line -match "^(?<key>[A-Za-z0-9_]+)=(?<value>.+)$") {
            $values[$Matches["key"]] = $Matches["value"].Trim()
        }
    }
    return $values
}

function Format-Value {
    param(
        [hashtable]$Values,
        [string]$Key
    )
    if ($Values.ContainsKey($Key)) {
        return $Values[$Key]
    }
    return "unknown"
}

$repo = (Resolve-Path -LiteralPath $RepoRoot).Path
$a64UtilPath = "src/xenia/cpu/backend/a64/a64_seq_util.h"
$a64MemoryPath = "src/xenia/cpu/backend/a64/a64_seq_memory.cc"
$x64MemoryPath = "src/xenia/cpu/backend/x64/x64_seq_memory.cc"
$posixMemoryPath = "src/xenia/base/memory_posix.cc"
$androidActivityPath = "android/android_studio_project/app/src/main/java/jp/xenia/emulator/EmulatorActivity.java"
$thorDebugPath = "tools/thor/thor_xenia_debug.ps1"

$a64Util = Read-RepoFile $repo $a64UtilPath
$a64Memory = Read-RepoFile $repo $a64MemoryPath
$x64Memory = Read-RepoFile $repo $x64MemoryPath
$posixMemory = Read-RepoFile $repo $posixMemoryPath
$androidActivity = Read-RepoFile $repo $androidActivityPath
$thorDebug = Read-RepoFile $repo $thorDebugPath

$offsetReportPath = Join-Path $repo $OffsetAuditReport
$offsetValues = Read-KeyValueReport $offsetReportPath

$checks = [ordered]@{
    a64_compute_memory_address = (Test-Pattern $a64Util "inline\s+XReg\s+ComputeMemoryAddress")
    a64_zero_extend_nonconstant_guest = (Test-Pattern $a64Util "e\.mov\(e\.w0,\s*WReg\(src\.getIdx\(\)\)\)")
    a64_granularity_adjust = (Test-Pattern $a64Util "allocation_granularity\(\)\s*>\s*0x1000")
    a64_add_guest_memory_offset = (Test-Pattern $a64Util "inline\s+XReg\s+AddGuestMemoryOffset")
    a64_32bit_wrap_comment = (Test-Pattern $a64Util "Guest address arithmetic wraps at 32 bits")
    a64_small_imm_offset_add = (Test-Pattern $a64Util "e\.add\(e\.w0,\s*e\.w0,\s*value\)")
    a64_shifted_imm_offset_add = (Test-Pattern $a64Util "e\.add\(e\.w0,\s*e\.w0,\s*value\s*>>\s*12,\s*12\)")
    a64_load_offset_uses_two_step = (Test-Pattern $a64Memory "AddGuestMemoryOffset\(e,\s*ComputeMemoryAddress\(e,\s*i\.src1\),\s*i\.src2\)")
    a64_offset_helper_present = (Test-Pattern $a64Util "ComputeMemoryAddressOffset")
    a64_offset_helper_fallback = (Test-Pattern $a64Util "return\s+AddGuestMemoryOffset\(e,\s*ComputeMemoryAddress\(e,\s*guest\),\s*offset\)")
    a64_offset_helper_granularity_fallback = (Test-Pattern $a64Util "allocation_granularity\(\)\s*>\s*0x1000")
    a64_offset_helper_preserves_x0 = (Test-Pattern $a64Util "return\s+e\.x0;")
    a64_offset_fastpath_cvar = (Test-Pattern $a64Memory "DEFINE_bool\(arm64_offset_memory_address_fastpath")
    a64_offset_fastpath_gate = (Test-Pattern $a64Memory "cvars::arm64_offset_memory_address_fastpath")
    a64_load_offset_uses_offset_helper = (Test-Pattern $a64Memory "ComputeOffsetMemoryAddress\(e,\s*i\.src1,\s*i\.src2\)")
    a64_mmio_helper_path = (Test-Pattern $a64Memory "IsPossibleMMIOInstruction\(e,\s*i\.instr\)")
    a64_inline_mmio_path = (Test-Pattern $a64Memory "cvars::emit_inline_mmio_checks")
    a64_store_watch_keeps_guest_address = (Test-Pattern $a64Memory "EmitGuestStoreWatch\(e,\s*i\.instr,\s*e\.x0,\s*4\)")
    android_extra_present = (Test-Pattern $androidActivity "arm64_offset_memory_address_fastpath")
    thor_launcher_param_present = (Test-Pattern $thorDebug '\$Arm64OffsetMemoryAddressFastpath')
    thor_launcher_extra_present = (Test-Pattern $thorDebug "arm64_offset_memory_address_fastpath")
    thor_capture_metadata_present = (Test-Pattern $thorDebug 'arm64_offset_memory_address_fastpath=\$Arm64OffsetMemoryAddressFastpath')
    x64_offset_helper_reference = (Test-Pattern $x64Memory "ComputeMemoryAddressOffset")
    x64_offset_granularity_threshold = (Test-Pattern $x64Memory "0xE0000000\s*-\s*offset_const")
    posix_allocation_granularity_is_page_size = (Test-Pattern $posixMemory "allocation_granularity\(\)\s*\{\s*return\s+page_size\(\);")
}

$requiredChecks = @(
    "a64_compute_memory_address",
    "a64_zero_extend_nonconstant_guest",
    "a64_granularity_adjust",
    "a64_add_guest_memory_offset",
    "a64_32bit_wrap_comment",
    "a64_mmio_helper_path",
    "a64_inline_mmio_path",
    "a64_store_watch_keeps_guest_address",
    "x64_offset_helper_reference",
    "x64_offset_granularity_threshold",
    "posix_allocation_granularity_is_page_size"
)

$missingRequired = @()
foreach ($key in $requiredChecks) {
    if (!$checks[$key]) {
        $missingRequired += $key
    }
}

$hasOldTwoStepLowering = $checks["a64_load_offset_uses_two_step"]
$hasDefaultOffPrototype = $checks["a64_offset_helper_present"] -and
    $checks["a64_offset_helper_fallback"] -and
    $checks["a64_offset_helper_granularity_fallback"] -and
    $checks["a64_offset_helper_preserves_x0"] -and
    $checks["a64_offset_fastpath_cvar"] -and
    $checks["a64_offset_fastpath_gate"] -and
    $checks["a64_load_offset_uses_offset_helper"] -and
    $checks["android_extra_present"] -and
    $checks["thor_launcher_param_present"] -and
    $checks["thor_launcher_extra_present"] -and
    $checks["thor_capture_metadata_present"]
$hasLegalCandidate = ($missingRequired.Count -eq 0) -and ($hasOldTwoStepLowering -or $hasDefaultOffPrototype)
$hostPointerFoldBlocked = $true
$mmioBlocksUnconditionalFold = $checks["a64_mmio_helper_path"] -and $checks["a64_inline_mmio_path"]
$storeWatchRequiresX0 = $checks["a64_store_watch_keeps_guest_address"]

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("A64 memory lowering feasibility audit")
$lines.Add("repo=$repo")
$lines.Add("offset_report=$offsetReportPath")
$lines.Add("")
$lines.Add("route_volume:")
$lines.Add(("  offset_ops={0}" -f (Format-Value $offsetValues "offset_ops")))
$lines.Add(("  load_offset_ops={0}" -f (Format-Value $offsetValues "load_offset_ops")))
$lines.Add(("  store_offset_ops={0}" -f (Format-Value $offsetValues "store_offset_ops")))
$lines.Add(("  body_weighted_offset_ops={0}" -f (Format-Value $offsetValues "body_weighted_offset_ops")))
$lines.Add(("  body_weighted_guest_stack_ops={0}" -f (Format-Value $offsetValues "body_weighted_guest_stack_ops")))
$lines.Add(("  body_weighted_context_gpr_ops={0}" -f (Format-Value $offsetValues "body_weighted_context_gpr_ops")))
$lines.Add("")
$lines.Add("source_checks:")
foreach ($entry in $checks.GetEnumerator()) {
    $lines.Add(("  {0}={1}" -f $entry.Key, ([int]$entry.Value)))
}
$lines.Add("")
$lines.Add("decision:")
$lines.Add(("  legal_offset_helper_candidate={0}" -f ([int]$hasLegalCandidate)))
$lines.Add(("  old_two_step_lowering_present={0}" -f ([int]$hasOldTwoStepLowering)))
$lines.Add(("  default_off_offset_helper_prototype={0}" -f ([int]$hasDefaultOffPrototype)))
$lines.Add(("  current_a64_lowering=default_off_offset_helper_or_ComputeMemoryAddress_plus_AddGuestMemoryOffset_then_membase_x0"))
$lines.Add(("  x64_reference_offset_helper={0}" -f ([int]$checks["x64_offset_helper_reference"])))
$lines.Add(("  candidate_fold_add_base_offset=possible_for_constant_offsets_if_x0_remains_final_32bit_guest_address"))
$lines.Add(("  candidate_host_pointer_fold_blocked={0}" -f ([int]$hostPointerFoldBlocked)))
$lines.Add(("  unconditional_mmio_fold_blocked={0}" -f ([int]$mmioBlocksUnconditionalFold)))
$lines.Add(("  store_watch_requires_x0_final_guest_address={0}" -f ([int]$storeWatchRequiresX0)))
$lines.Add(("  payload_materializations_allowed=0"))
$lines.Add(("  behavior_changed=default_off_only"))
$lines.Add("")
$lines.Add("required_preservation:")
$lines.Add("  guest_address_wrap=32bit_before_membase")
$lines.Add("  membase_application=after_guest_address_math")
$lines.Add("  byte_swap=unchanged")
$lines.Add("  mmio_exception_visibility=unchanged_or_slow_path")
$lines.Add("  store_watch_x0=final_guest_address")
$lines.Add("  granularity_gt_4k=preserve_x64_threshold_semantics_or_gate_fast_path")
$lines.Add("")
if (!$hasLegalCandidate) {
    $lines.Add("blocker:")
    if ($missingRequired.Count -gt 0) {
        $lines.Add(("  missing_required_checks={0}" -f ($missingRequired -join ",")))
    }
    if (!$hasOldTwoStepLowering -and !$hasDefaultOffPrototype) {
        $lines.Add("  missing_lowering_shape=old_two_step_or_default_off_offset_helper")
    }
} else {
    $lines.Add("next_slice:")
    if ($hasDefaultOffPrototype) {
        $lines.Add("  FullDeploy, then route-clean Thor capture with -Arm64OffsetMemoryAddressFastpath true and delayed body-time comparators 82282490,82281D28,82287788.")
        $lines.Add("  Treat this as route-safety proof only; do not run a quiet speed A/B until the enabled path reaches opening sky/dragon-wing with clean fatal markers.")
    } else {
        $lines.Add("  Implement no behavior in this audit slice.")
        $lines.Add("  A defensible future behavior slice is a default-off A64 offset-aware address helper, modeled on x64 ComputeMemoryAddressOffset, limited first to normal non-MMIO LOAD_OFFSET/STORE_OFFSET constant-offset paths and preserving x0 as the final 32-bit guest address.")
        $lines.Add("  For allocation_granularity() > 0x1000, copy the x64 threshold rule using 0xE0000000 - offset_const or keep the existing two-step path.")
        $lines.Add("  Do not replace guest 32-bit address math with host pointer plus immediate addressing without a no-wrap proof.")
    }
}

$resolvedOutput = Join-Path $repo $OutputPath
$outputDir = Split-Path -Parent $resolvedOutput
if (!(Test-Path -LiteralPath $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}
$lines | Set-Content -LiteralPath $resolvedOutput -Encoding ASCII
$lines
