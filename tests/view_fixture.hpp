// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/view.hpp"
namespace fixture {
// Two loops sharing one source: loop 1 mirrors loop 0. 3x2 cel,
// placement (-2,3), transparent 0. Top row 1,2,2; bottom row transparent.
inline const scimate::Bytes view_bytes{
    2,0,2,0,7,0,0,0,12,0,12,0,
    1,0,0x34,0x12,18,0,
    3,0,2,0,0xfe,3,0,
    0x11,0x22,0x30
};
}
