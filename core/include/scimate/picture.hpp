// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <array>
namespace scimate {
struct Picture {
    std::array<Bytes,3> planes; // visual, priority, control; 320x190, top to bottom.
    Bytes original;
    std::size_t terminator = 0;
};
Picture read_sci0_picture(const Bytes& payload);
Bytes write_sci0_picture(const Picture& picture);
using PicturePalette = std::array<std::array<std::uint8_t,3>,256>;
PicturePalette ega_picture_palette();
// SCI1 VGA full-room picture: embedded indexed bitmap and vector priority/control
// planes. Requires a VGA picture decoder; it is not an EGA-mode patch.
Bytes write_sci1_vga_picture(const Picture& picture, const PicturePalette& palette);
}
