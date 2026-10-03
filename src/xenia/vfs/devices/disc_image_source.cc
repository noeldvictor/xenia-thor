/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/disc_image_source.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "libchdr/chd.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"

#if XE_PLATFORM_ANDROID
#include <unistd.h>
#endif  // XE_PLATFORM_ANDROID

namespace xe {
namespace vfs {

namespace {

class MappedDiscImageSource final : public DiscImageSource {
 public:
  explicit MappedDiscImageSource(std::unique_ptr<MappedMemory> mmap)
      : mmap_(std::move(mmap)) {}

  uint64_t size() const override { return mmap_->size(); }

  bool Read(uint64_t offset, void* out, size_t length) override {
    if (offset > mmap_->size() || length > mmap_->size() - offset) {
      return false;
    }
    std::memcpy(out, mmap_->data() + offset, length);
    return true;
  }

  MappedMemory* mapped() override { return mmap_.get(); }

 private:
  std::unique_ptr<MappedMemory> mmap_;
};

int FileSeek(FILE* file, int64_t offset, int origin) {
#if XE_PLATFORM_WIN32
  return _fseeki64(file, offset, origin);
#else
  return fseeko(file, off_t(offset), origin);
#endif  // XE_PLATFORM_WIN32
}

int64_t FileTell(FILE* file) {
#if XE_PLATFORM_WIN32
  return _ftelli64(file);
#else
  return int64_t(ftello(file));
#endif  // XE_PLATFORM_WIN32
}

// libchdr reads the CHD through these, on the FILE the source opened (an
// Android content URI's file descriptor through fdopen).
uint64_t ChdFileSize(void* user) {
  auto file = static_cast<FILE*>(user);
  int64_t position = FileTell(file);
  if (FileSeek(file, 0, SEEK_END)) {
    return uint64_t(-1);
  }
  int64_t end = FileTell(file);
  FileSeek(file, position, SEEK_SET);
  return end < 0 ? uint64_t(-1) : uint64_t(end);
}

size_t ChdFileRead(void* buffer, size_t size, size_t count, void* user) {
  return std::fread(buffer, size, count, static_cast<FILE*>(user));
}

int ChdFileClose(void* user) { return std::fclose(static_cast<FILE*>(user)); }

int ChdFileSeek(void* user, int64_t offset, int origin) {
  return FileSeek(static_cast<FILE*>(user), offset, origin) ? -1 : 0;
}

const core_file_callbacks kChdFileCallbacks = {ChdFileSize, ChdFileRead,
                                               ChdFileClose, ChdFileSeek};

// A CHD made with `chdman createdvd`: the ISO in hunks (a few KB each),
// compressed with zstd, LZMA, zlib or Huffman. libchdr decodes one hunk per
// chd_read and is not thread-safe, so the reads are serialized; a hunk that a
// read covers completely is decoded straight into the caller's buffer, and the
// hunks a read covers in part (directory and small file reads) are kept in a
// small LRU cache.
class ChdDiscImageSource final : public DiscImageSource {
 public:
  ~ChdDiscImageSource() override {
    if (chd_) {
      chd_close(chd_);
    }
  }

  // Takes the file: libchdr closes it, on success with the CHD and on failure
  // at once.
  static std::unique_ptr<DiscImageSource> Open(FILE* file,
                                               const std::string& name) {
    chd_file* chd = nullptr;
    chd_error error = chd_open_core_file_callbacks(&kChdFileCallbacks, file,
                                                   CHD_OPEN_READ, nullptr, &chd);
    if (error != CHDERR_NONE) {
      XELOGE("CHD {}: cannot open it: {}{}", name, chd_error_string(error),
             error == CHDERR_REQUIRES_PARENT
                 ? " (a CHD with a parent is not supported; make a standalone "
                   "one with chdman createdvd)"
                 : "");
      return nullptr;
    }
    // A CD-ROM or GD-ROM CHD holds raw sectors with subcode per track, not the
    // 2,048-byte sectors of a DVD: an Xbox 360 disc is a DVD image.
    static const uint32_t kCdTags[] = {
        CDROM_TRACK_METADATA2_TAG, CDROM_TRACK_METADATA_TAG,
        CDROM_OLD_METADATA_TAG, GDROM_TRACK_METADATA_TAG,
        GDROM_OLD_METADATA_TAG};
    for (uint32_t tag : kCdTags) {
      char metadata[256];
      uint32_t metadata_length = 0;
      if (chd_get_metadata(chd, tag, 0, metadata, sizeof(metadata),
                           &metadata_length, nullptr,
                           nullptr) == CHDERR_NONE) {
        XELOGE(
            "CHD {}: a CD or GD-ROM image, not a DVD; make it with chdman "
            "createdvd from the ISO",
            name);
        chd_close(chd);
        return nullptr;
      }
    }
    const chd_header* header = chd_get_header(chd);
    if (!header || !header->hunkbytes || !header->logicalbytes) {
      XELOGE("CHD {}: no data in the header", name);
      chd_close(chd);
      return nullptr;
    }
    // Read-ahead of the compressed data: fewer, larger reads from the storage
    // (flash behind a content URI on Android).
    chd_set_cache_budget(chd, 4 * 1024 * 1024);
    XELOGI(
        "CHD {}: {} bytes in {} hunks of {} bytes, codecs {:08X} {:08X} "
        "{:08X} {:08X}",
        name, header->logicalbytes, header->totalhunks, header->hunkbytes,
        header->compression[0], header->compression[1],
        header->compression[2], header->compression[3]);
    return std::unique_ptr<DiscImageSource>(
        new ChdDiscImageSource(chd, header->hunkbytes, header->logicalbytes));
  }

