// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"

namespace scimate {
struct ViewCel {
    std::uint16_t width = 0, height = 0;
    int x = 0, y = 0;
    std::uint8_t transparent = 0;
    Bytes pixels; // Top-to-bottom rows, left-to-right palette indices.
};
struct ViewLoop {
    int mirror_of = -1; // -1 = source loop; mirrors hold no separate cels.
    std::uint16_t unknown = 0;
    std::vector<ViewCel> cels;
};
struct View {
    std::uint16_t version = 0;
    std::uint16_t reserved = 0; // EGA header word; palette pointer only in VGA variants.
    std::vector<ViewLoop> loops;
    Bytes original; // Retained for byte-identical unchanged serialization.
};
bool operator==(const ViewCel& a, const ViewCel& b);
bool operator==(const ViewLoop& a, const ViewLoop& b);
bool same_view(const View& a, const View& b);

// SCI0 EGA views only. Limits: 50 loops, 128 cels/loop, 320x200/cel,
// 16 MiB total decoded source pixels. Errors throw std::runtime_error.
View read_sci0_view(const Bytes& payload);
Bytes write_sci0_view(const View& view);
// SCI1 VGA views, using the game's shared palette. Placement, transparent
// indices and mirrored loops are retained. Original EGA bytes are never reused.
View read_sci1_vga_view(const Bytes& payload);
Bytes write_sci1_vga_view(const View& view);
} // namespace scimate
