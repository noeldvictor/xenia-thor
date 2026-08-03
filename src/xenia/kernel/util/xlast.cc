/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/xlast.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"

namespace xe {
namespace kernel {
namespace util {

XLastMatchmakingQuery::XLastMatchmakingQuery() {}
XLastMatchmakingQuery::XLastMatchmakingQuery(
    const pugi::xpath_node query_node) {
  node_ = query_node;
}

std::string XLastMatchmakingQuery::GetName() const {
  return node_.node().attribute("friendlyName").value();
}

std::vector<uint32_t> XLastMatchmakingQuery::GetReturns() const {
  return XLast::GetAllValuesFromNode(node_, "Returns", "id");
}

std::vector<uint32_t> XLastMatchmakingQuery::GetParameters() const {
  return XLast::GetAllValuesFromNode(node_, "Parameters", "id");
}

std::vector<uint32_t> XLastMatchmakingQuery::GetFilters() const {
  return XLast::GetAllValuesFromNode(node_, "Filters", "left");
}

XLast::XLast(const uint8_t* compressed_xml_data,
             const uint32_t compressed_data_size,
             const uint32_t decompressed_data_size) {
  if (!compressed_data_size || !decompressed_data_size) {
    XELOGW("XLast: Current title don't have any XLast XML data!");
    return;
  }

  parsed_xlast_ = std::make_unique<pugi::xml_document>();

  // NOTE(kernel-port): Edge gzip-inflates the XLast XML here with zlib-ng
  // (third_party/zlib-ng). We do not vendor zlib-ng - and have no gzip
  // implementation in-tree at all - so decompression is stubbed out.
  //
  // Consequence: xlast_decompressed_xml_ stays empty, so HasXLast() returns
  // false and every accessor below returns its documented empty value. Callers
  // (game_info_database, presence_string_builder, xdbf/spa_info) already handle
  // the no-XLast case, so this degrades gracefully to "title has no XLast XML"
  // - achievement/presence strings sourced from XLast are simply unavailable.
  //
  // Re-enable path: vendor third_party/zlib-ng (+ its premake lua), add it to
  // the kernel project links, restore `#include "third_party/zlib-ng/zlib-ng.h"`
  // and the zng_inflateInit2/zng_inflate/zng_inflateEnd block that filled
  // xlast_decompressed_xml_ (resize to decompressed_data_size first), then
  // load_buffer it into parsed_xlast_ as below.
  (void)compressed_xml_data;
  XELOGW(
      "XLast: XML decompression is not available in this build (zlib-ng not "
      "vendored); treating title as having no XLast data.");
}

std::u16string XLast::GetTitleName() const {
  std::string xpath = "/XboxLiveSubmissionProject/GameConfigProject";

  if (!HasXLast()) {
    return std::u16string();
  }

  const pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return std::u16string();
  }

  return xe::to_utf16(node.node().attribute("titleName").value());
}

std::map<ProductInformationEntry, uint32_t>
XLast::GetProductInformationAttributes() const {
  std::map<ProductInformationEntry, uint32_t> attributes;

  std::string xpath =
      "/XboxLiveSubmissionProject/GameConfigProject/ProductInformation";

  if (!HasXLast()) {
    return attributes;
  }

  const pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return attributes;
  }

  const auto node_attributes = node.node().attributes();
  for (const auto& attribute : node_attributes) {
    const auto entry =
        product_information_entry_string_to_enum.find(attribute.name());
    if (entry == product_information_entry_string_to_enum.cend()) {
      XELOGW("GetProductInformationAttributes: Missing attribute: {}",
             attribute.name());
      continue;
    }

    std::string attribute_value = std::string(attribute.value());
    if (attribute_value.empty()) {
      XELOGW(
          "GetProductInformationAttributes: Attribute: {} Contains no value!",
          attribute.name());
      continue;
    }

    attributes.emplace(entry->second,
                       xe::string_util::from_string<uint32_t>(attribute_value));
  }

  return attributes;
}

std::vector<XLanguage> XLast::GetSupportedLanguages() const {
  std::vector<XLanguage> languages;

  std::string xpath = fmt::format(
      "/XboxLiveSubmissionProject/GameConfigProject/LocalizedStrings");

  if (!HasXLast()) {
    return languages;
  }

  const pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return languages;
  }

  const auto locale = node.node().children("SupportedLocale");
  for (auto itr = locale.begin(); itr != locale.end(); itr++) {
    const std::string locale_name = itr->attribute("locale").value();

    for (const auto& language : language_mapping) {
      if (language.second == locale_name) {
        languages.push_back(language.first);
      }
    }
  }

  return languages;
}

