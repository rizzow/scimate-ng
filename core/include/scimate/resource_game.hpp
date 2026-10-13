// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "scimate/resource_map.hpp"
#include <map>
#include <vector>

namespace scimate {
using Bytes = std::vector<std::uint8_t>;

// Limits bound allocations for untrusted games; not SCI format limits.
inline constexpr std::size_t max_map_bytes = 16 * 1024 * 1024;
inline constexpr std::size_t max_game_bytes = 256 * 1024 * 1024;
inline constexpr std::size_t max_decoded_bytes = 256 * 1024 * 1024;
inline constexpr std::size_t max_stored_payload = 65531;

struct GameFiles {
    Bytes map;
    std::map<std::uint8_t, Bytes> volumes;
};

struct Resource {
    ResourceMapEntry entry;
    std::uint16_t compression;
    Bytes payload;
};

struct Game {
    GameFiles original;
    std::vector<Resource> resources;
};

// SCI0 methods 0 (stored), 1 (LSB-first LZW), and 2 (Huffman).
// Compressed streams must terminate and produce exactly expected_size bytes.
// Throws std::runtime_error for unsupported methods or malformed data.
Bytes decode_sci0(std::uint16_t method, const Bytes& input, std::size_t expected_size);

// Validates all map references, header IDs, lengths, and compressed payloads.
// Keeps only referenced volumes. Errors identify the record/volume/offset.
Game read_sci0_game(GameFiles files);

// Only payload edits are supported; resource identities, order, and volumes
// must match original. Unchanged games reproduce original files exactly.
// Edited games retain unchanged records and store modified payloads raw.
// Does not mutate game. Throws on format/size limits or verification failure.
GameFiles rebuild_sci0_game(const Game& game);
} // namespace scimate
