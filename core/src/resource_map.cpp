/***************************************************************************
    Copyright (c) 2015 Philip Fortier

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
***************************************************************************/
// Adapted SCI0 layout/navigation; see docs/provenance.md.
#include "scimate/resource_map.hpp"

#include <array>
#include <string>

namespace scimate {
namespace {
class BinaryReader {
public:
    explicit BinaryReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    std::size_t position() const noexcept { return position_; }
    std::size_t remaining() const noexcept { return bytes_.size() - position_; }
    std::uint32_t little_endian(std::size_t count) {
        if (count > remaining()) {
            throw MapParseError(MapError::truncated_record, position_, "truncated record");
        }
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            value |= static_cast<std::uint32_t>(bytes_[position_++]) << (8 * i);
        }
        return value;
    }
private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};
} // namespace

MapParseError::MapParseError(MapError code, std::size_t byte_offset, const char* message)
    : std::runtime_error("SCI0 map at byte " + std::to_string(byte_offset) + ": " + message),
      code_(code), byte_offset_(byte_offset) {}

std::string_view resource_type_name(ResourceType type) noexcept {
    constexpr std::array<std::string_view, 18> names{
        "view", "pic", "script", "text", "sound", "memory", "vocab", "font", "cursor", "patch",
        "bitmap", "palette", "cd_audio", "audio", "sync", "message", "map", "heap"
    };
    const auto index = static_cast<std::size_t>(type);
    return index < names.size() ? names[index] : "unknown";
}

std::vector<ResourceMapEntry> read_sci0_map(const std::vector<std::uint8_t>& bytes) {
    BinaryReader reader(bytes);
    std::vector<ResourceMapEntry> entries;
    while (reader.remaining() != 0) {
        const auto start = reader.position();
        if (reader.remaining() < 6) {
            throw MapParseError(MapError::truncated_record, start, "truncated record");
        }
        const auto id = reader.little_endian(2);
        const auto location = reader.little_endian(4);
        if (id == 0xffff && location == 0xffffffff) {
            if (reader.remaining() != 0) {
                throw MapParseError(MapError::trailing_data, reader.position(), "data after terminator");
            }
            return entries;
        }
        const auto type = id >> 11;
        if (type > 9) {
            throw MapParseError(MapError::invalid_type, start, "unsupported SCI0 resource type (expected 0..9)");
        }
        entries.push_back({{static_cast<ResourceType>(type), static_cast<std::uint16_t>(id & 0x7ff)},
                           static_cast<std::uint8_t>(location >> 26), location & 0x03ffffff});
    }
    throw MapParseError(MapError::missing_terminator, reader.position(), "missing terminator");
}
} // namespace scimate
