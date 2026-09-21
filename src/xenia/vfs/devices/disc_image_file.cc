/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/disc_image_file.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/vfs/devices/disc_image_entry.h"
namespace xe {
namespace vfs {

DiscImageFile::DiscImageFile(uint32_t file_access, DiscImageEntry* entry)
    : File(file_access, entry), entry_(entry) {}

DiscImageFile::~DiscImageFile() = default;

void DiscImageFile::Destroy() { delete this; }

X_STATUS DiscImageFile::ReadSync(std::span<uint8_t> buffer, size_t byte_offset,
                                 size_t* out_bytes_read) {
  if (byte_offset >= entry_->size()) {
    return X_STATUS_END_OF_FILE;
  }

  if (entry_->data_offset() >= entry_->mmap()->size()) {
    xe::FatalError("This ISO image is corrupted and cannot be played.");
    return X_STATUS_END_OF_FILE;
  }

  size_t real_offset = entry_->data_offset() + byte_offset;
  size_t real_length =
      std::min(buffer.size(), entry_->data_size() - byte_offset);
  std::memcpy(buffer.data(), entry_->mmap()->data() + real_offset, real_length);
  // The disc is a 2,048-byte-sector medium and its driver serves unbuffered
  // reads (FILE_NO_INTERMEDIATE_BUFFERING: sector-aligned offset and length)
  // in whole sectors: a read that crosses the logical end of the file returns
  // the length rounded up to the sector, with the tail of the last sector as
  // it is on the disc (zero here). Banjo-Kazooie: Nuts & Bolts verifies its
  // \Bundle files with such reads and compares the returned length with the
  // sector-rounded request; the clamp to the logical size (18,860 for a
  // 20,480 request at the end of a 103,958,956-byte file) made it call
  // XamShowDirtyDiscErrorUI (2026-09-20). The tail bytes come from the image
  // itself, as the drive returns them; zeros only past the end of the image.
  // Buffered reads (unaligned) keep the exact length.
  constexpr size_t kSectorSize = 2048;
  if (real_length < buffer.size() && (byte_offset % kSectorSize) == 0 &&
      (buffer.size() % kSectorSize) == 0) {
    size_t rounded = (real_length + kSectorSize - 1) / kSectorSize * kSectorSize;
    rounded = std::min(rounded, buffer.size());
    if (rounded > real_length) {
      size_t tail = rounded - real_length;
      size_t image_avail =
          entry_->mmap()->size() > real_offset + real_length
              ? entry_->mmap()->size() - (real_offset + real_length)
              : 0;
      size_t from_image = std::min(tail, image_avail);
      std::memcpy(buffer.data() + real_length,
                  entry_->mmap()->data() + real_offset + real_length,
                  from_image);
      std::memset(buffer.data() + real_length + from_image, 0,
                  tail - from_image);
      real_length = rounded;
    }
  }
  *out_bytes_read = real_length;
  return X_STATUS_SUCCESS;
}

}  // namespace vfs
}  // namespace xe
