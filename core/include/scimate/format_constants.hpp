// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace scimate::format {
inline constexpr unsigned max_sci0_resource_number = 2047, last_sci0_resource_type = 9;
inline constexpr std::size_t max_payload_bytes = 65535, max_interchange_bytes = 16 * 1024 * 1024;
inline constexpr unsigned early_sound_header_bytes = 17, late_sound_header_bytes = 33;
inline constexpr int picture_width = 320, picture_height = 190, max_cel_height = 200;
inline constexpr std::array<std::uint32_t, 16> ega_rgb{
    0x000000,0x0000aa,0x00aa00,0x00aaaa,0xaa0000,0xaa00aa,0xaa5500,0xaaaaaa,
    0x555555,0x5555ff,0x55ff55,0x55ffff,0xff5555,0xff55ff,0xffff55,0xffffff};
}
