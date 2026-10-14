// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/picture.hpp"
#include <filesystem>
namespace scimate {
Bytes encode_picture_plane_png(const Bytes& pixels);
Bytes decode_picture_plane_png(const Bytes& png);
void export_sci0_picture(const Bytes& payload, const std::filesystem::path& destination);
Bytes import_sci0_picture(const std::filesystem::path& directory);
VgaPicture import_vga_picture_edit(const std::filesystem::path& directory);
// Preserve EGA indices 0..15 and assign additional opaque RGB colours to 16..254.
VgaPicture decode_vga_picture_visual_png(const Bytes& png);
void export_sci0_picture_file(const std::filesystem::path& payload, const std::filesystem::path& destination);
void import_sci0_picture_file(const std::filesystem::path& directory, const std::filesystem::path& payload);
}