std::u16string XLast::GetLocalizedString(uint32_t string_id,
                                         XLanguage language) const {
  std::string xpath = fmt::format(
      "/XboxLiveSubmissionProject/GameConfigProject/LocalizedStrings/"
      "LocalizedString[@id = \"{}\"]",
      string_id);

  if (!HasXLast()) {
    return std::u16string();
  }

  const pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return std::u16string();
  }

  const std::string locale_name = GetLocaleStringFromLanguage(language);
  const pugi::xml_node locale_node =
      node.node().find_child_by_attribute("locale", locale_name.c_str());

  if (!locale_node) {
    return std::u16string();
  }

  return xe::to_utf16(locale_node.child_value());
}

const std::optional<uint32_t> XLast::GetPresenceStringId(
    const uint32_t context_id) {
  std::string xpath = fmt::format(
      "/XboxLiveSubmissionProject/GameConfigProject/Presence/"
      "PresenceMode[@contextValue = \"{}\"]",
      context_id);

  std::optional<uint32_t> id = std::nullopt;

  if (!HasXLast()) {
    return id;
  }

  pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());

  if (node) {
    const auto string_id = node.node().attribute("stringId").value();
    id = xe::string_util::from_string<uint32_t>(string_id);
  }

  return id;
}

const std::optional<uint32_t> XLast::GetPropertyStringId(
    const uint32_t property_id) {
  std::string xpath = fmt::format(
      "/XboxLiveSubmissionProject/GameConfigProject/Properties/Property[@id = "
      "\"0x{:08X}\"]",
      property_id);

  std::optional<uint32_t> value = std::nullopt;

  if (!HasXLast()) {
    return value;
  }

  pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());

  if (node) {
    const auto string_id_value = node.node().attribute("stringId").value();
    value = xe::string_util::from_string<uint32_t>(string_id_value);
  }

  return value;
}

const std::u16string XLast::GetPresenceRawString(const uint32_t presence_value,
                                                 const XLanguage language) {
  const std::optional<uint32_t> presence_string_id =
      GetPresenceStringId(presence_value);

  std::u16string raw_presence = u"";

  if (presence_string_id.has_value()) {
    raw_presence = GetLocalizedString(presence_string_id.value(), language);
  }

  return raw_presence;
}

const std::optional<uint32_t> XLast::GetContextStringId(
    const uint32_t context_id, const uint32_t context_value) {
  std::string xpath = fmt::format(
      "/XboxLiveSubmissionProject/GameConfigProject/Contexts/Context[@id = "
      "\"0x{:08X}\"]/ContextValue[@value = \"{}\"]",
      context_id, context_value);

  std::optional<uint32_t> value = std::nullopt;

  if (!HasXLast()) {
    return value;
  }

  pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());

  if (node) {
    // const auto default_value =
    //     node.node().parent().attribute("defaultValue").value();
    // value = xe::string_util::from_string<uint32_t>(default_value);

    const auto string_id_value = node.node().attribute("stringId").value();
    value = xe::string_util::from_string<uint32_t>(string_id_value);
  }

  return value;
}

XLastMatchmakingQuery* XLast::GetMatchmakingQuery(
    const uint32_t query_id) const {
  std::string xpath = fmt::format(
      "/XboxLiveSubmissionProject/GameConfigProject/Matchmaking/Queries/"
      "Query[@id = \"{}\"]",
      query_id);

  XLastMatchmakingQuery* query = nullptr;

  if (!HasXLast()) {
    return query;
  }

  pugi::xpath_node node = parsed_xlast_->select_node(xpath.c_str());
  if (!node) {
    return query;
  }

  return new XLastMatchmakingQuery(node);
}

std::vector<uint32_t> XLast::GetAllValuesFromNode(
    const pugi::xpath_node node, const std::string child_name,
    const std::string attribute_name) {
  std::vector<uint32_t> result{};

  const auto searched_child = node.node().child(child_name.c_str());

  for (pugi::xml_node_iterator itr = searched_child.begin();
       itr != searched_child.end(); itr++) {
    result.push_back(xe::string_util::from_string<uint32_t>(
        itr->attribute(attribute_name.c_str()).value(), true));
  }

  return result;
}

void XLast::Dump(std::string file_name) const {
  if (!HasXLast()) {
    return;
  }

  if (file_name.empty()) {
    file_name = xe::to_utf8(GetTitleName());
  }

  FILE* outfile =
      xe::filesystem::OpenFile(fmt::format("{}.xml", file_name).c_str(), "ab");
  if (!outfile) {
    return;
  }

  fwrite(xlast_decompressed_xml_.data(), 1, xlast_decompressed_xml_.size(),
         outfile);
  fclose(outfile);
}

std::string XLast::GetLocaleStringFromLanguage(XLanguage language) const {
  const auto value = language_mapping.find(language);
  if (value != language_mapping.cend()) {
    return value->second;
  }

  return language_mapping.at(XLanguage::kEnglish);
}

}  // namespace util
}  // namespace kernel
}  // namespace xe
