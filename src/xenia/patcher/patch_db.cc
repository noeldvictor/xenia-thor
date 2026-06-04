/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#include <cstring>

#include "xenia/patcher/patch_db.h"

#include "third_party/cpptoml/include/cpptoml.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/base/string_util.h"

// Defined in config.cc - reads a TOML file, skipping a leading UTF-8 BOM.
extern std::shared_ptr<cpptoml::table> ParseFile(
    const std::filesystem::path& filename);

DEFINE_bool(apply_patches, true, "Enables custom patching functionality",
            "General");

namespace xe {
namespace patcher {

namespace {
// The fork's string_util lacks hex_string_to_array (upstream uses it for the
// "array" patch type); a small local parser covers it. Tolerates whitespace.
bool HexStringToArray(std::vector<uint8_t>& data, const std::string& hex) {
  auto hexval = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string clean;
  clean.reserve(hex.size());
  for (char c : hex) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      continue;
    }
    clean.push_back(c);
  }
  if (clean.empty() || (clean.size() % 2) != 0) {
    return false;
  }
  data.clear();
  data.reserve(clean.size() / 2);
  for (size_t i = 0; i < clean.size(); i += 2) {
    int hi = hexval(clean[i]);
    int lo = hexval(clean[i + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    data.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return true;
}
}  // namespace

PatchDB::PatchDB(const std::filesystem::path patches_root) {
  patches_root_ = patches_root;
  LoadPatches();
}

PatchDB::~PatchDB() {}

void PatchDB::LoadPatches() {
  if (!cvars::apply_patches) {
    return;
  }

  const std::filesystem::path patches_directory = patches_root_ / "patches";
  const std::vector<xe::filesystem::FileInfo> patch_files =
      filesystem::ListFiles(patches_directory);

  for (const xe::filesystem::FileInfo& patch_file : patch_files) {
    // Skip files that don't have title_id as name and .patch.toml as extension.
    if (!std::regex_match(path_to_utf8(patch_file.name),
                          patch_filename_regex_)) {
      XELOGE("PatchDB: Skipped loading file {} due to incorrect filename",
             path_to_utf8(patch_file.name));
      continue;
    }

    const PatchFileEntry loaded_title_patches =
        ReadPatchFile(patch_file.path / patch_file.name);
    if (loaded_title_patches.title_id != -1) {
      loaded_patches_.push_back(loaded_title_patches);
    }
  }
  XELOGI("PatchDB: Loaded patches for {} titles", loaded_patches_.size());
}

PatchFileEntry PatchDB::ReadPatchFile(
    const std::filesystem::path& file_path) const {
  PatchFileEntry patch_file;
  std::shared_ptr<cpptoml::table> patch_toml_fields;

  try {
    patch_toml_fields = ParseFile(file_path);
  } catch (...) {
    XELOGE("PatchDB: Cannot load patch file: {}",
           path_to_utf8(file_path.filename()));
    patch_file.title_id = -1;
    return patch_file;
  }

  auto title_name = patch_toml_fields->get_as<std::string>("title_name");
  auto title_id = patch_toml_fields->get_as<std::string>("title_id");
  auto hashes_node = patch_toml_fields->get("hash");

  if (!title_name || !title_id || !hashes_node) {
    XELOGE("PatchDB: Cannot load patch file: {}",
           path_to_utf8(file_path.filename()));
    patch_file.title_id = -1;
    return patch_file;
  }

  patch_file.title_id =
      static_cast<uint32_t>(strtoul((*title_id).c_str(), nullptr, 16));
  patch_file.title_name = *title_name;
  ReadHashes(patch_file, hashes_node);

  auto patch_array = patch_toml_fields->get_table_array("patch");
  if (!patch_array) {
    return patch_file;
  }

  for (const auto& patch_entry : *patch_array) {
    PatchInfoEntry patch = PatchInfoEntry();
    ReadPatchHeader(patch, patch_entry);
    patch_file.patch_info.push_back(patch);
  }
  return patch_file;
}

bool PatchDB::ReadPatchData(
    std::vector<PatchDataEntry>& patch_data,
    const std::pair<std::string, PatchData> data_type,
    std::shared_ptr<cpptoml::table> patch_fields) const {
  auto patch_data_tables = patch_fields->get_table_array(data_type.first);
  if (!patch_data_tables) {
    return true;
  }

  for (const auto& table : *patch_data_tables) {
    auto address_field = table->get_as<int64_t>("address");
    if (!address_field) {
      continue;
    }
    uint32_t address = static_cast<uint32_t>(*address_field);
    size_t alloc_size = (size_t)data_type.second.size;

    switch (data_type.second.type) {
      case PatchDataType::kBE8: {
        uint16_t value =
            static_cast<uint16_t>(table->get_as<int64_t>("value").value_or(0));
        patch_data.push_back({address, PatchDataValue(alloc_size, value)});
        break;
      }
      case PatchDataType::kBE16: {
        uint16_t value =
            static_cast<uint16_t>(table->get_as<int64_t>("value").value_or(0));
        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kBE32: {
        uint32_t value =
            static_cast<uint32_t>(table->get_as<int64_t>("value").value_or(0));
        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kBE64: {
        uint64_t value =
            static_cast<uint64_t>(table->get_as<int64_t>("value").value_or(0));
        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kF64: {
        double value = 0.0;
        auto as_float = table->get_as<double>("value");
        auto as_int = table->get_as<int64_t>("value");
        if (as_float) {
          value = *as_float;
        } else if (as_int) {
          value = static_cast<double>(*as_int);
        }
        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kF32: {
        float value = 0.0f;
        auto as_float = table->get_as<double>("value");
        auto as_int = table->get_as<int64_t>("value");
        if (as_float) {
          value = static_cast<float>(*as_float);
        } else if (as_int) {
          value = static_cast<float>(*as_int);
        }
        patch_data.push_back(
            {address, PatchDataValue(alloc_size, xe::byte_swap(value))});
        break;
      }
      case PatchDataType::kString: {
        std::string value = table->get_as<std::string>("value").value_or("");
        patch_data.push_back({address, PatchDataValue(value)});
        break;
      }
      case PatchDataType::kU16String: {
        std::u16string value =
            xe::to_utf16(table->get_as<std::string>("value").value_or(""));
        patch_data.push_back({address, PatchDataValue(value)});
        break;
      }
      case PatchDataType::kByteArray: {
        std::vector<uint8_t> bytes;
        const std::string value =
            table->get_as<std::string>("value").value_or("");
        if (!HexStringToArray(bytes, value)) {
          XELOGW(
              "PatchDB: Cannot convert hex string to byte array at {:08X}! "
              "Skipping",
              address);
          return false;
        }
        patch_data.push_back({address, PatchDataValue(bytes)});
        break;
      }
      default: {
        XELOGW("PatchDB: Unknown patch data type for address {:08X}! Skipping",
               address);
        return false;
      }
    }
  }
  return true;
}

std::vector<PatchFileEntry> PatchDB::GetTitlePatches(
    const uint32_t title_id, const std::optional<uint64_t> hash) {
  std::vector<PatchFileEntry> title_patches;

  std::copy_if(
      loaded_patches_.cbegin(), loaded_patches_.cend(),
      std::back_inserter(title_patches), [=](const PatchFileEntry entry) {
        bool hash_exist = std::find(entry.hashes.cbegin(), entry.hashes.cend(),
                                    hash) != entry.hashes.cend();

        return entry.title_id == title_id && hash_exist;
      });

  return title_patches;
}

void PatchDB::ReadHashes(PatchFileEntry& patch_entry,
                         std::shared_ptr<cpptoml::base> hashes_node) const {
  auto add_hash = [&patch_entry](std::shared_ptr<cpptoml::base> node) {
    auto string_value = node->as<std::string>();
    if (!string_value) {
      return;
    }
    const std::string string_hash = string_value->get();
    if (string_hash.empty()) {
      return;
    }
    patch_entry.hashes.push_back(
        xe::string_util::from_string<uint64_t>(string_hash, true));
  };

  if (hashes_node->is_array()) {
    for (const auto& hash_entry : *hashes_node->as_array()) {
      add_hash(hash_entry);
    }
  } else {
    add_hash(hashes_node);
  }
}

void PatchDB::ReadPatchHeader(
    PatchInfoEntry& patch_info,
    std::shared_ptr<cpptoml::table> patch_fields) const {
  std::string patch_name = patch_fields->get_as<std::string>("name").value_or("");
  std::string patch_desc = patch_fields->get_as<std::string>("desc").value_or("");
  std::string patch_author =
      patch_fields->get_as<std::string>("author").value_or("");
  bool is_enabled = patch_fields->get_as<bool>("is_enabled").value_or(false);

  patch_info.id = 0;  // Todo(Gliniak): Implement id for future GUI stuff
  patch_info.patch_name = patch_name;
  patch_info.patch_desc = patch_desc;
  patch_info.patch_author = patch_author;
  patch_info.is_enabled = is_enabled;

  // Iterate through all available data sizes
  for (const auto& patch_data_type : patch_data_types_size_) {
    bool success =
        ReadPatchData(patch_info.patch_data, patch_data_type, patch_fields);

    if (!success) {
      XELOGE("PatchDB: Cannot read patch {}", patch_name);
      break;
    }
  }
}

}  // namespace patcher
}  // namespace xe
