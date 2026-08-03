/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_PORTAL_PORTAL_H_
#define XENIA_HID_PORTAL_PORTAL_H_

#include <cstdint>
#include <span>

#include "xenia/xbox.h"

namespace xe {
namespace hid {

constexpr uint8_t kPortalBufferSize = 0x20;

// NOTE(kernel-port): interface-only port of Edge's "portal" device support
// (Skylanders / Disney Infinity USB portals). Edge implements it in
// src/xenia/hid/portal/{portal.cc,hardware_portal.{h,cc}}, built into
// xenia-hid and instantiated on Win32 only. This fork ships just the abstract
// interface so the XamInputNonController* kernel exports compile;
// InputSystem::GetPortal() always returns nullptr here, so those exports
// return X_ERROR_DEVICE_NOT_CONNECTED (identical to Edge on a machine with no
// portal attached).
// Re-enable path: copy portal.cc + hardware_portal.{h,cc} from `edge/edge`
// into this directory, re-run premake so xenia-hid picks up the new .cc files
// (this project globs *.cc per directory), and construct portal_ in the
// InputSystem constructor under XE_PLATFORM_WIN32.
class Portal {
 public:
  virtual ~Portal() = default;

  virtual bool IsConnected() = 0;

  virtual X_STATUS Read(std::span<uint8_t> data, uint32_t& bytes_read,
                        uint16_t& state) = 0;
  virtual X_STATUS Write(std::span<uint8_t> data) = 0;

  virtual void OnDeviceArrival() = 0;
  virtual void OnDeviceRemoval() = 0;
};

}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_PORTAL_PORTAL_H_
