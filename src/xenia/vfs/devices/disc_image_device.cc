/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/disc_image_device.h"

#include <cstring>
#include <vector>

#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#if XE_PLATFORM_ANDROID
#include "xenia/base/filesystem.h"
#endif  // XE_PLATFORM_ANDROID
#include "xenia/vfs/devices/disc_image_entry.h"
#include "xenia/vfs/gdfx_util.h"

namespace xe {
namespace vfs {

using namespace xe::literals;

DiscImageDevice::DiscImageDevice(const std::string_view mount_path,
                                 const std::filesystem::path& host_path)
    : Device(mount_path), name_("GDFX"), host_path_(host_path) {}

DiscImageDevice::~DiscImageDevice() = default;

bool DiscImageDevice::Initialize() {
  // An ISO is mapped; a CHD (by its signature) is decompressed on demand.
  source_ = DiscImageSource::Open(host_path_);
  if (!source_) {
    XELOGE("Disc image could not be opened");
    return false;
  } else {
    XELOGFS("DiscImageDevice::Initialize");
  }

  ParseState state = {0};
  state.size = size_t(source_->size());
  auto result = Verify(&state);
  if (result != Error::kSuccess) {
    XELOGE("Failed to verify disc image header: {}",
           static_cast<int32_t>(result));
    return false;
  }

  result = ReadAllEntries(&state);
  if (result != Error::kSuccess) {
    XELOGE("Failed to read all GDFX entries: {}", static_cast<int32_t>(result));
    return false;
  }

  return true;
}

void DiscImageDevice::Dump(StringBuffer* string_buffer) {
  auto global_lock = global_critical_region_.Acquire();
  root_entry_->Dump(string_buffer, 0);
}

Entry* DiscImageDevice::ResolvePath(const std::string_view path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo
  XELOGFS("DiscImageDevice::ResolvePath({})", path);
  return root_entry_->ResolvePath(path);
}

DiscImageDevice::Error DiscImageDevice::Verify(ParseState* state) {
  // The game partition starts at one of the known offsets; its volume
  // descriptor (magic, root sector and size) is at sector 32. The same test
  // as GdfxFindPartition, through reads: a CHD is not in memory.
  for (size_t offset : kGdfxLikelyOffsets) {
    size_t sector32_offset = offset + (32 * kGdfxSectorSize);
    uint8_t descriptor[28];
    if (!source_->Read(sector32_offset, descriptor, sizeof(descriptor)) ||
        std::memcmp(descriptor, kGdfxMagic, kGdfxMagicSize)) {
      continue;
    }
    uint32_t root_sector = xe::load<uint32_t>(descriptor + 20);
    uint32_t root_size = xe::load<uint32_t>(descriptor + 24);
    if (root_size < 13 || root_size > 32 * 1024 * 1024) {
      continue;
    }
    state->game_offset = offset;
    state->root_sector = root_sector;
    state->root_size = root_size;
    state->root_offset =
        state->game_offset + (state->root_sector * kGdfxSectorSize);
    return Error::kSuccess;
  }
  return Error::kErrorFileMismatch;
}

DiscImageDevice::Error DiscImageDevice::ReadAllEntries(ParseState* state) {
  auto root_entry = new DiscImageEntry(this, nullptr, "", source_.get());
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry_ = std::unique_ptr<Entry>(root_entry);

  std::vector<uint8_t> root_buffer(state->root_size);
  if (!source_->Read(state->root_offset, root_buffer.data(),
                     root_buffer.size())) {
    return Error::kErrorReadError;
  }
  if (!ReadEntry(state, root_buffer.data(), 0, root_entry)) {
    return Error::kErrorOutOfMemory;
  }

  return Error::kSuccess;
}

bool DiscImageDevice::ReadEntry(ParseState* state, const uint8_t* buffer,
                                uint16_t entry_ordinal,
                                DiscImageEntry* parent) {
  const uint8_t* p = buffer + (entry_ordinal * 4);

  uint16_t node_l = xe::load<uint16_t>(p + 0);
  uint16_t node_r = xe::load<uint16_t>(p + 2);
  size_t sector = xe::load<uint32_t>(p + 4);
  size_t length = xe::load<uint32_t>(p + 8);
  uint8_t attributes = xe::load<uint8_t>(p + 12);
  uint8_t name_length = xe::load<uint8_t>(p + 13);
  auto name_buffer = reinterpret_cast<const char*>(p + 14);

  if (node_l && !ReadEntry(state, buffer, node_l, parent)) {
    return false;
  }

  // Filename is stored as Windows-1252, convert it to UTF-8.
  auto ansi_name = std::string(name_buffer, name_length);
  auto name = xe::win1252_to_utf8(ansi_name);
  // Fallback to normal name if for whatever reason conversion from 1252 code
  // page failed.
  if (name.empty()) {
    name = ansi_name;
  }

  auto entry = DiscImageEntry::Create(this, parent, name, source_.get());
  entry->attributes_ = attributes | kFileAttributeReadOnly;
  entry->size_ = length;
  entry->allocation_size_ = xe::round_up(length, bytes_per_sector());

  // Set to January 1, 1970 (UTC) in 100-nanosecond intervals
  entry->create_timestamp_ = 10000 * 11644473600000LL;
  entry->access_timestamp_ = 10000 * 11644473600000LL;
  entry->write_timestamp_ = 10000 * 11644473600000LL;

  if (attributes & kFileAttributeDirectory) {
    // Folder.
    entry->data_offset_ = 0;
    entry->data_size_ = 0;
    if (length) {
      // Not a leaf - read in children.
      if (state->size < state->game_offset + (sector * kGdfxSectorSize)) {
        // Out of bounds read.
        return false;
      }
      // Read child list.
      std::vector<uint8_t> folder(length);
      if (!source_->Read(state->game_offset + (sector * kGdfxSectorSize),
                         folder.data(), folder.size())) {
        return false;
      }
      if (!ReadEntry(state, folder.data(), 0, entry.get())) {
        return false;
      }
    }
  } else {
    // File.
    entry->data_offset_ = state->game_offset + (sector * kGdfxSectorSize);
    entry->data_size_ = length;
  }

  // Add to parent.
  parent->children_.emplace_back(std::move(entry));

  // Read next file in the list.
  if (node_r && !ReadEntry(state, buffer, node_r, parent)) {
    return false;
  }

  return true;
}

}  // namespace vfs
}  // namespace xe
