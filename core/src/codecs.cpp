// SPDX-License-Identifier: GPL-2.0-or-later
// SCI0 algorithms adapted from SCI Companion's FreeSCI-derived Codec.cpp.
// Original algorithm credits: Carl Muckenhoupt and Sergey Lapin.
// See docs/provenance.md. Rewritten with bounded input/output and no platform APIs.
#include "scimate/resource_game.hpp"

#include <array>
#include <stdexcept>

namespace scimate {
namespace {
[[noreturn]] void invalid(const char* message) { throw std::runtime_error(message); }

class Bits {
public:
    Bits(const Bytes& bytes, bool msb, std::size_t offset = 0)
        : bytes_(bytes), msb_(msb), bit_(offset * 8) {}
    unsigned read(unsigned count) {
        if (count > bytes_.size() * 8 - bit_) invalid("truncated compressed bitstream");
        unsigned result = 0;
        for (unsigned i = 0; i < count; ++i) {
            const unsigned shift = static_cast<unsigned>(bit_ % 8);
            const unsigned value = (bytes_[bit_ / 8] >> (msb_ ? 7 - shift : shift)) & 1u;
            if (msb_) result = (result << 1) | value;
            else result |= value << i;
            ++bit_;
        }
        return result;
    }
private:
    const Bytes& bytes_;
    bool msb_;
    std::size_t bit_;
};

Bytes lzw(const Bytes& input, std::size_t expected) {
    // SCI0 registers each emitted string as an output slice; a reference
    // reproduces that slice plus its following byte (possibly overlapping).
    std::array<std::size_t, 4096> offsets{};
    std::array<std::size_t, 4096> lengths{};
    unsigned width = 9, next = 258;
    Bits bits(input, false);
    Bytes output;
    output.reserve(expected);
    for (;;) {
        const auto token = bits.read(width);
        if (token == 257) break;
        if (token == 256) { width = 9; next = 258; continue; }
        const auto start = output.size();
        if (token < 256) {
            if (output.size() == expected) invalid("LZW output exceeds declared size");
            output.push_back(static_cast<std::uint8_t>(token));
        } else {
            if (token >= next) invalid("invalid LZW dictionary reference");
            const auto count = lengths[token] + 1;
            if (count > expected - output.size()) invalid("LZW output exceeds declared size");
            for (std::size_t i = 0; i < count; ++i) {
                const auto source = offsets[token] + i;
                if (source >= output.size()) invalid("invalid LZW backreference");
                const auto byte = output[source];
                output.push_back(byte);
            }
        }
        // SCI0 grows widths after reading the boundary token, unlike SCI1.
        if (next == (1u << width) && width < 12) ++width;
        if (next < offsets.size()) {
            offsets[next] = start;
            lengths[next] = output.size() - start;
            ++next;
        }
    }
    if (output.size() != expected) invalid("LZW output shorter than declared size");
    return output;
}

Bytes huffman(const Bytes& input, std::size_t expected) {
    if (input.size() < 2) invalid("truncated Huffman header");
    const std::size_t nodes = input[0];
    const std::size_t data_start = 2 + nodes * 2;
    if (nodes == 0 || data_start > input.size()) invalid("invalid Huffman node table");
    Bits bits(input, true, data_start);
    Bytes output;
    output.reserve(expected);
    for (;;) {
        std::size_t node = 0;
        unsigned value = 0;
        for (;;) {
            if (node >= nodes) invalid("Huffman node outside table");
            const auto branches = input[2 + node * 2 + 1];
            if (branches == 0) { value = input[2 + node * 2]; break; }
            const auto right = bits.read(1);
            const auto step = right ? (branches & 15u) : (branches >> 4);
            if (right && step == 0) { value = 256u | bits.read(8); break; }
            if (step == 0) invalid("Huffman self-referencing node");
            node += step;
        }
        if (value == (256u | input[1])) break;
        if (output.size() == expected) invalid("Huffman output exceeds declared size");
        output.push_back(static_cast<std::uint8_t>(value));
    }
    if (output.size() != expected) invalid("Huffman output shorter than declared size");
    return output;
}
} // namespace

Bytes decode_sci0(std::uint16_t method, const Bytes& input, std::size_t expected_size) {
    if (input.size() > max_stored_payload || expected_size > 65535)
        invalid("resource exceeds SCI0 header limits");
    switch (method) {
    case 0:
        if (input.size() != expected_size) invalid("stored resource length mismatch");
        return input;
    case 1: return lzw(input, expected_size);
    case 2: return huffman(input, expected_size);
    default: invalid("unsupported SCI0 compression method");
    }
}
} // namespace scimate
