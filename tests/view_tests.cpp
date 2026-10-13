// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_fixtures.hpp"
#include "view_fixture.hpp"
#include <iostream>

int main() {
    using namespace fixture;
    try {
        auto view = read_sci0_view(view_bytes);
        require(view.loops.size() == 2 && view.loops[1].mirror_of == 0, "mirror decoding");
        const auto& cel = view.loops[0].cels[0];
        require(cel.width == 3 && cel.height == 2 && cel.x == -2 && cel.y == 3 && cel.transparent == 0, "cel metadata");
        require(cel.pixels == Bytes({1,2,2,0,0,0}), "pixel orientation/RLE");
        require(write_sci0_view(view) == view_bytes, "unchanged view not exact");
        auto padded = view_bytes; padded.push_back(0xab);
        require(write_sci0_view(read_sci0_view(padded)) == padded, "trailing bytes not preserved");
        auto changed = view;
        changed.loops[0].cels[0].pixels = {1,1,2,3,0,0};
        changed.loops[0].cels[0].x = -128;
        const auto encoded = write_sci0_view(changed);
        require(same_view(changed,read_sci0_view(encoded)), "edited view mismatch");
        require(encoded[8] == encoded[10] && encoded[9] == encoded[11] && encoded[2] == 2, "mirror table not preserved");
        require(read_sci0_view(encoded).loops[0].unknown == 0x1234, "unknown loop data lost");
        // Exercise row crossing, zero-count runs, and runs longer than 15 pixels.
        auto cross = view_bytes; cross.resize(25); cross.push_back(0x61);
        require(read_sci0_view(cross).loops[0].cels[0].pixels == Bytes(6,1), "cross-row run");
        cross.insert(cross.begin()+25,0);
        require(read_sci0_view(cross).loops[0].cels[0].pixels == Bytes(6,1), "zero-count command");
        auto wide = view;
        wide.loops[0].cels[0].width = 320; wide.loops[0].cels[0].height = 200;
        wide.loops[0].cels[0].pixels.assign(64000,15);
        require(same_view(wide,read_sci0_view(write_sci0_view(wide))), "long runs/max dimensions");
        auto empty = view; empty.loops[0].cels.clear();
        require(same_view(empty,read_sci0_view(write_sci0_view(empty))), "empty loop preservation");
        for (std::size_t n = 0; n < view_bytes.size(); ++n)
            fails([&] { read_sci0_view(Bytes(view_bytes.begin(),view_bytes.begin()+static_cast<std::ptrdiff_t>(n))); }, "SCI0 view");
        auto bad = view_bytes; bad.back() = 0x40;
        fails([&] { read_sci0_view(bad); }, "exceeds pixel");
        bad = view_bytes; bad[2] = 3;
        fails([&] { read_sci0_view(bad); }, "no matching source");
        bad = view_bytes; bad[24] = 16;
        fails([&] { read_sci0_view(bad); }, "transparent");
        bad = view_bytes; bad[8] = 0;
        fails([&] { read_sci0_view(bad); }, "loop offset");
        bad = view_bytes; bad[6] = 1;
        auto reserved=read_sci0_view(bad); reserved.loops[0].cels[0].x=0;
        require(read_sci0_view(write_sci0_view(reserved)).reserved==1,"reserved EGA header word lost");
        bad = view_bytes; bad[1] = 0x80;
        fails([&] { read_sci0_view(bad); }, "unsupported header");
        changed = view; changed.loops[0].cels[0].x = 128;
        fails([&] { write_sci0_view(changed); }, "placement");
        changed = view; changed.loops[0].cels[0].pixels[0] = 16;
        fails([&] { write_sci0_view(changed); }, "pixel outside");
        changed = view; changed.loops[0].mirror_of = 1;
        fails([&] { write_sci0_view(changed); }, "mirror");
        for (std::size_t i = 0; i < wide.loops[0].cels[0].pixels.size(); ++i)
            wide.loops[0].cels[0].pixels[i] = static_cast<std::uint8_t>(i%16);
        wide.loops[0].cels.push_back(wide.loops[0].cels[0]);
        fails([&] { write_sci0_view(wide); }, "65535");
        std::uint32_t state = 0x987654;
        for (unsigned i = 0; i < 2000; ++i) {
            bad = view_bytes; state = state*1664525u+1013904223u;
            bad[state%bad.size()] = static_cast<std::uint8_t>(state>>24);
            try { read_sci0_view(bad); } catch (const std::runtime_error&) {}
        }
        std::cout << "SCI0 view tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
