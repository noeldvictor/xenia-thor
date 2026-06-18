/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_ENTRY_TABLE_H_
#define XENIA_CPU_ENTRY_TABLE_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "xenia/base/mutex.h"

namespace xe {
namespace cpu {

class Function;

typedef struct Entry_t {
  typedef enum {
    STATUS_NEW = 0,
    STATUS_COMPILING,
    STATUS_READY,
    STATUS_FAILED,
  } Status;

  uint32_t address;
  uint32_t end_address;
  Status status;
  Function* function;
} Entry;

class EntryTable {
 public:
  EntryTable();
  ~EntryTable();

  Entry* Get(uint32_t address);
  Entry::Status GetOrCreate(uint32_t address, Entry** out_entry);

  std::vector<Function*> FindWithAddress(uint32_t address);

 private:
  // Lock-free validated read-cache for Get() (cpu_lockfree_entry_lookup,
  // default-off). Entries live until ~EntryTable, so an Entry* is STABLE for the
  // table's lifetime; a slot is validated by entry->address (so hash collisions
  // just miss to the locked path), and ONLY READY entries are ever published, so
  // a validated hit needs no global lock and no status re-read. One atomic<Entry*>
  // per slot = a single acquire-load, torn-read-free. Power-of-two for masking.
  static constexpr uint32_t kLookupCacheSize = 1u << 14;
  static constexpr uint32_t kLookupCacheMask = kLookupCacheSize - 1;
  static uint32_t LookupCacheSlot(uint32_t address) {
    return (address * 2654435761u) & kLookupCacheMask;  // Knuth multiplicative
  }

  xe::global_critical_region global_critical_region_;
  // TODO(benvanik): replace with a better data structure.
  std::unordered_map<uint32_t, Entry*> map_;
  std::unique_ptr<std::atomic<Entry*>[]> lookup_cache_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_ENTRY_TABLE_H_
