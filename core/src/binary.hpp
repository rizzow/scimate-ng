// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include "scimate/diagnostics.hpp"
#include <string>
namespace scimate::binary_detail {
inline void width(unsigned count, const std::string& context) {
    scimate::require(count >= 1 && count <= 4, context + ": invalid integer width");
}
inline std::uint32_t read(const Bytes& bytes, std::size_t offset, unsigned count,
                          const std::string& context, bool big = false) {
    width(count, context);
    scimate::require(offset <= bytes.size() && count <= bytes.size() - offset, context);
    std::uint32_t result = 0;
    for (unsigned i = 0; i < count; ++i)
        result |= std::uint32_t(bytes[offset + i]) << (8 * (big ? count - 1 - i : i));
    return result;
}
inline void append(Bytes& bytes, std::uint32_t value, unsigned count,
                   const std::string& context, bool big = false) {
    width(count, context);
    scimate::require(count == 4 || value < (std::uint32_t(1) << (8 * count)), context);
    for (unsigned i = 0; i < count; ++i)
        bytes.push_back(static_cast<std::uint8_t>(value >> (8 * (big ? count - 1 - i : i))));
}
inline void patch_word(Bytes& bytes, std::size_t offset, std::size_t value, const std::string& context) {
    scimate::require(offset <= bytes.size() && bytes.size() - offset >= 2 && value <= 65535, context);
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}
}
