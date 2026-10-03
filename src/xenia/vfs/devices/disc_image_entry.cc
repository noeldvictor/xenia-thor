/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/disc_image_entry.h"

#include "xenia/base/math.h"
#include "xenia/vfs/devices/disc_image_file.h"

namespace xe {
namespace vfs {

DiscImageEntry::DiscImageEntry(Device* device, Entry* parent,
                               const std::string_view path,
                               DiscImageSource* source)
    : Entry(device, parent, path),
      source_(source),
      data_offset_(0),
      data_size_(0) {}

DiscImageEntry::~DiscImageEntry() = default;

std::unique_ptr<DiscImageEntry> DiscImageEntry::Create(
    Device* device, Entry* parent, const std::string_view name,
    DiscImageSource* source) {
  auto path = xe::utf8::join_guest_paths(parent->path(), name);
  auto entry = std::make_unique<DiscImageEntry>(device, parent, path, source);
  return std::move(entry);
}

X_STATUS DiscImageEntry::Open(uint32_t desired_access, File** out_file) {
  *out_file = new DiscImageFile(desired_access, this);
  return X_STATUS_SUCCESS;
}

std::unique_ptr<MappedMemory> DiscImageEntry::OpenMapped(
    MappedMemory::Mode mode, size_t offset, size_t length) {
  if (mode != MappedMemory::Mode::kRead) {
    // Only allow reads.
    return nullptr;
  }

  MappedMemory* mapped = source_->mapped();
  if (!mapped) {
    return nullptr;
  }
  size_t real_offset = data_offset_ + offset;
  size_t real_length = length ? std::min(length, data_size_) : data_size_;
  return mapped->Slice(real_offset, real_length);
}

bool DiscImageEntry::DeleteEntryInternal(Entry* entry) { return false; }
}  // namespace vfs
}  // namespace xe