  uint64_t size() const override { return logical_bytes_; }

  bool Read(uint64_t offset, void* out, size_t length) override {
    if (offset > logical_bytes_ || length > logical_bytes_ - offset) {
      return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto dest = static_cast<uint8_t*>(out);
    while (length) {
      uint32_t hunk = uint32_t(offset / hunk_bytes_);
      size_t in_hunk = size_t(offset % hunk_bytes_);
      size_t count = std::min(length, size_t(hunk_bytes_) - in_hunk);
      if (!in_hunk && count == hunk_bytes_) {
        if (!ReadHunk(hunk, dest)) {
          return false;
        }
      } else {
        const uint8_t* data = CachedHunk(hunk);
        if (!data) {
          return false;
        }
        std::memcpy(dest, data + in_hunk, count);
      }
      dest += count;
      offset += count;
      length -= count;
    }
    return true;
  }

 private:
  ChdDiscImageSource(chd_file* chd, uint32_t hunk_bytes, uint64_t logical_bytes)
      : chd_(chd), hunk_bytes_(hunk_bytes), logical_bytes_(logical_bytes) {
    // About 8 MB of decoded hunks.
    size_t entries =
        std::clamp(size_t(8 * 1024 * 1024) / hunk_bytes_, size_t(8),
                   size_t(256));
    cache_.resize(entries);
    for (CacheEntry& entry : cache_) {
      entry.data.resize(hunk_bytes_);
    }
  }

  bool ReadHunk(uint32_t hunk, uint8_t* dest) {
    chd_error error = chd_read(chd_, hunk, dest);
    if (error != CHDERR_NONE) {
      static uint32_t logged = 0;
      if (logged < 16) {
        ++logged;
        XELOGE("CHD: hunk {} cannot be read: {}", hunk,
               chd_error_string(error));
      }
      return false;
    }
    return true;
  }

  const uint8_t* CachedHunk(uint32_t hunk) {
    ++use_clock_;
    CacheEntry* victim = &cache_[0];
    for (CacheEntry& entry : cache_) {
      if (entry.valid && entry.hunk == hunk) {
        entry.last_use = use_clock_;
        return entry.data.data();
      }
      if (!entry.valid ||
          (victim->valid && entry.last_use < victim->last_use)) {
        victim = &entry;
      }
    }
    victim->valid = false;
    if (!ReadHunk(hunk, victim->data.data())) {
      return nullptr;
    }
    victim->valid = true;
    victim->hunk = hunk;
    victim->last_use = use_clock_;
    return victim->data.data();
  }

  struct CacheEntry {
    bool valid = false;
    uint32_t hunk = 0;
    uint64_t last_use = 0;
    std::vector<uint8_t> data;
  };

  chd_file* chd_;
  uint32_t hunk_bytes_;
  uint64_t logical_bytes_;
  std::mutex mutex_;
  std::vector<CacheEntry> cache_;
  uint64_t use_clock_ = 0;
};

}  // namespace

std::unique_ptr<DiscImageSource> DiscImageSource::Open(
    const std::filesystem::path& host_path) {
  const std::string name = xe::path_to_utf8(host_path);
  FILE* file = nullptr;
#if XE_PLATFORM_ANDROID
  const bool content_uri = xe::filesystem::IsAndroidContentUri(name);
  if (content_uri) {
    int fd = xe::filesystem::OpenAndroidContentFileDescriptor(name, "r");
    if (fd >= 0) {
      file = fdopen(fd, "rb");
      if (!file) {
        close(fd);
      }
    }
  } else {
    file = xe::filesystem::OpenFile(host_path, "rb");
  }
#else
  file = xe::filesystem::OpenFile(host_path, "rb");
#endif  // XE_PLATFORM_ANDROID
  if (file) {
    char magic[8] = {};
    if (std::fread(magic, 1, sizeof(magic), file) == sizeof(magic) &&
        !std::memcmp(magic, "MComprHD", sizeof(magic))) {
      FileSeek(file, 0, SEEK_SET);
      return ChdDiscImageSource::Open(file, name);
    }
    std::fclose(file);
  }

  std::unique_ptr<MappedMemory> mmap;
#if XE_PLATFORM_ANDROID
  if (content_uri) {
    mmap = MappedMemory::OpenForAndroidContentUri(name,
                                                  MappedMemory::Mode::kRead);
  } else {
    mmap = MappedMemory::Open(host_path, MappedMemory::Mode::kRead);
  }
#else
  mmap = MappedMemory::Open(host_path, MappedMemory::Mode::kRead);
#endif  // XE_PLATFORM_ANDROID
  if (!mmap) {
    return nullptr;
  }
  return std::make_unique<MappedDiscImageSource>(std::move(mmap));
}

}  // namespace vfs
}  // namespace xe
