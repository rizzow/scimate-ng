/***************************************************************************
    Copyright (c) 2015 Philip Fortier

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
***************************************************************************/
// SCI0 EGA layout/RLE adapted from Resources/View.cpp; see docs/provenance.md.
#include "scimate/view.hpp"
#include <algorithm>
#include <string>

namespace scimate {
namespace {
[[noreturn]] void invalid(const char* message) { throw std::runtime_error(std::string("SCI0 view: ") + message); }
unsigned word(const Bytes& b, std::size_t p) {
    if (p > b.size() || b.size() - p < 2) invalid("truncated word");
    return b[p] | (static_cast<unsigned>(b[p+1]) << 8);
}
void put(Bytes& b, unsigned n) { b.push_back(static_cast<std::uint8_t>(n)); b.push_back(static_cast<std::uint8_t>(n >> 8)); }
void patch(Bytes& b, std::size_t p, std::size_t n) {
    if (n > 65535) invalid("16-bit offset/size limit exceeded");
    b.at(p) = static_cast<std::uint8_t>(n); b.at(p+1) = static_cast<std::uint8_t>(n >> 8);
}
int signed_byte(std::uint8_t n) { return n < 128 ? n : static_cast<int>(n) - 256; }
void validate(const View& view, bool vga = false) {
    if (view.loops.empty() || view.loops.size() > 50) invalid("expected 1..50 loops");
    std::size_t total = 0;
    for (std::size_t i = 0; i < view.loops.size(); ++i) {
        const auto& loop = view.loops[i];
        if (loop.mirror_of != -1) {
            if (i >= 16 || loop.mirror_of < 0 || static_cast<std::size_t>(loop.mirror_of) >= view.loops.size() ||
                view.loops[static_cast<std::size_t>(loop.mirror_of)].mirror_of != -1 || !loop.cels.empty())
                invalid("invalid mirror relationship");
            continue;
        }
        if (loop.cels.size() > 128) invalid("more than 128 cels in a loop");
        for (const auto& cel : loop.cels) {
            if (!cel.width || !cel.height || cel.width > 320 || cel.height > 200) invalid("cel dimensions outside 1..320 by 1..200");
            if (cel.x < -128 || cel.x > 127 || cel.y < -128 || cel.y > 127) invalid("placement outside signed-byte range");
            if (!vga && cel.transparent > 15) invalid("transparent index outside EGA palette");
            if (cel.pixels.size() != static_cast<std::size_t>(cel.width) * cel.height) invalid("pixel count differs from dimensions");
            if (!vga && std::any_of(cel.pixels.begin(), cel.pixels.end(), [](auto p) { return p > 15; })) invalid("pixel outside EGA palette");
            total += cel.pixels.size();
            if (total > 16 * 1024 * 1024) invalid("decoded pixels exceed 16 MiB limit");
        }
    }
}
} // namespace
bool operator==(const ViewCel& a, const ViewCel& b) {
    return a.width == b.width && a.height == b.height && a.x == b.x && a.y == b.y &&
           a.transparent == b.transparent && a.pixels == b.pixels;
}
bool operator==(const ViewLoop& a, const ViewLoop& b) {
    return a.mirror_of == b.mirror_of && a.unknown == b.unknown && a.cels == b.cels;
}
bool same_view(const View& a, const View& b) { return a.version == b.version && a.reserved == b.reserved && a.loops == b.loops; }

namespace {
constexpr unsigned vga_flag = 0x80, vga_repeat = 0x80, vga_skip = 0xc0;
constexpr unsigned vga_command_mask = 0xc0, vga_run_mask = 0x3f;
constexpr std::size_t vga_literal_limit = 127, profitable_repeat_length = 3;
constexpr std::size_t view_header_bytes = 8, ega_cel_header_bytes = 7, vga_cel_header_bytes = 8;
View read_view(const Bytes& b, bool vga) {
    if (b.size() > 65535) invalid("payload exceeds 65535 bytes");
    const auto header = word(b, 0), mask = word(b, 2);
    if (vga && (header >> 8) != vga_flag) invalid("unsupported SCI1 VGA flags");
    const auto count = vga ? header & 0xff : header;
    if (count < 1 || count > 50) invalid("unsupported header or loop count");
    if (count < 16 && (mask >> count)) invalid("mirror bits outside loop table");
    View view;
    view.version = static_cast<std::uint16_t>(word(b, 4));
    view.reserved = static_cast<std::uint16_t>(word(b, 6));
    view.loops.resize(count);
    std::vector<unsigned> offsets;
    for (unsigned i = 0; i < count; ++i) {
        const auto offset = word(b, 8 + i*2);
        if (offset < 8 + count*2 || offset >= b.size()) invalid("loop offset outside data");
        offsets.push_back(offset);
    }
    std::size_t total = 0;
    for (unsigned i = 0; i < count; ++i) {
        auto& loop = view.loops[i];
        if (i < 16 && (mask & (1u << i))) {
            for (unsigned source = 0; source < count; ++source)
                if ((source >= 16 || !(mask & (1u << source))) && offsets[source] == offsets[i]) {
                    loop.mirror_of = static_cast<int>(source); break;
                }
            if (loop.mirror_of == -1) invalid("mirror has no matching source loop");
            continue;
        }
        const auto start = offsets[i];
        const auto cels = word(b, start);
        if (cels > 128) invalid("more than 128 cels in a loop");
        loop.unknown = static_cast<std::uint16_t>(word(b, start + 2));
        for (unsigned c = 0; c < cels; ++c) {
            std::size_t p = word(b, start + 4 + c*2);
            const auto cel_header = vga ? vga_cel_header_bytes : ega_cel_header_bytes;
            if (p < view_header_bytes + count*2 || p > b.size() || b.size() - p < cel_header) invalid("cel header outside data");
            ViewCel cel;
            cel.width = static_cast<std::uint16_t>(word(b,p)); cel.height = static_cast<std::uint16_t>(word(b,p+2));
            cel.x = signed_byte(b[p+4]); cel.y = signed_byte(b[p+5]); cel.transparent = b[p+6]; p += cel_header;
            if (!cel.width || !cel.height || cel.width > 320 || cel.height > 200) invalid("cel dimensions outside supported range");
            const auto pixels = static_cast<std::size_t>(cel.width) * cel.height;
            total += pixels;
            if (total > 16 * 1024 * 1024) invalid("decoded pixels exceed 16 MiB limit");
            cel.pixels.reserve(pixels);
            while (cel.pixels.size() < pixels) {
                if (p == b.size()) invalid("truncated cel RLE");
                const auto command = b[p++];
                const auto kind = command & vga_command_mask;
                const auto run = static_cast<std::size_t>(vga ? (kind < vga_repeat ? command : command & vga_run_mask) : command >> 4);
                if (!run && vga) invalid("zero-length VGA cel RLE command");
                if (run > pixels - cel.pixels.size()) invalid("cel RLE exceeds pixel count");
                if (!vga) cel.pixels.insert(cel.pixels.end(), run, static_cast<std::uint8_t>(command & 15));
                else if (kind < vga_repeat) {
                    if (run > b.size()-p) invalid("truncated VGA literal run");
                    cel.pixels.insert(cel.pixels.end(),b.begin()+p,b.begin()+p+run); p += run;
                } else if (kind == vga_skip) cel.pixels.insert(cel.pixels.end(),run,cel.transparent);
                else {
                    if (p == b.size()) invalid("truncated VGA repeat run");
                    cel.pixels.insert(cel.pixels.end(),run,b[p++]);
                }
            }
            loop.cels.push_back(std::move(cel));
        }
    }
    validate(view,vga);
    view.original = b;
    return view;
}
} // namespace
View read_sci0_view(const Bytes& b) { return read_view(b,false); }
View read_sci1_vga_view(const Bytes& b) { return read_view(b,true); }

Bytes write_sci0_view(const View& view) {
    validate(view);
    if (!view.original.empty() && same_view(view, read_sci0_view(view.original))) return view.original;
    Bytes b;
    put(b, static_cast<unsigned>(view.loops.size()));
    unsigned mask = 0;
    for (std::size_t i = 0; i < view.loops.size(); ++i) if (view.loops[i].mirror_of >= 0) mask |= 1u << i;
    put(b,mask); put(b,view.version); put(b,view.reserved);
    b.resize(8 + view.loops.size()*2);
    std::vector<std::size_t> offsets(view.loops.size());
    for (std::size_t i = 0; i < view.loops.size(); ++i) {
        const auto& loop = view.loops[i];
        if (loop.mirror_of >= 0) continue;
        offsets[i] = b.size();
        put(b,static_cast<unsigned>(loop.cels.size())); put(b,loop.unknown);
        const auto table = b.size(); b.resize(b.size() + loop.cels.size()*2);
        for (std::size_t c = 0; c < loop.cels.size(); ++c) {
            const auto& cel = loop.cels[c];
            patch(b,table+c*2,b.size());
            put(b,cel.width); put(b,cel.height);
            b.push_back(static_cast<std::uint8_t>(cel.x)); b.push_back(static_cast<std::uint8_t>(cel.y)); b.push_back(cel.transparent);
            // Keep runs within rows for compatibility with legacy tools.
            for (std::size_t y = 0; y < cel.height; ++y) {
                std::size_t x = 0;
                while (x < cel.width) {
                    const auto color = cel.pixels[y*cel.width+x];
                    std::size_t run = 1;
                    while (run < 15 && x+run < cel.width && cel.pixels[y*cel.width+x+run] == color) ++run;
                    b.push_back(static_cast<std::uint8_t>((run << 4) | color)); x += run;
                }
            }
            if (b.size() > 65535) invalid("encoded view exceeds 65535 bytes");
        }
    }
    for (std::size_t i = 0; i < view.loops.size(); ++i)
        patch(b,8+i*2,view.loops[i].mirror_of >= 0 ? offsets[static_cast<std::size_t>(view.loops[i].mirror_of)] : offsets[i]);
    if (b.size() > 65535) invalid("encoded view exceeds 65535 bytes");
    if (!same_view(view,read_sci0_view(b))) invalid("encoded view verification failed");
    return b;
}
Bytes write_sci1_vga_view(const View& view) {
    validate(view,true);
    Bytes bytes;
    put(bytes,static_cast<unsigned>(view.loops.size()) | (vga_flag << 8));
    unsigned mirror_mask = 0;
    for (std::size_t i = 0; i < view.loops.size(); ++i)
        if (view.loops[i].mirror_of >= 0) mirror_mask |= 1u << i;
    put(bytes,mirror_mask);
    put(bytes,1); // SCI1 VGA format version.
    put(bytes,0); // Shared game palette; no embedded palette.
    bytes.resize(view_header_bytes+view.loops.size()*2);
    std::vector<std::size_t> offsets(view.loops.size());
    for (std::size_t i = 0; i < view.loops.size(); ++i) {
        const auto& loop = view.loops[i];
        if (loop.mirror_of >= 0) continue;
        offsets[i] = bytes.size();
        put(bytes,static_cast<unsigned>(loop.cels.size())); put(bytes,loop.unknown);
        const auto table = bytes.size(); bytes.resize(table+loop.cels.size()*2);
        for (std::size_t c = 0; c < loop.cels.size(); ++c) {
            const auto& cel = loop.cels[c];
            patch(bytes,table+c*2,bytes.size());
            put(bytes,cel.width); put(bytes,cel.height);
            bytes.push_back(static_cast<std::uint8_t>(cel.x));
            bytes.push_back(static_cast<std::uint8_t>(cel.y));
            bytes.push_back(cel.transparent); bytes.push_back(0);
            // Each run is bounded by the six-bit VGA run field. Transparent
            // runs carry no literal colour and preserve the cel's clear index.
            std::size_t cursor = 0;
            while (cursor < cel.pixels.size()) {
                const auto colour = cel.pixels[cursor];
                std::size_t run = 1;
                while (run < vga_run_mask && cursor+run < cel.pixels.size() && cel.pixels[cursor+run] == colour) ++run;
                if (colour == cel.transparent || run >= profitable_repeat_length) {
                    bytes.push_back(static_cast<std::uint8_t>((colour == cel.transparent ? vga_skip : vga_repeat) | run));
                    if (colour != cel.transparent) bytes.push_back(colour);
                    cursor += run;
                } else {
                    const auto start = cursor++;
                    while (cursor-start < vga_literal_limit && cursor < cel.pixels.size()) {
                        const auto next = cel.pixels[cursor];
                        if (next == cel.transparent ||
                            (cursor+2 < cel.pixels.size() && next == cel.pixels[cursor+1] && next == cel.pixels[cursor+2])) break;
                        ++cursor;
                    }
                    bytes.push_back(static_cast<std::uint8_t>(cursor-start));
                    bytes.insert(bytes.end(),cel.pixels.begin()+start,cel.pixels.begin()+cursor);
                }
            }
            if (bytes.size() > 65535) invalid("encoded VGA view exceeds 65535 bytes");
        }
    }
    for (std::size_t i = 0; i < view.loops.size(); ++i)
        patch(bytes,view_header_bytes+i*2,view.loops[i].mirror_of >= 0 ? offsets[static_cast<std::size_t>(view.loops[i].mirror_of)] : offsets[i]);
    if (bytes.size() > 65535) invalid("encoded VGA view exceeds 65535 bytes");
    if (read_sci1_vga_view(bytes).loops != view.loops) invalid("encoded VGA view verification failed");
    return bytes;
}
} // namespace scimate
