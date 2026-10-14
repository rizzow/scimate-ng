// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/picture.hpp"
#include "scimate/format_constants.hpp"
#include <stdexcept>
#include <string>
#include <utility>

namespace scimate {
namespace {
class VgaPictureDecoder {
    static constexpr unsigned width = format::picture_width, height = format::picture_height;
    static constexpr unsigned pixels = width*height;
    const Bytes& bytes;
    std::size_t cursor = 0;
    VgaPicture result{};
    int visual = -1, priority = -1, control = -1;
    [[noreturn]] void fail(const char* reason) const {
        throw std::runtime_error("SCI1 VGA picture at byte "+std::to_string(cursor)+": "+reason);
    }
    unsigned byte() { if (cursor == bytes.size()) fail("truncated command"); return bytes[cursor++]; }
    unsigned word() { const auto low = byte(); return low | (byte()<<8); }
    std::pair<unsigned,unsigned> coordinate() {
        const auto high = byte(), x = byte(), y = byte();
        const auto point = std::pair{((high & 0xf0)<<4)|x,((high & 0x0f)<<8)|y};
        if (point.first >= width || point.second >= height) fail("coordinate outside picture");
        return point;
    }
    void write(unsigned plane, unsigned offset, unsigned value) {
        result.picture.planes[plane][offset] = static_cast<std::uint8_t>(value);
        result.picture.written[plane][offset] = 1;
    }
    void line(std::pair<unsigned,unsigned> first, std::pair<unsigned,unsigned> last) {
        const int dx = static_cast<int>(last.first)-static_cast<int>(first.first);
        const int dy = static_cast<int>(last.second)-static_cast<int>(first.second);
        const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        // The encoder emits only axis-aligned or 45-degree exact pixel runs.
        if (ax && ay && ax != ay) fail("unsupported non-exact vector line");
        const int steps = ax > ay ? ax : ay;
        int x = first.first, y = first.second;
        for (int i = 0; i <= steps; ++i) {
            const auto offset = static_cast<unsigned>(y)*width+static_cast<unsigned>(x);
            for (const auto plane : {std::pair{0u,visual},std::pair{1u,priority},std::pair{2u,control}})
                if (plane.second >= 0) write(plane.first,offset,plane.second);
            x += (dx > 0)-(dx < 0); y += (dy > 0)-(dy < 0);
        }
    }
    void palette() {
        unsigned mapping[256];
        for (auto& index : mapping) index = byte();
        for (unsigned i = 0; i < 4; ++i) byte(); // Timestamp.
        for (unsigned i = 0; i < 256; ++i) {
            const auto used = byte();
            std::array<std::uint8_t,3> rgb{};
            for (auto& channel : rgb) channel = static_cast<std::uint8_t>(byte());
            if (used) result.palette[mapping[i]] = rgb;
        }
    }
    void bitmap() {
        const auto origin = coordinate();
        const auto size = word();
        if (size < 8 || size > bytes.size()-cursor) fail("invalid bitmap size");
        const auto end = cursor+size;
        const auto columns = word(), rows = word();
        const auto x_offset = byte(), y_offset = byte(), clear = byte(), reserved = byte();
        if (!columns || !rows || columns > width-origin.first || rows > height-origin.second ||
            x_offset || y_offset || reserved) fail("unsupported bitmap geometry");
        unsigned produced = 0;
        const auto total = columns*rows;
        while (produced < total) {
            if (cursor >= end) fail("truncated bitmap run");
            const auto command = byte(), kind = command & 0xc0;
            const auto count = kind < 0x80 ? command : command & 0x3f;
            if (!count || count > total-produced) fail("invalid bitmap run length");
            unsigned repeated = clear;
            if (kind == 0x80) { if (cursor >= end) fail("truncated repeated pixel"); repeated = byte(); }
            for (unsigned i = 0; i < count; ++i,++produced) {
                unsigned value = repeated;
                if (kind < 0x80) { if (cursor >= end) fail("truncated literal pixels"); value = byte(); }
                if (value != clear) write(0,(origin.second+produced/columns)*width+origin.first+produced%columns,value);
            }
        }
        if (cursor != end) fail("trailing bitmap bytes");
    }
public:
    VgaPictureDecoder(const Bytes& payload, const Picture* background): bytes(payload) {
        if (background) result.picture.planes = background->planes;
        else result.picture.planes = {Bytes(pixels,15),Bytes(pixels,0),Bytes(pixels,0)};
        for (const auto& plane : result.picture.planes) if (plane.size() != pixels) fail("invalid background dimensions");
        for (auto& mask : result.picture.written) mask.assign(pixels,0);
    }
    VgaPicture decode() {
        while (cursor < bytes.size()) switch (byte()) {
        case 0xf0: visual = byte(); break;
        case 0xf1: visual = -1; break;
        case 0xf2: priority = byte(); if (priority > 15) fail("invalid priority"); break;
        case 0xf3: priority = -1; break;
        case 0xfb: control = byte(); if (control > 15) fail("invalid control"); break;
        case 0xfc: control = -1; break;
        case 0xf6: {
            auto point = coordinate();
            while (cursor < bytes.size() && bytes[cursor] < 0xf0) {
                const auto next = coordinate(); line(point,next); point = next;
            }
            break;
        }
        case 0xfe: {
            const auto extended = byte();
            if (extended == 2) palette();
            else if (extended == 1) bitmap();
            else fail("unsupported extended opcode");
            break;
        }
        case 0xff:
            if (cursor != bytes.size()) fail("trailing picture bytes");
            result.picture.original = bytes; result.picture.terminator = cursor-1; return std::move(result);
        default: fail("unsupported vector opcode");
        }
        fail("missing terminator");
    }
};
}
VgaPicture read_sci1_vga_picture(const Bytes& payload, const Picture* background) {
    return VgaPictureDecoder(payload,background).decode();
}
}
