// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <array>
namespace scimate {
struct Picture {
    std::array<Bytes,3> planes; // visual, priority, control; 320x190, top to bottom.
    Bytes original;
    std::size_t terminator = 0;
    std::array<Bytes,3> written; // Pixels explicitly drawn, including white/zero.
    bool contains_fill = false;
};
Picture read_sci0_picture(const Bytes& payload);
Bytes write_sci0_picture(const Picture& picture);
using PicturePalette = std::array<std::array<std::uint8_t,3>,256>;
PicturePalette ega_picture_palette();
enum class VgaPictureMode { background, overlay };
// Backgrounds use an embedded bitmap. Overlays use vector writes to retain
// untouched pixels and explicit zero masks; background-dependent fills are
// refused. Requires a VGA decoder, not an EGA-mode patch.
Bytes write_sci1_vga_picture(const Picture& picture, const PicturePalette& palette,
                           VgaPictureMode mode = VgaPictureMode::background);
}
