/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_RING_BUFFER_H_
#define XENIA_BASE_RING_BUFFER_H_

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"

namespace xe {

class RingBuffer {
 public:
  RingBuffer(uint8_t* buffer, size_t capacity);

  uint8_t* buffer() const { return buffer_; }
  size_t capacity() const { return capacity_; }
  bool empty() const { return read_offset_ == write_offset_; }

  size_t read_offset() const { return read_offset_; }
  uintptr_t read_ptr() const { return uintptr_t(buffer_) + read_offset_; }
  void set_read_offset(size_t offset) { read_offset_ = offset % capacity_; }
  size_t read_count() const {
    if (read_offset_ == write_offset_) {
      return 0;
    } else if (read_offset_ < write_offset_) {
      return write_offset_ - read_offset_;
    } else {
      return (capacity_ - read_offset_) + write_offset_;
    }
  }

  size_t write_offset() const { return write_offset_; }
  uintptr_t write_ptr() const { return uintptr_t(buffer_) + write_offset_; }
  void set_write_offset(size_t offset) { write_offset_ = offset % capacity_; }
  size_t write_count() const {
    if (read_offset_ == write_offset_) {
      return capacity_;
    } else if (write_offset_ < read_offset_) {
      return read_offset_ - write_offset_;
    } else {
      return (capacity_ - write_offset_) + read_offset_;
    }
  }

  void AdvanceRead(size_t count);
  void AdvanceWrite(size_t count);

  struct ReadRange {
    const uint8_t* first;
    size_t first_length;
    const uint8_t* second;
    size_t second_length;
  };
  ReadRange BeginRead(size_t count);
  void EndRead(ReadRange read_range);

  size_t Read(uint8_t* buffer, size_t count);
  template <typename T>
  size_t Read(T* buffer, size_t count) {
    return Read(reinterpret_cast<uint8_t*>(buffer), count);
  }

  // Fast inline read of a single fundamental value. The PM4 command stream is
  // parsed almost entirely through this (millions of dword reads per frame in
  // ExecutePacketType0), so avoid the full Read() path (function call, bounds
  // checks, std::min, a 4-byte memcpy) for the overwhelmingly common case where
  // the value does not wrap the ring - read it directly from the buffer.
  template <typename T>
  T Read() {
    static_assert(std::is_fundamental<T>::value,
                  "Immediate read only supports basic types!");
    if (read_offset_ + sizeof(T) <= capacity_) {
      T imm;
      std::memcpy(&imm, buffer_ + read_offset_, sizeof(T));
      read_offset_ += sizeof(T);
      if (read_offset_ == capacity_) {
        read_offset_ = 0;
      }
      return imm;
    }
    // Rare wrap-around (or sizeof(T) > capacity) - use the general path.
    T imm;
    size_t read = Read(reinterpret_cast<uint8_t*>(&imm), sizeof(T));
    assert_true(read == sizeof(T));
    return imm;
  }

  template <typename T>
  T ReadAndSwap() {
    return xe::byte_swap(Read<T>());
  }

  size_t Write(const uint8_t* buffer, size_t count);
  template <typename T>
  size_t Write(const T* buffer, size_t count) {
    return Write(reinterpret_cast<const uint8_t*>(buffer), count);
  }

  template <typename T>
  size_t Write(T& data) {
    return Write(reinterpret_cast<const uint8_t*>(&data), sizeof(T));
  }

 private:
  uint8_t* buffer_ = nullptr;
  size_t capacity_ = 0;
  size_t read_offset_ = 0;
  size_t write_offset_ = 0;
};

}  // namespace xe

#endif  // XENIA_BASE_RING_BUFFER_H_
