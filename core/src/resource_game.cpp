// SPDX-License-Identifier: GPL-2.0-or-later
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
// SCI0 header/layout adaptation: see docs/provenance.md.
#include "scimate/resource_game.hpp"

#include <set>
#include "binary.hpp"
#include <string>
#include <utility>

namespace scimate {
namespace {
std::uint16_t word(const Bytes& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(binary_detail::read(bytes, offset, 2, "truncated resource header"));
}
void append(Bytes& bytes, std::uint32_t value, unsigned count) {
    binary_detail::append(bytes, value, count, "resource field exceeds encoded width");
}
bool same_entry(const ResourceMapEntry& a, const ResourceMapEntry& b) {
    return a.id.type == b.id.type && a.id.number == b.id.number &&
           a.volume == b.volume && a.offset == b.offset;
}
std::uint16_t packed_id(const ResourceId& id) {
    return static_cast<std::uint16_t>((static_cast<unsigned>(id.type) << 11) | id.number);
}
} // namespace

Game read_sci0_game(GameFiles files) {
    if (files.map.size() > max_map_bytes) throw std::runtime_error("map exceeds 16 MiB limit");
    std::size_t total = files.map.size();
    for (const auto& volume : files.volumes) {
        if (volume.first > 63) throw std::runtime_error("invalid SCI0 volume number");
        if (volume.second.size() > max_game_bytes - total)
            throw std::runtime_error("game exceeds 256 MiB input limit");
        total += volume.second.size();
    }
    const auto entries = read_sci0_map(files.map);
    Game game;
    std::set<std::uint8_t> used;
    std::size_t decoded = 0;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        try {
            const auto found = files.volumes.find(entry.volume);
            if (found == files.volumes.end()) throw std::runtime_error("missing volume");
            const auto& volume = found->second;
            const std::size_t offset = entry.offset;
            if (offset > volume.size() || volume.size() - offset < 8)
                throw std::runtime_error("resource header outside volume");
            if (word(volume, offset) != packed_id(entry.id))
                throw std::runtime_error("resource ID differs from map");
            const auto stored = word(volume, offset + 2);
            const auto size = word(volume, offset + 4);
            const auto method = word(volume, offset + 6);
            if (stored < 4) throw std::runtime_error("stored length does not include required four bytes");
            const auto count = static_cast<std::size_t>(stored - 4);
            if (count > volume.size() - offset - 8) throw std::runtime_error("truncated resource payload");
            if (size > max_decoded_bytes - decoded) throw std::runtime_error("decoded game exceeds 256 MiB limit");
            decoded += size;
            const auto begin = volume.begin() + static_cast<std::ptrdiff_t>(offset + 8);
            Bytes compressed(begin, begin + static_cast<std::ptrdiff_t>(count));
            game.resources.push_back({entry, method, decode_sci0(method, compressed, size)});
            used.insert(entry.volume);
        } catch (const std::runtime_error& error) {
            throw std::runtime_error("record " + std::to_string(index) + ", volume " +
                std::to_string(entry.volume) + ", offset " + std::to_string(entry.offset) + ": " + error.what());
        }
    }
    for (auto it = files.volumes.begin(); it != files.volumes.end();) {
        if (used.count(it->first) == 0) it = files.volumes.erase(it);
        else ++it;
    }
    game.original = std::move(files);
    return game;
}

GameFiles rebuild_sci0_game(const Game& game) {
    const auto original = read_sci0_game(game.original);
    if (game.resources.size() != original.resources.size())
        throw std::runtime_error("adding/removing resources is not supported");
    bool changed = false;
    std::size_t decoded = 0;
    for (std::size_t i = 0; i < game.resources.size(); ++i) {
        const auto& resource = game.resources[i];
        const auto& before = original.resources[i];
        if (!same_entry(resource.entry, before.entry) || resource.compression != before.compression)
            throw std::runtime_error("resource metadata must match original; only payload edits are supported");
        if (resource.payload.size() > 65535 || resource.payload.size() > max_decoded_bytes - decoded)
            throw std::runtime_error("edited payload exceeds resource/game size limit");
        decoded += resource.payload.size();
        if (resource.payload != before.payload) {
            if (resource.payload.size() > max_stored_payload)
                throw std::runtime_error("edited uncompressed payload exceeds 65531 bytes");
            changed = true;
        }
    }
    if (!changed) return original.original;

    GameFiles output;
    std::size_t total = 6;
    for (std::size_t i = 0; i < game.resources.size(); ++i) {
        const auto& resource = game.resources[i];
        auto& volume = output.volumes[resource.entry.volume];
        if (volume.size() > 0x03ffffff) throw std::runtime_error("rebuilt volume exceeds 26-bit offset limit");
        append(output.map, packed_id(resource.entry.id), 2);
        append(output.map, static_cast<std::uint32_t>(volume.size()) |
               (static_cast<std::uint32_t>(resource.entry.volume) << 26), 4);
        const auto old_size = volume.size();
        if (resource.payload == original.resources[i].payload) {
            const auto& source = original.original.volumes.at(resource.entry.volume);
            const std::size_t offset = resource.entry.offset;
            const auto size = static_cast<std::size_t>(word(source, offset + 2)) + 4;
            const auto begin = source.begin() + static_cast<std::ptrdiff_t>(offset);
            volume.insert(volume.end(), begin, begin + static_cast<std::ptrdiff_t>(size));
        } else {
            append(volume, packed_id(resource.entry.id), 2);
            append(volume, static_cast<std::uint32_t>(resource.payload.size() + 4), 2);
            append(volume, static_cast<std::uint32_t>(resource.payload.size()), 2);
            append(volume, 0, 2);
            volume.insert(volume.end(), resource.payload.begin(), resource.payload.end());
        }
        total += volume.size() - old_size + 6;
        if (total > max_game_bytes) throw std::runtime_error("rebuilt game exceeds 256 MiB limit");
    }
    output.map.insert(output.map.end(), 6, 0xff);
    const auto reopened = read_sci0_game(output);
    for (std::size_t i = 0; i < game.resources.size(); ++i) {
        if (reopened.resources[i].payload != game.resources[i].payload)
            throw std::runtime_error("rebuilt payload verification failed");
    }
    return output;
}
} // namespace scimate
