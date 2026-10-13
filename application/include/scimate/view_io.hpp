// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/view.hpp"
#include <filesystem>

namespace scimate {
// PNG adapter: indexed EGA output; palette/RGB/RGBA input with exact EGA
// colors and binary alpha. Opaque use of the transparent index is rejected.
Bytes encode_cel_png(const ViewCel& cel);
ViewCel decode_cel_png(const Bytes& png, std::uint8_t transparent);

// Creates a new directory with original.bin, view.tsv, and source cel PNGs.
// Editable columns: x, y, transparent. PNG controls dimensions and pixels.
void export_sci0_view(const Bytes& payload, const std::filesystem::path& destination);
Bytes import_sci0_view(const std::filesystem::path& directory);
// Standalone decoded view payload helpers. Outputs must not already exist.
void export_sci0_view_file(const std::filesystem::path& payload, const std::filesystem::path& destination);
void import_sci0_view_file(const std::filesystem::path& directory, const std::filesystem::path& payload);
} // namespace scimate
