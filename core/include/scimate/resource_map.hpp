// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace scimate {

enum class ResourceType : std::uint8_t {
    view, pic, script, text, sound, memory, vocab, font, cursor, patch,
    bitmap, palette, cd_audio, audio, sync, message, map, heap
};

std::string_view resource_type_name(ResourceType type) noexcept;

struct ResourceId {
    ResourceType type;
    std::uint16_t number;
};

struct ResourceMapEntry {
    ResourceId id;
    std::uint8_t volume;
    std::uint32_t offset;
};

enum class MapError { truncated_record, missing_terminator, invalid_type, trailing_data };

class MapParseError : public std::runtime_error {
public:
    MapParseError(MapError code, std::size_t byte_offset, const char* message);
    MapError code() const noexcept { return code_; }
    std::size_t byte_offset() const noexcept { return byte_offset_; }
private:
    MapError code_;
    std::size_t byte_offset_;
};

// Strict SCI0 layout only. Preserves order and duplicates. Requires a final
// six-byte FF terminator, accepts types 0..9, and never accesses volume files.
// Throws MapParseError; no partial result is returned on malformed input.
std::vector<ResourceMapEntry> read_sci0_map(const std::vector<std::uint8_t>& bytes);

} // namespace scimate
