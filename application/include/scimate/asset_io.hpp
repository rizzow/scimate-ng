// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <filesystem>
namespace scimate {
// Lossless, numbered ASCII TSV; non-ASCII bytes use \xHH escapes.
std::string export_sci0_text_table(const Bytes& payload);
Bytes import_sci0_text_table(const std::string& table);
struct AssetExportSummary {
  bool editable = false, conversion_error = false;
};
// All SCI0 resource types: standard media/text formats, semantic JSON records,
// or explicitly labelled byte-level JSON for layouts with unknown semantics.
AssetExportSummary export_sci0_asset(ResourceId id, const Bytes &payload,
                                     const std::filesystem::path &destination);
Bytes import_sci0_asset(const std::filesystem::path &directory,
                        ResourceId *id = nullptr);
void export_sci0_asset_file(ResourceId id, const std::filesystem::path &payload,
                            const std::filesystem::path &destination);
void import_sci0_asset_file(const std::filesystem::path &directory,
                            const std::filesystem::path &payload);
} // namespace scimate
