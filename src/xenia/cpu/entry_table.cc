/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/entry_table.h"

#include "xenia/base/cvar.h"
#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"

DEFINE_bool(
    cpu_lockfree_entry_lookup, false,
    "Thor CPU speed lane: serve EntryTable::Get() (the JIT function-resolution "
    "lookup) from a lock-free validated read-cache, skipping the global "
    "critical-region lock on the common hit. Entries are immortal for the "
    "table's lifetime so the cached Entry* is stable; a slot is validated by "
    "entry->address and only READY entries are cached, so a hit is exact + "
    "needs no lock. Removes lock acquire/release from indirect-call/cold "
    "resolution on CPU-bound titles (Burnout, Lost Odyssey). Default off.",
    "CPU");

namespace xe {
namespace cpu {

EntryTable::EntryTable() {
  // std::atomic<Entry*> has a trivial (uninitialized) default ctor, so zero the
  // slots explicitly. Always allocated (~128KB); the cvar gates use, not size.
  lookup_cache_ = std::make_unique<std::atomic<Entry*>[]>(kLookupCacheSize);
  for (uint32_t i = 0; i < kLookupCacheSize; ++i) {
    lookup_cache_[i].store(nullptr, std::memory_order_relaxed);
  }
}

EntryTable::~EntryTable() {
  auto global_lock = global_critical_region_.Acquire();
  for (auto it : map_) {
    Entry* entry = it.second;
    delete entry;
  }
}

Entry* EntryTable::Get(uint32_t address) {
  // Lock-free fast path: a validated cache hit returns without the global lock.
  if (cvars::cpu_lockfree_entry_lookup) {
    Entry* cached = lookup_cache_[LookupCacheSlot(address)].load(
        std::memory_order_acquire);
    if (cached && cached->address == address) {
      // Only READY entries are ever published here, and Entry* is stable, so a
      // matching slot is a valid READY entry - no lock, no status re-read.
      return cached;
    }
  }

  auto global_lock = global_critical_region_.Acquire();
  const auto& it = map_.find(address);
  Entry* entry = it != map_.end() ? it->second : nullptr;
  if (entry) {
    // TODO(benvanik): wait if needed?
    if (entry->status != Entry::STATUS_READY) {
      entry = nullptr;
    }
  }
  if (cvars::cpu_lockfree_entry_lookup && entry) {
    // entry is READY here - publish it for future lock-free hits (release pairs
    // with the acquire-load above so the reader sees a fully-built Entry).
    lookup_cache_[LookupCacheSlot(address)].store(entry,
                                                  std::memory_order_release);
  }
  return entry;
}

Entry::Status EntryTable::GetOrCreate(uint32_t address, Entry** out_entry) {
  // TODO(benvanik): replace with a map with wait-free for find.
  // https://github.com/facebook/folly/blob/master/folly/AtomicHashMap.h

  auto global_lock = global_critical_region_.Acquire();
  const auto& it = map_.find(address);
  Entry* entry = it != map_.end() ? it->second : nullptr;
  Entry::Status status;
  if (entry) {
    // If we aren't ready yet spin and wait.
    if (entry->status == Entry::STATUS_COMPILING) {
      // Still compiling, so spin.
      do {
        global_lock.unlock();
        // TODO(benvanik): sleep for less time?
        xe::threading::Sleep(std::chrono::microseconds(10));
        global_lock.lock();
      } while (entry->status == Entry::STATUS_COMPILING);
    }
    status = entry->status;
  } else {
    // Create and return for initialization.
    entry = new Entry();
    entry->address = address;
    entry->end_address = 0;
    entry->status = Entry::STATUS_COMPILING;
    entry->function = 0;
    map_[address] = entry;
    status = Entry::STATUS_NEW;
  }
  global_lock.unlock();
  *out_entry = entry;
  return status;
}

std::vector<Function*> EntryTable::FindWithAddress(uint32_t address) {
  auto global_lock = global_critical_region_.Acquire();
  std::vector<Function*> fns;
  for (auto& it : map_) {
    Entry* entry = it.second;
    if (address >= entry->address && address <= entry->end_address) {
      if (entry->status == Entry::STATUS_READY) {
        fns.push_back(entry->function);
      }
    }
  }
  return fns;
}

}  // namespace cpu
}  // namespace xe
