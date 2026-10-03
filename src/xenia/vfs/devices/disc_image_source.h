/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_DISC_IMAGE_SOURCE_H_
#define XENIA_VFS_DEVICES_DISC_IMAGE_SOURCE_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

#include "xenia/base/mapped_memory.h"

namespace xe {
namespace vfs {

// The bytes of a disc image: a memory-mapped ISO, or a CHD (MAME's compressed
// hunks of data, `chdman createdvd`) whose hunks are decompressed on demand.
class DiscImageSource {
 public:
  virtual ~DiscImageSource() = default;

  // The logical size of the image (the ISO size for a CHD).
  virtual uint64_t size() const = 0;
  // Copies [offset, offset + length) of the image into out. False past the end
  // of the image or when the image cannot be read there.
  virtual bool Read(uint64_t offset, void* out, size_t length) = 0;
  // The whole image in memory, or nullptr when it is not mapped (a CHD): then
  // the entries cannot be memory-mapped and readers fall back to reading.
  virtual MappedMemory* mapped() { return nullptr; }

  // Opens a disc image: a CHD by its "MComprHD" signature, otherwise the file
  // mapped as an ISO. Accepts an Android content URI.
  static std::unique_ptr<DiscImageSource> Open(
      const std::filesystem::path& host_path);
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_DISC_IMAGE_SOURCE_H_
