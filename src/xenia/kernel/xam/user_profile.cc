/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/user_profile.h"

<<<<<<< ours
=======
#include "xenia/base/cvar.h"

#include <sstream>

>>>>>>> theirs
#include "third_party/fmt/include/fmt/format.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xdbf/gpd_info.h"

DEFINE_string(
    user_gamertag, "",
    "Gamertag for the signed-in profile (max 15 characters). Empty keeps the "
    "default. Set from the Android launcher's settings; some titles show or "
    "save under this name.",
    "Kernel");

namespace xe {
namespace kernel {
namespace xam {

UserProfile::UserProfile(const uint64_t xuid,
                         const X_XAMACCOUNTINFO* account_info)
    : xuid_(xuid), account_info_(*account_info), profile_images_() {
  // 58410A1F checks the user XUID against a mask of 0x00C0000000000000 (3<<54),
  // if non-zero, it prevents the user from playing the game.
  // "You do not have permissions to perform this operation."
<<<<<<< ours
  LoadProfileGpds();

  // Load default gamer tiles
  LoadProfileIcon(XTileType::kGamerTile);
  LoadProfileIcon(XTileType::kGamerTileSmall);

  // Also load personal gamer tiles (custom profile pictures)
  // Note: These use the same filenames as regular tiles, so if a personal
  // tile exists, it will overwrite the default tile file. We load both types
  // to maintain compatibility - the personal tile will be preferred when
  // present.
  LoadProfileIcon(XTileType::kPersonalGamerTile);
  LoadProfileIcon(XTileType::kPersonalGamerTileSmall);
=======
  xuid_ = 0xB13EBABEBABEBABE;
  name_ = "User";
  if (!cvars::user_gamertag.empty()) {
    name_ = cvars::user_gamertag.substr(0, 15);
  }

  // https://cs.rin.ru/forum/viewtopic.php?f=38&t=60668&hilit=gfwl+live&start=195
  // https://github.com/arkem/py360/blob/master/py360/constants.py
  // XPROFILE_GAMER_YAXIS_INVERSION
  AddSetting(std::make_unique<Int32Setting>(0x10040002, 0));
  // XPROFILE_OPTION_CONTROLLER_VIBRATION
  AddSetting(std::make_unique<Int32Setting>(0x10040003, 3));
  // XPROFILE_GAMERCARD_ZONE
  AddSetting(std::make_unique<Int32Setting>(0x10040004, 0));
  // XPROFILE_GAMERCARD_REGION
  AddSetting(std::make_unique<Int32Setting>(0x10040005, 0));
  // XPROFILE_GAMERCARD_CRED
  AddSetting(std::make_unique<Int32Setting>(0x10040006, 0xFA));
  // XPROFILE_GAMERCARD_REP
  AddSetting(std::make_unique<FloatSetting>(0x5004000B, 0.0f));
  // XPROFILE_OPTION_VOICE_MUTED
  AddSetting(std::make_unique<Int32Setting>(0x1004000C, 0));
  // XPROFILE_OPTION_VOICE_THRU_SPEAKERS
  AddSetting(std::make_unique<Int32Setting>(0x1004000D, 0));
  // XPROFILE_OPTION_VOICE_VOLUME
  AddSetting(std::make_unique<Int32Setting>(0x1004000E, 0x64));
  // XPROFILE_GAMERCARD_MOTTO
  AddSetting(std::make_unique<UnicodeSetting>(0x402C0011, u""));
  // XPROFILE_GAMERCARD_TITLES_PLAYED
  AddSetting(std::make_unique<Int32Setting>(0x10040012, 1));
  // XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED
  AddSetting(std::make_unique<Int32Setting>(0x10040013, 0));
  // XPROFILE_GAMER_DIFFICULTY
  AddSetting(std::make_unique<Int32Setting>(0x10040015, 0));
  // XPROFILE_GAMER_CONTROL_SENSITIVITY
  AddSetting(std::make_unique<Int32Setting>(0x10040018, 0));
  // Preferred color 1
  AddSetting(std::make_unique<Int32Setting>(0x1004001D, 0xFFFF0000u));
  // Preferred color 2
  AddSetting(std::make_unique<Int32Setting>(0x1004001E, 0xFF00FF00u));
  // XPROFILE_GAMER_ACTION_AUTO_AIM
  AddSetting(std::make_unique<Int32Setting>(0x10040022, 1));
  // XPROFILE_GAMER_ACTION_AUTO_CENTER
  AddSetting(std::make_unique<Int32Setting>(0x10040023, 0));
  // XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL
  AddSetting(std::make_unique<Int32Setting>(0x10040024, 0));
  // XPROFILE_GAMER_RACE_TRANSMISSION
  AddSetting(std::make_unique<Int32Setting>(0x10040026, 0));
  // XPROFILE_GAMER_RACE_CAMERA_LOCATION
  AddSetting(std::make_unique<Int32Setting>(0x10040027, 0));
  // XPROFILE_GAMER_RACE_BRAKE_CONTROL
  AddSetting(std::make_unique<Int32Setting>(0x10040028, 0));
  // XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL
  AddSetting(std::make_unique<Int32Setting>(0x10040029, 0));
  // XPROFILE_GAMERCARD_TITLE_CRED_EARNED
  AddSetting(std::make_unique<Int32Setting>(0x10040038, 0));
  // XPROFILE_GAMERCARD_TITLE_ACHIEVEMENTS_EARNED
  AddSetting(std::make_unique<Int32Setting>(0x10040039, 0));

  // If we set this, games will try to get it.
  // XPROFILE_GAMERCARD_PICTURE_KEY
  AddSetting(
      std::make_unique<UnicodeSetting>(0x4064000F, u"gamercard_picture_key"));

  // XPROFILE_TITLE_SPECIFIC1
  AddSetting(std::make_unique<BinarySetting>(0x63E83FFF));
  // XPROFILE_TITLE_SPECIFIC2
  AddSetting(std::make_unique<BinarySetting>(0x63E83FFE));
  // XPROFILE_TITLE_SPECIFIC3
  AddSetting(std::make_unique<BinarySetting>(0x63E83FFD));
>>>>>>> theirs
}

GpdInfo* UserProfile::GetGpd(const uint32_t title_id) {
  return const_cast<GpdInfo*>(
      const_cast<const UserProfile*>(this)->GetGpd(title_id));
}

const GpdInfo* UserProfile::GetGpd(const uint32_t title_id) const {
  if (title_id == kDashboardID) {
    return &dashboard_gpd_;
  }

  if (!games_gpd_.count(title_id)) {
    return nullptr;
  }

  return &games_gpd_.at(title_id);
}

void UserProfile::LoadProfileGpds() {
  // First load dashboard GPD because it stores all opened games
  dashboard_gpd_ = LoadGpd(kDashboardID);
  if (!dashboard_gpd_.IsValid()) {
    dashboard_gpd_ = GpdInfoProfile();
  }

  const auto gpds_to_load = dashboard_gpd_.GetTitlesInfo();

  for (const auto& gpd : gpds_to_load) {
    const auto gpd_data = LoadGpd(gpd->title_id);
    if (gpd_data.empty()) {
      continue;
    }

    // Use insert_or_assign to replace existing GPDs when reloading
    games_gpd_.insert_or_assign(gpd->title_id,
                                GpdInfoTitle(gpd->title_id, gpd_data));
  }
}

void UserProfile::LoadProfileIcon(XTileType tile_type) {
  if (!kTileFileNames.count(tile_type)) {
    return;
  }

  const std::string path =
      fmt::format("User_{:016X}:\\{}", xuid_, kTileFileNames.at(tile_type));

  vfs::File* file = nullptr;
  vfs::FileAction action;

  const X_STATUS result = kernel_state()->file_system()->OpenFile(
      nullptr, path, vfs::FileDisposition::kOpen, vfs::FileAccess::kGenericRead,
      false, true, &file, &action);

  if (result != X_STATUS_SUCCESS) {
    return;
  }

  std::vector<uint8_t> data(file->entry()->size());
  size_t written_bytes = 0;
  file->ReadSync(std::span<uint8_t>(data.data(), file->entry()->size()), 0,
                 &written_bytes);
  file->Destroy();

  profile_images_.insert_or_assign(tile_type, data);
}

void UserProfile::WriteProfileIcon(XTileType tile_type,
                                   std::span<const uint8_t> icon_data) {
  const std::string path =
      fmt::format("User_{:016X}:\\{}", xuid_, kTileFileNames.at(tile_type));

  vfs::File* file = nullptr;
  vfs::FileAction action;

  const X_STATUS result = kernel_state()->file_system()->OpenFile(
      nullptr, path, vfs::FileDisposition::kOverwriteIf,
      vfs::FileAccess::kGenericWrite | vfs::FileAccess::kFileWriteData, false,
      true, &file, &action);

  if (result != X_STATUS_SUCCESS) {
    return;
  }

  // Set the file length first to ensure we can write
  X_STATUS set_length_result = file->SetLength(icon_data.size());
  if (set_length_result != X_STATUS_SUCCESS &&
      set_length_result != X_STATUS_NOT_IMPLEMENTED) {
    XELOGW("WriteProfileIcon: SetLength failed with status {:08X}",
           set_length_result);
  }

  size_t written_bytes = 0;

  X_STATUS write_result =
      file->WriteSync({icon_data.data(), icon_data.size()}, 0, &written_bytes);
  if (write_result != X_STATUS_SUCCESS) {
    XELOGW("WriteProfileIcon: WriteSync failed with status {:08X}",
           write_result);
  } else {
    XELOGI("WriteProfileIcon: Successfully wrote {} bytes to {}", written_bytes,
           path);
  }

  file->Destroy();

  profile_images_.insert_or_assign(
      tile_type, std::vector<uint8_t>(icon_data.begin(), icon_data.end()));
}

std::vector<uint8_t> UserProfile::LoadGpd(const uint32_t title_id) {
  auto entry = kernel_state()->file_system()->ResolvePath(
      fmt::format("User_{:016X}:\\{:08X}.gpd", xuid_, title_id));

  if (!entry) {
    XELOGW("User {} (XUID: {:016X}) doesn't have profile GPD!", name(), xuid());
    return {};
  }

  vfs::File* file;
  auto result = entry->Open(vfs::FileAccess::kFileReadData, &file);
  if (result != X_STATUS_SUCCESS) {
    XELOGW("User {} (XUID: {:016X}) cannot open profile GPD!", name(), xuid());
    return {};
  }

  std::vector<uint8_t> data(entry->size());

  size_t read_size = 0;
  result = file->ReadSync(std::span<uint8_t>(data.data(), entry->size()), 0,
                          &read_size);
  if (result != X_STATUS_SUCCESS || read_size != entry->size()) {
    XELOGW(
        "User {} (XUID: {:016X}) cannot read profile GPD! Status: {:08X} read: "
        "{}/{} bytes",
        name(), xuid(), result, read_size, entry->size());
    return {};
  }

  file->Destroy();
  return data;
}

bool UserProfile::WriteGpd(const uint32_t title_id) {
  const GpdInfo* gpd = GetGpd(title_id);
  if (!gpd) {
    return false;
  }

  std::vector<uint8_t> data = gpd->Serialize();

  vfs::File* file = nullptr;
  vfs::FileAction action;

  const std::string mounted_path =
      fmt::format("User_{:016X}:\\{:08X}.gpd", xuid_, title_id);

  const X_STATUS result = kernel_state()->file_system()->OpenFile(
      nullptr, mounted_path, vfs::FileDisposition::kOverwriteIf,
      vfs::FileAccess::kGenericWrite, false, true, &file, &action);

  if (result != X_STATUS_SUCCESS) {
    return false;
  }

  size_t written_bytes = 0;
  file->WriteSync(std::span<uint8_t>(data.data(), data.size()), 0,
                  &written_bytes);
  file->Destroy();
  return true;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
