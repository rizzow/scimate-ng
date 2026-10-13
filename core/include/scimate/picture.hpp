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
}
