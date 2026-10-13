// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <stdexcept>
#include <string>

namespace fixture {
using namespace scimate;
inline void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void fails(F operation, const std::string& diagnostic) {
    try { operation(); }
    catch (const std::runtime_error& error) {
        if (std::string(error.what()).find(diagnostic) == std::string::npos)
            throw std::runtime_error("unexpected error: " + std::string(error.what()) + "; expected " + diagnostic);
        return;
    }
    throw std::runtime_error("expected error: " + diagnostic);
}
inline void put(Bytes& bytes, std::uint32_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
inline void record(GameFiles& files, unsigned id, std::uint8_t volume_number,
                   unsigned method, unsigned decoded_size, const Bytes& payload) {
    auto& volume = files.volumes[volume_number];
    put(files.map, id, 2);
    put(files.map, static_cast<std::uint32_t>(volume.size()) | (static_cast<std::uint32_t>(volume_number) << 26), 4);
    put(volume, id, 2); put(volume, static_cast<unsigned>(payload.size()) + 4, 2);
    put(volume, decoded_size, 2); put(volume, method, 2);
    volume.insert(volume.end(), payload.begin(), payload.end());
}
inline const Bytes lzw{0x00, 0x83, 0x08, 0x11, 0x48, 0x30, 0x20};
// Two-node Huffman table: left leaf A, right escape; bits 0,1+0x42,1+0xFF.
inline const Bytes huffman{2, 0xff, 0, 0x10, 0x41, 0, 0x50, 0xbf, 0xe0};
inline GameFiles game() {
    GameFiles files;
    files.volumes[1] = {0xde, 0xad, 0xbe}; // Preserve preamble and trailing unused bytes.
    record(files, 0x1807, 1, 0, 3, {'o', 'l', 'd'});
    record(files, 0x1002, 63, 1, 7, lzw);
    record(files, 0x1807, 1, 2, 2, huffman); // Duplicate ID, distinct record.
    files.volumes[1].push_back(0xcc);
    files.map.insert(files.map.end(), 6, 0xff);
    return files;
}
} // namespace fixture
