/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <atomic>
#include <cstring>
#include <limits>

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

DEFINE_bool(
    xboxkrnl_tolerate_debug_memory, true,
    "Research bring-up: log nonzero DebugMemory flags in Nt*VirtualMemory "
    "calls and handle them as normal guest memory instead of asserting.",
    "Kernel");
DEFINE_bool(
    xboxkrnl_physical_memory_audit, false,
    "Thor Android compatibility: trace physical memory allocation/free "
    "ownership without changing behavior.",
    "Kernel");
DEFINE_uint32(
    xboxkrnl_physical_memory_audit_budget, 256,
    "Thor Android compatibility: maximum physical memory ownership audit rows "
    "to emit.",
    "Kernel");

namespace xe {
namespace kernel {
namespace xboxkrnl {
namespace {

std::atomic<int> g_physical_memory_audit_budget{0};
std::atomic<uint32_t> g_physical_memory_audit_configured_budget{
    std::numeric_limits<uint32_t>::max()};

bool CheckDebugMemoryArgument(const char* export_name, uint32_t debug_memory) {
  if (!debug_memory) {
    return true;
  }
  if (cvars::xboxkrnl_tolerate_debug_memory) {
    XELOGW("{} received DebugMemory={} - treating as guest memory",
           export_name, debug_memory);
    return true;
  }
  XELOGE("{} received unsupported DebugMemory={}", export_name, debug_memory);
  return false;
}

void ConfigurePhysicalMemoryAuditBudget() {
  uint32_t budget = cvars::xboxkrnl_physical_memory_audit_budget;
  uint32_t configured_budget =
      g_physical_memory_audit_configured_budget.load(std::memory_order_relaxed);
  if (configured_budget == budget) {
    return;
  }

  if (g_physical_memory_audit_configured_budget.compare_exchange_strong(
          configured_budget, budget, std::memory_order_acq_rel)) {
    int clamped_budget =
        budget > static_cast<uint32_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(budget);
    g_physical_memory_audit_budget.store(clamped_budget,
                                         std::memory_order_release);
  }
}

bool ConsumePhysicalMemoryAuditBudget() {
  if (!cvars::xboxkrnl_physical_memory_audit) {
    return false;
  }

  ConfigurePhysicalMemoryAuditBudget();
  int value = g_physical_memory_audit_budget.load(std::memory_order_relaxed);
  while (value > 0) {
    if (g_physical_memory_audit_budget.compare_exchange_strong(
            value, value - 1, std::memory_order_acq_rel)) {
      return true;
    }
  }
  return false;
}

const char* HeapTypeName(HeapType type) {
  switch (type) {
    case HeapType::kGuestVirtual:
      return "guest_virtual";
    case HeapType::kGuestXex:
      return "guest_xex";
    case HeapType::kGuestPhysical:
      return "guest_physical";
    case HeapType::kHostPhysical:
      return "host_physical";
    default:
      return "unknown";
  }
}

struct PhysicalMemoryThreadAuditContext {
  uint32_t thread_id = 0;
  uint32_t lr = 0;
  uint32_t ctr = 0;
  uint32_t r1 = 0;
};

PhysicalMemoryThreadAuditContext GetPhysicalMemoryThreadAuditContext() {
  PhysicalMemoryThreadAuditContext result = {};
  XThread* thread = XThread::GetCurrentThread();
  if (!thread) {
    return result;
  }

  result.thread_id = thread->thread_id();
  auto thread_state = thread->thread_state();
  auto context = thread_state ? thread_state->context() : nullptr;
  if (!context) {
    return result;
  }

  result.lr = static_cast<uint32_t>(context->lr);
  result.ctr = static_cast<uint32_t>(context->ctr);
  result.r1 = static_cast<uint32_t>(context->r[1]);
  return result;
}

struct HeapRegionAuditInfo {
  bool valid = false;
  uint32_t allocation_base = 0;
  uint32_t allocation_size = 0;
  uint32_t region_size = 0;
  uint32_t state = 0;
  uint32_t protect = 0;
};

HeapRegionAuditInfo QueryHeapRegionAuditInfo(BaseHeap* heap,
                                             uint32_t address) {
  HeapRegionAuditInfo result = {};
  if (!heap) {
    return result;
  }

  HeapAllocationInfo info = {};
  if (!heap->QueryRegionInfo(address, &info)) {
    return result;
  }

  result.valid = true;
  result.allocation_base = heap->heap_base() + info.allocation_base;
  result.allocation_size = info.allocation_size;
  result.region_size = info.region_size;
  result.state = info.state;
  result.protect = info.protect;
  return result;
}

void LogPhysicalMemoryAllocateAudit(
    uint32_t flags, uint32_t requested_size, uint32_t protect_bits,
    uint32_t min_addr_range, uint32_t max_addr_range, uint32_t alignment,
    uint32_t page_size, uint32_t adjusted_size, uint32_t adjusted_alignment,
    uint32_t heap_min_addr, uint32_t heap_max_addr, PhysicalHeap* heap,
    uint32_t result) {
  if (!ConsumePhysicalMemoryAuditBudget()) {
    return;
  }

  auto thread_context = GetPhysicalMemoryThreadAuditContext();
  uint32_t parent_address =
      result && heap ? heap->GetPhysicalAddress(result) : UINT32_MAX;
  HeapRegionAuditInfo physical_info =
      result ? QueryHeapRegionAuditInfo(heap, result) : HeapRegionAuditInfo{};
  BaseHeap* parent_heap =
      parent_address != UINT32_MAX ? kernel_memory()->LookupHeap(parent_address)
                                   : nullptr;
  HeapRegionAuditInfo parent_info =
      parent_heap ? QueryHeapRegionAuditInfo(parent_heap, parent_address)
                  : HeapRegionAuditInfo{};

  XELOGI(
      "Xboxkrnl physical memory audit allocate thid {:08X} lr {:08X} ctr "
      "{:08X} r1 {:08X} flags {:08X} requested_size {:08X} protect_bits "
      "{:08X} min_addr {:08X} max_addr {:08X} alignment {:08X} page_size "
      "{:X} adjusted_size {:08X} adjusted_alignment {:08X} heap_base {:08X} "
      "heap_size {:08X} heap_min {:08X} heap_max {:08X} result {:08X} "
      "parent_address {:08X} physical_allocation_base {:08X} "
      "physical_allocation_size {:08X} physical_region_size {:08X} "
      "physical_state {} physical_protect {} parent_allocation_base {:08X} "
      "parent_allocation_size {:08X} parent_region_size {:08X} parent_state "
      "{} parent_protect {} behavior_changed 0",
      thread_context.thread_id, thread_context.lr, thread_context.ctr,
      thread_context.r1, flags, requested_size, protect_bits, min_addr_range,
      max_addr_range, alignment, page_size, adjusted_size, adjusted_alignment,
      heap ? heap->heap_base() : 0, heap ? heap->heap_size() : 0,
      heap_min_addr, heap_max_addr, result, parent_address,
      physical_info.allocation_base, physical_info.allocation_size,
      physical_info.region_size, physical_info.state, physical_info.protect,
      parent_info.allocation_base, parent_info.allocation_size,
      parent_info.region_size, parent_info.state, parent_info.protect);
}

void LogPhysicalMemoryFreeAudit(const char* phase, uint32_t type,
                                uint32_t base_address, BaseHeap* heap,
                                bool result_known, bool result,
                                uint32_t returned_region_size) {
  if (!ConsumePhysicalMemoryAuditBudget()) {
    return;
  }

  auto thread_context = GetPhysicalMemoryThreadAuditContext();
  HeapType heap_type = heap ? heap->heap_type() : HeapType::kGuestVirtual;
  PhysicalHeap* physical_heap =
      heap && heap_type == HeapType::kGuestPhysical
          ? static_cast<PhysicalHeap*>(heap)
          : nullptr;
  uint32_t parent_address = physical_heap
                                ? physical_heap->GetPhysicalAddress(base_address)
                                : UINT32_MAX;
  HeapRegionAuditInfo physical_info =
      heap ? QueryHeapRegionAuditInfo(heap, base_address) : HeapRegionAuditInfo{};
  BaseHeap* parent_heap =
      parent_address != UINT32_MAX ? kernel_memory()->LookupHeap(parent_address)
                                   : nullptr;
  HeapRegionAuditInfo parent_info =
      parent_heap ? QueryHeapRegionAuditInfo(parent_heap, parent_address)
                  : HeapRegionAuditInfo{};
  uint32_t heap_page_size = heap ? heap->page_size() : 0;
  uint32_t parent_page_size = parent_heap ? parent_heap->page_size() : 0;

  XELOGI(
      "Xboxkrnl physical memory audit free phase {} thid {:08X} lr {:08X} "
      "ctr {:08X} r1 {:08X} type {} base_address {:08X} heap_present {} "
      "heap_type {} heap_base {:08X} heap_page_size {:X} parent_address "
      "{:08X} physical_allocation_base {:08X} physical_allocation_size "
      "{:08X} physical_region_size {:08X} physical_state {} "
      "physical_protect {} physical_region_start {} physical_page_aligned {} "
      "parent_allocation_base {:08X} parent_allocation_size {:08X} "
      "parent_region_size {:08X} parent_state {} parent_protect {} "
      "parent_region_start {} parent_page_aligned {} result_known {} result "
      "{} returned_region_size {:08X} behavior_changed 0",
      phase, thread_context.thread_id, thread_context.lr, thread_context.ctr,
      thread_context.r1, type, base_address, heap ? 1 : 0,
      heap ? HeapTypeName(heap_type) : "none", heap ? heap->heap_base() : 0,
      heap_page_size, parent_address, physical_info.allocation_base,
      physical_info.allocation_size, physical_info.region_size,
      physical_info.state, physical_info.protect,
      physical_info.valid && base_address == physical_info.allocation_base ? 1
                                                                           : 0,
      heap_page_size && (base_address % heap_page_size) == 0 ? 1 : 0,
      parent_info.allocation_base, parent_info.allocation_size,
      parent_info.region_size, parent_info.state, parent_info.protect,
      parent_info.valid && parent_address == parent_info.allocation_base ? 1
                                                                         : 0,
      parent_page_size && parent_address != UINT32_MAX &&
              (parent_address % parent_page_size) == 0
          ? 1
          : 0,
      result_known ? 1 : 0, result ? 1 : 0, returned_region_size);
}

}  // namespace

uint32_t ToXdkProtectFlags(uint32_t protect) {
  uint32_t result = 0;
  if (!(protect & kMemoryProtectRead) && !(protect & kMemoryProtectWrite)) {
    result = X_PAGE_NOACCESS;
  } else if ((protect & kMemoryProtectRead) &&
             !(protect & kMemoryProtectWrite)) {
    result = X_PAGE_READONLY;
  } else {
    result = X_PAGE_READWRITE;
  }
  if (protect & kMemoryProtectNoCache) {
    result |= X_PAGE_NOCACHE;
  }
  if (protect & kMemoryProtectWriteCombine) {
    result |= X_PAGE_WRITECOMBINE;
  }
  return result;
}

uint32_t FromXdkProtectFlags(uint32_t protect) {
  uint32_t result = 0;
  if ((protect & X_PAGE_READONLY) | (protect & X_PAGE_EXECUTE_READ)) {
    result = kMemoryProtectRead;
  } else if ((protect & X_PAGE_READWRITE) |
             (protect & X_PAGE_EXECUTE_READWRITE)) {
    result = kMemoryProtectRead | kMemoryProtectWrite;
  }
  if (protect & X_PAGE_NOCACHE) {
    result |= kMemoryProtectNoCache;
  }
  if (protect & X_PAGE_WRITECOMBINE) {
    result |= kMemoryProtectWriteCombine;
  }
  return result;
}

dword_result_t NtAllocateVirtualMemory_entry(lpdword_t base_addr_ptr,
                                             lpdword_t region_size_ptr,
                                             dword_t alloc_type,
                                             dword_t protect_bits,
                                             dword_t debug_memory) {
  // NTSTATUS
  // _Inout_  PVOID *BaseAddress,
  // _Inout_  PSIZE_T RegionSize,
  // _In_     ULONG AllocationType,
  // _In_     ULONG Protect
  // _In_     BOOLEAN DebugMemory

  assert_not_null(base_addr_ptr);
  assert_not_null(region_size_ptr);

  // Set to TRUE when allocation is from devkit memory area.
  if (!CheckDebugMemoryArgument("NtAllocateVirtualMemory",
                                debug_memory.value())) {
    return X_STATUS_INVALID_PARAMETER;
  }

  // This allocates memory from the kernel heap, which is initialized on startup
  // and shared by both the kernel implementation and user code.
  // The xe_memory_ref object is used to actually get the memory, and although
  // it's simple today we could extend it to do better things in the future.

  // Must request a size.
  if (!base_addr_ptr || !region_size_ptr || !*region_size_ptr) {
    return X_STATUS_INVALID_PARAMETER;
  }
  // Check allocation type.
  if (!(alloc_type & (X_MEM_COMMIT | X_MEM_RESET | X_MEM_RESERVE))) {
    return X_STATUS_INVALID_PARAMETER;
  }
  // If MEM_RESET is set only MEM_RESET can be set.
  if (alloc_type & X_MEM_RESET && (alloc_type & ~X_MEM_RESET)) {
    return X_STATUS_INVALID_PARAMETER;
  }
  // Don't allow games to set execute bits.
  if (protect_bits & (X_PAGE_EXECUTE | X_PAGE_EXECUTE_READ |
                      X_PAGE_EXECUTE_READWRITE | X_PAGE_EXECUTE_WRITECOPY)) {
    XELOGW("Game setting EXECUTE bit on allocation");
  }

  uint32_t page_size;
  if (*base_addr_ptr != 0) {
    // ignore specified page size when base address is specified.
    auto heap = kernel_memory()->LookupHeap(*base_addr_ptr);
    if (heap->heap_type() != HeapType::kGuestVirtual) {
      return X_STATUS_INVALID_PARAMETER;
    }
    page_size = heap->page_size();
  } else {
    // Adjust size.
    page_size = 4 * 1024;
    if (alloc_type & X_MEM_LARGE_PAGES) {
      page_size = 64 * 1024;
    }
  }

  // Round the base address down to the nearest page boundary.
  uint32_t adjusted_base = *base_addr_ptr - (*base_addr_ptr % page_size);
  // For some reason, some games pass in negative sizes.
  uint32_t adjusted_size = int32_t(*region_size_ptr) < 0
                               ? -int32_t(region_size_ptr.value())
                               : region_size_ptr.value();
  adjusted_size = xe::round_up(adjusted_size, page_size);

  // Allocate.
  uint32_t allocation_type = 0;
  if (alloc_type & X_MEM_RESERVE) {
    allocation_type |= kMemoryAllocationReserve;
  }
  if (alloc_type & X_MEM_COMMIT) {
    allocation_type |= kMemoryAllocationCommit;
  }
  if (alloc_type & X_MEM_RESET) {
    XELOGE("X_MEM_RESET not implemented");
    assert_always();
  }
  uint32_t protect = FromXdkProtectFlags(protect_bits);
  uint32_t address = 0;
  BaseHeap* heap;
  HeapAllocationInfo prev_alloc_info = {};
  bool was_commited = false;

  if (adjusted_base != 0) {
    heap = kernel_memory()->LookupHeap(adjusted_base);
    if (heap->page_size() != page_size) {
      // Specified the wrong page size for the wrong heap.
      return X_STATUS_ACCESS_DENIED;
    }
    was_commited = heap->QueryRegionInfo(adjusted_base, &prev_alloc_info) &&
                   (prev_alloc_info.state & kMemoryAllocationCommit) != 0;

    if (heap->AllocFixed(adjusted_base, adjusted_size, page_size,
                         allocation_type, protect)) {
      address = adjusted_base;
    }
  } else {
    bool top_down = !!(alloc_type & X_MEM_TOP_DOWN);
    heap = kernel_memory()->LookupHeapByType(false, page_size);
    heap->Alloc(adjusted_size, page_size, allocation_type, protect, top_down,
                &address);
  }
  if (!address) {
    // Failed - assume no memory available.
    return X_STATUS_NO_MEMORY;
  }

  // Zero memory, if needed.
  if (address && !(alloc_type & X_MEM_NOZERO)) {
    if (alloc_type & X_MEM_COMMIT) {
      if (!(protect & kMemoryProtectWrite)) {
        heap->Protect(address, adjusted_size,
                      kMemoryProtectRead | kMemoryProtectWrite);
      }
      if (!was_commited) {
        kernel_memory()->Zero(address, adjusted_size);
      }
      if (!(protect & kMemoryProtectWrite)) {
        heap->Protect(address, adjusted_size, protect);
      }
    }
  }

  XELOGD("NtAllocateVirtualMemory = {:08X}", address);

  // Stash back.
  // Maybe set X_STATUS_ALREADY_COMMITTED if MEM_COMMIT?
  *base_addr_ptr = address;
  *region_size_ptr = adjusted_size;
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(NtAllocateVirtualMemory, kMemory, kImplemented);

dword_result_t NtProtectVirtualMemory_entry(lpdword_t base_addr_ptr,
                                            lpdword_t region_size_ptr,
                                            dword_t protect_bits,
                                            lpdword_t old_protect,
                                            dword_t debug_memory) {
  // Set to TRUE when this memory refers to devkit memory area.
  if (!CheckDebugMemoryArgument("NtProtectVirtualMemory",
                                debug_memory.value())) {
    return X_STATUS_INVALID_PARAMETER;
  }

  // Must request a size.
  if (!base_addr_ptr || !region_size_ptr || !*region_size_ptr) {
    return X_STATUS_INVALID_PARAMETER;
  }

  // Don't allow games to set execute bits.
  if (protect_bits & (X_PAGE_EXECUTE | X_PAGE_EXECUTE_READ |
                      X_PAGE_EXECUTE_READWRITE | X_PAGE_EXECUTE_WRITECOPY)) {
    XELOGW("Game setting EXECUTE bit on protect");
    return X_STATUS_ACCESS_DENIED;
  }

  auto heap = kernel_memory()->LookupHeap(*base_addr_ptr);
  if (heap->heap_type() != HeapType::kGuestVirtual) {
    return X_STATUS_INVALID_PARAMETER;
  }
  // Adjust the base downwards to the nearest page boundary.
  uint32_t adjusted_base =
      *base_addr_ptr - (*base_addr_ptr % heap->page_size());
  uint32_t adjusted_size = xe::round_up(*region_size_ptr, heap->page_size());
  uint32_t protect = FromXdkProtectFlags(protect_bits);

  uint32_t tmp_old_protect = 0;

  // FIXME: I think it's valid for NtProtectVirtualMemory to span regions, but
  // as of now our implementation will fail in this case. Need to verify.
  if (!heap->Protect(adjusted_base, adjusted_size, protect, &tmp_old_protect)) {
    return X_STATUS_ACCESS_DENIED;
  }

  // Write back output variables.
  *base_addr_ptr = adjusted_base;
  *region_size_ptr = adjusted_size;

  if (old_protect) {
    *old_protect = tmp_old_protect;
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(NtProtectVirtualMemory, kMemory, kImplemented);

dword_result_t NtFreeVirtualMemory_entry(lpdword_t base_addr_ptr,
                                         lpdword_t region_size_ptr,
                                         dword_t free_type,
                                         dword_t debug_memory) {
  uint32_t base_addr_value = *base_addr_ptr;
  uint32_t region_size_value = *region_size_ptr;
  // X_MEM_DECOMMIT | X_MEM_RELEASE

  // NTSTATUS
  // _Inout_  PVOID *BaseAddress,
  // _Inout_  PSIZE_T RegionSize,
  // _In_     ULONG FreeType
  // _In_     BOOLEAN DebugMemory

  // Set to TRUE when freeing external devkit memory.
  if (!CheckDebugMemoryArgument("NtFreeVirtualMemory", debug_memory.value())) {
    return X_STATUS_INVALID_PARAMETER;
  }

  if (!base_addr_value) {
    return X_STATUS_MEMORY_NOT_ALLOCATED;
  }

  auto heap = kernel_state()->memory()->LookupHeap(base_addr_value);
  if (heap->heap_type() != HeapType::kGuestVirtual) {
    return X_STATUS_INVALID_PARAMETER;
  }
  bool result = false;
  if (free_type == X_MEM_DECOMMIT) {
    // If zero, we may need to query size (free whole region).
    assert_not_zero(region_size_value);

    region_size_value = xe::round_up(region_size_value, heap->page_size());
    result = heap->Decommit(base_addr_value, region_size_value);
  } else {
    result = heap->Release(base_addr_value, &region_size_value);
  }
  if (!result) {
    return X_STATUS_UNSUCCESSFUL;
  }

  *base_addr_ptr = base_addr_value;
  *region_size_ptr = region_size_value;
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(NtFreeVirtualMemory, kMemory, kImplemented);

struct X_MEMORY_BASIC_INFORMATION {
  be<uint32_t> base_address;
  be<uint32_t> allocation_base;
  be<uint32_t> allocation_protect;
  be<uint32_t> region_size;
  be<uint32_t> state;
  be<uint32_t> protect;
  be<uint32_t> type;
};

dword_result_t NtQueryVirtualMemory_entry(
    dword_t base_address,
    pointer_t<X_MEMORY_BASIC_INFORMATION> memory_basic_information_ptr) {
  auto heap = kernel_state()->memory()->LookupHeap(base_address);
  HeapAllocationInfo alloc_info;
  if (heap == nullptr || !heap->QueryRegionInfo(base_address, &alloc_info)) {
    return X_STATUS_INVALID_PARAMETER;
  }

  memory_basic_information_ptr->base_address = alloc_info.base_address;
  memory_basic_information_ptr->allocation_base = alloc_info.allocation_base;
  memory_basic_information_ptr->allocation_protect =
      ToXdkProtectFlags(alloc_info.allocation_protect);
  memory_basic_information_ptr->region_size = alloc_info.region_size;
  // https://docs.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-memory_basic_information
  // State: ... This member can be one of the following values: MEM_COMMIT,
  // MEM_FREE, MEM_RESERVE.
  // State queried by Beautiful Katamari before displaying the loading screen.
  uint32_t x_state;
  if (alloc_info.state & kMemoryAllocationCommit) {
    assert_not_zero(alloc_info.state & kMemoryAllocationReserve);
    x_state = X_MEM_COMMIT;
  } else if (alloc_info.state & kMemoryAllocationReserve) {
    x_state = X_MEM_RESERVE;
  } else {
    x_state = X_MEM_FREE;
  }
  memory_basic_information_ptr->state = x_state;
  memory_basic_information_ptr->protect = ToXdkProtectFlags(alloc_info.protect);
  memory_basic_information_ptr->type = X_MEM_PRIVATE;

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(NtQueryVirtualMemory, kMemory, kImplemented);

dword_result_t MmAllocatePhysicalMemoryEx_entry(
    dword_t flags, dword_t region_size, dword_t protect_bits,
    dword_t min_addr_range, dword_t max_addr_range, dword_t alignment) {
  // Type will usually be 0 (user request?), where 1 and 2 are sometimes made
  // by D3D/etc.

  // Check protection bits.
  if (!(protect_bits & (X_PAGE_READONLY | X_PAGE_READWRITE))) {
    XELOGE("MmAllocatePhysicalMemoryEx: bad protection bits");
    return 0;
  }

  // Either may be OR'ed into protect_bits:
  // X_PAGE_NOCACHE
  // X_PAGE_WRITECOMBINE
  // We could use this to detect what's likely GPU-synchronized memory
  // and let the GPU know we're messing with it (or even allocate from
  // the GPU). At least the D3D command buffer is X_PAGE_WRITECOMBINE.

  // Calculate page size.
  // Default            = 4KB
  // X_MEM_LARGE_PAGES  = 64KB
  // X_MEM_16MB_PAGES   = 16MB
  uint32_t page_size = 4 * 1024;
  if (protect_bits & X_MEM_LARGE_PAGES) {
    page_size = 64 * 1024;
  } else if (protect_bits & X_MEM_16MB_PAGES) {
    page_size = 16 * 1024 * 1024;
  }

  // Round up the region size and alignment to the next page.
  uint32_t adjusted_size = xe::round_up(region_size, page_size);
  uint32_t adjusted_alignment = xe::round_up(alignment, page_size);

  uint32_t allocation_type = kMemoryAllocationReserve | kMemoryAllocationCommit;
  uint32_t protect = FromXdkProtectFlags(protect_bits);
  bool top_down = true;
  auto heap = static_cast<PhysicalHeap*>(
      kernel_memory()->LookupHeapByType(true, page_size));
  // min_addr_range/max_addr_range are bounds in physical memory, not virtual.
  uint32_t heap_base = heap->heap_base();
  uint32_t heap_physical_address_offset = heap->GetPhysicalAddress(heap_base);
  uint32_t heap_min_addr =
      xe::sat_sub(min_addr_range.value(), heap_physical_address_offset);
  uint32_t heap_max_addr =
      xe::sat_sub(max_addr_range.value(), heap_physical_address_offset);
  uint32_t heap_size = heap->heap_size();
  heap_min_addr = heap_base + std::min(heap_min_addr, heap_size - 1);
  heap_max_addr = heap_base + std::min(heap_max_addr, heap_size - 1);
  uint32_t base_address;
  if (!heap->AllocRange(heap_min_addr, heap_max_addr, adjusted_size,
                        adjusted_alignment, allocation_type, protect, top_down,
                        &base_address)) {
    LogPhysicalMemoryAllocateAudit(
        flags, region_size, protect_bits, min_addr_range, max_addr_range,
        alignment, page_size, adjusted_size, adjusted_alignment, heap_min_addr,
        heap_max_addr, heap, 0);
    // Failed - assume no memory available.
    return 0;
  }
  XELOGD("MmAllocatePhysicalMemoryEx = {:08X}", base_address);
  LogPhysicalMemoryAllocateAudit(
      flags, region_size, protect_bits, min_addr_range, max_addr_range,
      alignment, page_size, adjusted_size, adjusted_alignment, heap_min_addr,
      heap_max_addr, heap, base_address);

  return base_address;
}
DECLARE_XBOXKRNL_EXPORT1(MmAllocatePhysicalMemoryEx, kMemory, kImplemented);

dword_result_t MmAllocatePhysicalMemory_entry(dword_t flags,
                                              dword_t region_size,
                                              dword_t protect_bits) {
  return MmAllocatePhysicalMemoryEx_entry(flags, region_size, protect_bits, 0,
                                          0xFFFFFFFFu, 0);
}
DECLARE_XBOXKRNL_EXPORT1(MmAllocatePhysicalMemory, kMemory, kImplemented);

void MmFreePhysicalMemory_entry(dword_t type, dword_t base_address) {
  // base_address = result of MmAllocatePhysicalMemory.

  assert_true((base_address & 0x1F) == 0);

  auto heap = kernel_state()->memory()->LookupHeap(base_address);
  if (!heap) {
    LogPhysicalMemoryFreeAudit("request", type, base_address, nullptr, false,
                               false, 0);
    XELOGE("MmFreePhysicalMemory failed: no heap for type={} base_address={:08X}",
           type, base_address);
    return;
  }
  LogPhysicalMemoryFreeAudit("request", type, base_address, heap, false, false,
                             0);
  uint32_t released_region_size = 0;
  bool released = heap->Release(base_address, &released_region_size);
  LogPhysicalMemoryFreeAudit("result", type, base_address, heap, true, released,
                             released_region_size);
  if (!released) {
    XELOGE(
        "MmFreePhysicalMemory failed: type={} base_address={:08X} "
        "heap_type={} heap_base={:08X} page_size={:X}",
        type, base_address, static_cast<int>(heap->heap_type()),
        heap->heap_base(), heap->page_size());
  }
}
DECLARE_XBOXKRNL_EXPORT1(MmFreePhysicalMemory, kMemory, kImplemented);

dword_result_t MmQueryAddressProtect_entry(dword_t base_address) {
  auto heap = kernel_state()->memory()->LookupHeap(base_address);
  uint32_t access;
  if (!heap->QueryProtect(base_address, &access)) {
    access = 0;
  }
  access = !access ? 0 : ToXdkProtectFlags(access);

  return access;
}
DECLARE_XBOXKRNL_EXPORT2(MmQueryAddressProtect, kMemory, kImplemented,
                         kHighFrequency);

void MmSetAddressProtect_entry(lpvoid_t base_address, dword_t region_size,
                               dword_t protect_bits) {
  if (!protect_bits) {
    XELOGE("MmSetAddressProtect: Failed due to incorrect protect_bits");
    return;
  }

  uint32_t protect = FromXdkProtectFlags(protect_bits);
  auto heap = kernel_memory()->LookupHeap(base_address);
  heap->Protect(base_address.guest_address(), region_size, protect);
}
DECLARE_XBOXKRNL_EXPORT1(MmSetAddressProtect, kMemory, kImplemented);

dword_result_t MmQueryAllocationSize_entry(lpvoid_t base_address) {
  auto heap = kernel_state()->memory()->LookupHeap(base_address);
  uint32_t size;
  if (!heap->QuerySize(base_address, &size)) {
    size = 0;
  }

  return size;
}
DECLARE_XBOXKRNL_EXPORT1(MmQueryAllocationSize, kMemory, kImplemented);

// https://code.google.com/p/vdash/source/browse/trunk/vdash/include/kernel.h
struct X_MM_QUERY_STATISTICS_SECTION {
  xe::be<uint32_t> available_pages;
  xe::be<uint32_t> total_virtual_memory_bytes;
  xe::be<uint32_t> reserved_virtual_memory_bytes;
  xe::be<uint32_t> physical_pages;
  xe::be<uint32_t> pool_pages;
  xe::be<uint32_t> stack_pages;
  xe::be<uint32_t> image_pages;
  xe::be<uint32_t> heap_pages;
  xe::be<uint32_t> virtual_pages;
  xe::be<uint32_t> page_table_pages;
  xe::be<uint32_t> cache_pages;
};

struct X_MM_QUERY_STATISTICS_RESULT {
  xe::be<uint32_t> size;
  xe::be<uint32_t> total_physical_pages;
  xe::be<uint32_t> kernel_pages;
  X_MM_QUERY_STATISTICS_SECTION title;
  X_MM_QUERY_STATISTICS_SECTION system;
  xe::be<uint32_t> highest_physical_page;
};
static_assert_size(X_MM_QUERY_STATISTICS_RESULT, 104);

dword_result_t MmQueryStatistics_entry(
    pointer_t<X_MM_QUERY_STATISTICS_RESULT> stats_ptr) {
  if (!stats_ptr) {
    return X_STATUS_INVALID_PARAMETER;
  }

  const uint32_t size = sizeof(X_MM_QUERY_STATISTICS_RESULT);

  if (stats_ptr->size != size) {
    return X_STATUS_BUFFER_TOO_SMALL;
  }

  // Zero out the struct.
  stats_ptr.Zero();

  // Set the constants the game is likely asking for.
  // These numbers are mostly guessed. If the game is just checking for
  // memory, this should satisfy it. If it's actually verifying things
  // this won't work :/
  stats_ptr->size = size;

  stats_ptr->total_physical_pages = 0x00020000;  // 512mb / 4kb pages
  stats_ptr->kernel_pages = 0x00000300;

  // TODO(gibbed): maybe use LookupHeapByType instead?
  auto heap_a = kernel_memory()->LookupHeap(0xA0000000);
  auto heap_c = kernel_memory()->LookupHeap(0xC0000000);
  auto heap_e = kernel_memory()->LookupHeap(0xE0000000);

  assert_not_null(heap_a);
  assert_not_null(heap_c);
  assert_not_null(heap_e);

#define GET_USED_PAGE_COUNT(x) \
  (x->GetTotalPageCount() - x->GetUnreservedPageCount())
#define GET_USED_PAGE_SIZE(x) ((GET_USED_PAGE_COUNT(x) * x->page_size()) / 4096)
  uint32_t used_pages = 0;
  used_pages += GET_USED_PAGE_SIZE(heap_a);
  used_pages += GET_USED_PAGE_SIZE(heap_c);
  used_pages += GET_USED_PAGE_SIZE(heap_e);
#undef GET_USED_PAGE_SIZE
#undef GET_USED_PAGE_COUNT

  assert_true(used_pages < stats_ptr->total_physical_pages);

  stats_ptr->title.available_pages =
      stats_ptr->total_physical_pages - used_pages;
  stats_ptr->title.total_virtual_memory_bytes =
      0x2FFF0000;  // TODO(gibbed): FIXME
  stats_ptr->title.reserved_virtual_memory_bytes =
      0x00160000;                                // TODO(gibbed): FIXME
  stats_ptr->title.physical_pages = 0x00001000;  // TODO(gibbed): FIXME
  stats_ptr->title.pool_pages = 0x00000010;
  stats_ptr->title.stack_pages = 0x00000100;
  stats_ptr->title.image_pages = 0x00000100;
  stats_ptr->title.heap_pages = 0x00000100;
  stats_ptr->title.virtual_pages = 0x00000100;
  stats_ptr->title.page_table_pages = 0x00000100;
  stats_ptr->title.cache_pages = 0x00000100;

  stats_ptr->system.available_pages = 0x00000000;
  stats_ptr->system.total_virtual_memory_bytes = 0x00000000;
  stats_ptr->system.reserved_virtual_memory_bytes = 0x00000000;
  stats_ptr->system.physical_pages = 0x00000000;
  stats_ptr->system.pool_pages = 0x00000000;
  stats_ptr->system.stack_pages = 0x00000000;
  stats_ptr->system.image_pages = 0x00000000;
  stats_ptr->system.heap_pages = 0x00000000;
  stats_ptr->system.virtual_pages = 0x00000000;
  stats_ptr->system.page_table_pages = 0x00000000;
  stats_ptr->system.cache_pages = 0x00000000;

  stats_ptr->highest_physical_page = 0x0001FFFF;

  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(MmQueryStatistics, kMemory, kImplemented);

// https://msdn.microsoft.com/en-us/library/windows/hardware/ff554547(v=vs.85).aspx
dword_result_t MmGetPhysicalAddress_entry(dword_t base_address) {
  // PHYSICAL_ADDRESS MmGetPhysicalAddress(
  //   _In_  PVOID BaseAddress
  // );
  // base_address = result of MmAllocatePhysicalMemory.
  uint32_t physical_address = kernel_memory()->GetPhysicalAddress(base_address);
  assert_true(physical_address != UINT32_MAX);
  if (physical_address == UINT32_MAX) {
    physical_address = 0;
  }
  if (cvars::gpu_trace_swap) {
    XELOGI("GPU swap trace: MmGetPhysicalAddress base={:08X} -> {:08X}",
           uint32_t(base_address), physical_address);
  }
  return physical_address;
}
DECLARE_XBOXKRNL_EXPORT1(MmGetPhysicalAddress, kMemory, kImplemented);

dword_result_t MmMapIoSpace_entry(dword_t unk0, lpvoid_t src_address,
                                  dword_t size, dword_t flags) {
  // I've only seen this used to map XMA audio contexts.
  // The code seems fine with taking the src address, so this just returns that.
  // If others start using it there could be problems.
  assert_true(unk0 == 2);
  assert_true(size == 0x40);
  assert_true(flags == 0x404);

  return src_address.guest_address();
}
DECLARE_XBOXKRNL_EXPORT1(MmMapIoSpace, kMemory, kImplemented);

dword_result_t ExAllocatePoolTypeWithTag_entry(dword_t size, dword_t tag,
                                               dword_t zero) {
  uint32_t alignment = 8;
  uint32_t adjusted_size = size;
  if (adjusted_size < 4 * 1024) {
    adjusted_size = xe::round_up(adjusted_size, 4 * 1024);
  } else {
    alignment = 4 * 1024;
  }

  uint32_t addr =
      kernel_state()->memory()->SystemHeapAlloc(adjusted_size, alignment);

  return addr;
}
DECLARE_XBOXKRNL_EXPORT1(ExAllocatePoolTypeWithTag, kMemory, kImplemented);

dword_result_t ExAllocatePool_entry(dword_t size) {
  const uint32_t none = 0x656E6F4E;  // 'None'
  return ExAllocatePoolTypeWithTag_entry(size, none, 0);
}
DECLARE_XBOXKRNL_EXPORT1(ExAllocatePool, kMemory, kImplemented);

void ExFreePool_entry(lpvoid_t base_address) {
  kernel_state()->memory()->SystemHeapFree(base_address);
}
DECLARE_XBOXKRNL_EXPORT1(ExFreePool, kMemory, kImplemented);

dword_result_t KeGetImagePageTableEntry_entry(lpvoid_t address) {
  // Unknown
  return 1;
}
DECLARE_XBOXKRNL_EXPORT1(KeGetImagePageTableEntry, kMemory, kStub);

dword_result_t KeLockL2_entry() {
  // TODO
  return 0;
}
DECLARE_XBOXKRNL_EXPORT1(KeLockL2, kMemory, kStub);

void KeUnlockL2_entry() {}
DECLARE_XBOXKRNL_EXPORT1(KeUnlockL2, kMemory, kStub);

dword_result_t MmCreateKernelStack_entry(dword_t stack_size, dword_t r4) {
  assert_zero(r4);  // Unknown argument.

  auto stack_size_aligned = (stack_size + 0xFFF) & 0xFFFFF000;
  uint32_t stack_alignment = (stack_size & 0xF000) ? 0x1000 : 0x10000;

  uint32_t stack_address;
  kernel_memory()
      ->LookupHeap(0x70000000)
      ->AllocRange(0x70000000, 0x7F000000, stack_size_aligned, stack_alignment,
                   kMemoryAllocationReserve | kMemoryAllocationCommit,
                   kMemoryProtectRead | kMemoryProtectWrite, false,
                   &stack_address);
  return stack_address + stack_size;
}
DECLARE_XBOXKRNL_EXPORT1(MmCreateKernelStack, kMemory, kImplemented);

dword_result_t MmDeleteKernelStack_entry(lpvoid_t stack_base,
                                         lpvoid_t stack_end) {
  // Release the stack (where stack_end is the low address)
  if (kernel_memory()->LookupHeap(0x70000000)->Release(stack_end)) {
    return X_STATUS_SUCCESS;
  }

  return X_STATUS_UNSUCCESSFUL;
}
DECLARE_XBOXKRNL_EXPORT1(MmDeleteKernelStack, kMemory, kImplemented);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Memory);
