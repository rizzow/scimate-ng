// SPDX-License-Identifier: GPL-2.0-or-later
// DOS SCI1/SCI1.1 layouts checked against ScummVM resource.cpp; see provenance.
#include "scimate/sci1.hpp"
#include <set>

namespace scimate {
namespace {
unsigned get(const Bytes& bytes, std::size_t offset, unsigned width) {
    if (offset > bytes.size() || width > bytes.size()-offset) throw std::runtime_error("truncated SCI1 field at byte " + std::to_string(offset));
    unsigned value = 0;
    for (unsigned i = 0; i < width; ++i) value |= static_cast<unsigned>(bytes[offset+i]) << (i*8);
    return value;
}
void put(Bytes& bytes, unsigned value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i*8)));
}
} // namespace
std::vector<ResourceMapEntry> read_sci1_map(const Bytes& bytes, Sci1Format format) {
    if (bytes.size() > 65535) throw std::runtime_error("SCI1 directory map exceeds 16-bit size limit");
    const unsigned stride = format == Sci1Format::sci11 ? 5 : 6;
    std::vector<std::pair<unsigned, unsigned>> directory;
    std::set<unsigned> types;
    std::size_t cursor = 0;
    for (;;) {
        auto type = get(bytes, cursor, 1), offset = get(bytes, cursor+1, 2);
        cursor += 3;
        if (type == 255) {
            if (offset != bytes.size()) throw std::runtime_error("SCI1 directory terminator must point to EOF");
            directory.emplace_back(type, offset);
            break;
        }
        if (type < 128 || type > 145 || !types.insert(type & 127).second) throw std::runtime_error("invalid or duplicate SCI1 directory type");
        directory.emplace_back(type & 127, offset);
    }
    if (directory.front().second != cursor) throw std::runtime_error("SCI1 first resource table must follow directory");
    std::vector<ResourceMapEntry> entries;
    for (std::size_t i = 0; i+1 < directory.size(); ++i) {
        auto begin = directory[i].second, end = directory[i+1].second;
        if (begin > end || end > bytes.size() || (end-begin)%stride) throw std::runtime_error("invalid SCI1 resource table bounds/stride");
        for (auto pos = begin; pos < end; pos += stride) {
            auto number = get(bytes, pos, 2), packed = get(bytes, pos+2, stride-2);
            auto volume = format == Sci1Format::sci11 ? 0u : packed >> 28;
            auto offset = format == Sci1Format::sci11 ? packed << 1 : packed & 0x0fffffff;
            entries.push_back({{static_cast<ResourceType>(directory[i].first), static_cast<std::uint16_t>(number)}, static_cast<std::uint8_t>(volume), offset});
        }
    }
    return entries;
}
Game read_sci1_game(GameFiles files, Sci1Format format) {
    const auto entries = read_sci1_map(files.map, format);
    std::size_t total = files.map.size(), decoded = 0;
    for (const auto& volume : files.volumes) {
        if (volume.first > (format == Sci1Format::sci11 ? 0 : 15)) throw std::runtime_error("invalid SCI1 volume number");
        if (volume.second.size() > max_game_bytes-total) throw std::runtime_error("SCI1 game exceeds 256 MiB limit");
        total += volume.second.size();
    }
    Game game;
    std::set<std::uint8_t> used;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        try {
            auto volume = files.volumes.find(entry.volume);
            if (volume == files.volumes.end()) throw std::runtime_error("missing volume");
            const auto& bytes = volume->second;
            const auto offset = static_cast<std::size_t>(entry.offset);
            if (offset > bytes.size() || bytes.size()-offset < 9) throw std::runtime_error("resource header outside volume");
            if ((get(bytes, offset, 1) & 127u) != static_cast<unsigned>(entry.id.type) || get(bytes, offset+1, 2) != entry.id.number)
                throw std::runtime_error("resource ID differs from map");
            auto packed = get(bytes, offset+3, 2), size = get(bytes, offset+5, 2), method = get(bytes, offset+7, 2);
            if (format == Sci1Format::sci1_late) {
                if (packed < 4) throw std::runtime_error("SCI1 stored size must include four bytes");
                packed -= 4;
            }
            if (packed > bytes.size()-offset-9) throw std::runtime_error("truncated resource payload");
            if (size > max_decoded_bytes-decoded) throw std::runtime_error("SCI1 decoded game exceeds 256 MiB limit");
            decoded += size;
            const auto begin = bytes.begin()+static_cast<std::ptrdiff_t>(offset+9);
            game.resources.push_back({entry, static_cast<std::uint16_t>(method), decode_sci1(static_cast<std::uint16_t>(method), Bytes(begin, begin+packed), size)});
            used.insert(entry.volume);
        } catch (const std::runtime_error& error) {
            throw std::runtime_error("SCI1 record " + std::to_string(i) + ", volume " + std::to_string(entry.volume) + ", offset " + std::to_string(entry.offset) + ": " + error.what());
        }
    }
    for (auto it = files.volumes.begin(); it != files.volumes.end();) {
        if (!used.count(it->first)) it = files.volumes.erase(it); else ++it;
    }
    game.original = std::move(files);
    return game;
}
GameFiles write_sci1_game(const std::vector<Resource>& resources, Sci1Format format) {
    std::map<unsigned, std::vector<const Resource*>> groups;
    for (const auto& resource : resources) {
        auto type = static_cast<unsigned>(resource.entry.id.type);
        if (type > 17) throw std::runtime_error("unsupported SCI1 resource type");
        if (resource.payload.size() > (format == Sci1Format::sci11 ? 65535u : 65531u)) throw std::runtime_error("SCI1 payload exceeds uncompressed header limit");
        groups[type].push_back(&resource);
    }
    const auto stride = format == Sci1Format::sci11 ? 5u : 6u;
    if (resources.size() > 65535/stride) throw std::runtime_error("SCI1 resource count exceeds map limit");
    std::size_t map_size = 3*(groups.size()+1)+stride*resources.size();
    if (map_size > 65535) throw std::runtime_error("SCI1 map exceeds 16-bit directory limit");
    GameFiles files;
    std::size_t table_offset = 3*(groups.size()+1);
    for (const auto& group : groups) {
        put(files.map, 128 | group.first, 1); put(files.map, static_cast<unsigned>(table_offset), 2);
        table_offset += stride*group.second.size();
    }
    put(files.map, 255, 1); put(files.map, static_cast<unsigned>(map_size), 2);
    auto& volume = files.volumes[0];
    std::size_t decoded = 0;
    for (const auto& group : groups) for (const auto* resource : group.second) {
        if (format == Sci1Format::sci11 && volume.size()%2) volume.push_back(0);
        if (volume.size() > (format == Sci1Format::sci11 ? 0x01fffffeu : 0x0fffffffu)) throw std::runtime_error("SCI1 volume offset limit exceeded");
        if (resource->payload.size() > max_decoded_bytes-decoded) throw std::runtime_error("SCI1 decoded game exceeds 256 MiB limit");
        decoded += resource->payload.size();
        put(files.map, resource->entry.id.number, 2);
        put(files.map, static_cast<unsigned>(format == Sci1Format::sci11 ? volume.size()/2 : volume.size()), stride-2);
        put(volume, 128 | group.first, 1); put(volume, resource->entry.id.number, 2);
        put(volume, static_cast<unsigned>(resource->payload.size()) + (format == Sci1Format::sci11 ? 0 : 4), 2);
        put(volume, static_cast<unsigned>(resource->payload.size()), 2); put(volume, 0, 2);
        volume.insert(volume.end(), resource->payload.begin(), resource->payload.end());
        if (volume.size()+files.map.size() > max_game_bytes) throw std::runtime_error("SCI1 output exceeds 256 MiB limit");
    }
    if (resources.empty()) files.volumes.clear();
    return files;
}
} // namespace scimate
