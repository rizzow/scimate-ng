// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_fixtures.hpp"
#include <iostream>

namespace {
using namespace fixture;
// Test-only bit packer. Width boundaries below are explicit SCI0 test vectors,
// not derived from the production decoder's dictionary counters.
struct BitWriter {
    Bytes bytes;
    std::size_t position = 0;
    void put(unsigned value, unsigned width) {
        for (unsigned i = 0; i < width; ++i) {
            if (position % 8 == 0) bytes.push_back(0);
            bytes.back() |= static_cast<std::uint8_t>(((value >> i) & 1u) << (position % 8));
            ++position;
        }
    }
};
void codecs() {
    require(decode_sci0(0, {'x'}, 1) == Bytes{'x'}, "stored decoding");
    require(decode_sci0(0, {}, 0).empty(), "empty stored decoding");
    require(decode_sci0(1, lzw, 7) == Bytes({'A','B','A','B','A','B','A'}), "known LZW overlap vector");
    require(decode_sci0(2, huffman, 2) == Bytes({'A','B'}), "known Huffman vector");
    require(decode_sci0(2, {4, 0xff, 0, 0x12, 'A', 0, 0, 0x10, 'B', 0, 0x5f, 0xf8}, 2)
            == Bytes({'A','B'}), "Huffman right subtree");
    require(decode_sci0(2, {2, 0xff, 0, 0x10, 0xff, 0, 0x7f, 0xc0}, 1)
            == Bytes{0xff}, "Huffman leaf with terminator byte value");
    fails([] { decode_sci0(3, {}, 0); }, "unsupported");
    fails([] { decode_sci0(0, {1}, 2); }, "length mismatch");
    fails([] { decode_sci0(0, Bytes(65532), 65532); }, "limits");
    fails([] { decode_sci0(1, {}, 65536); }, "limits");
    for (auto method : {1, 2}) {
        const auto& data = method == 1 ? lzw : huffman;
        const std::size_t size = method == 1 ? 7 : 2;
        for (std::size_t cut = 0; cut < data.size(); ++cut) {
            fails([&] { decode_sci0(static_cast<std::uint16_t>(method), Bytes(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(cut)), size); }, "");
        }
        fails([&] { decode_sci0(static_cast<std::uint16_t>(method), data, size - 1); }, "exceeds");
        fails([&] { decode_sci0(static_cast<std::uint16_t>(method), data, size + 1); }, "shorter");
    }
    BitWriter invalid;
    invalid.put(258, 9); invalid.put(257, 9);
    fails([&] { decode_sci0(1, invalid.bytes, 1); }, "dictionary reference");
    BitWriter reset;
    reset.put(65, 9); reset.put(256, 9); reset.put(258, 9);
    fails([&] { decode_sci0(1, reset.bytes, 3); }, "dictionary reference");

    BitWriter wide;
    Bytes expected;
    for (unsigned i = 0; i < 5000; ++i) {
        wide.put(i % 256, i < 255 ? 9 : i < 767 ? 10 : i < 1791 ? 11 : 12);
        expected.push_back(static_cast<std::uint8_t>(i % 256));
    }
    wide.put(256, 12); // Reset a full dictionary and return to nine-bit codes.
    wide.put('Z', 9); wide.put(258, 9); wide.put(257, 9);
    expected.insert(expected.end(), 3, 'Z');
    require(decode_sci0(1, wide.bytes, expected.size()) == expected, "LZW widths/full dictionary/reset");
    for (unsigned count : {254u, 255u, 256u, 766u, 767u, 768u, 1790u, 1791u, 1792u, 3839u}) {
        BitWriter boundary;
        for (unsigned i = 0; i < count; ++i)
            boundary.put('A', i < 255 ? 9 : i < 767 ? 10 : i < 1791 ? 11 : 12);
        boundary.put(257, count < 255 ? 9 : count < 767 ? 10 : count < 1791 ? 11 : 12);
        require(decode_sci0(1, boundary.bytes, count) == Bytes(count, 'A'), "LZW end-code width boundary");
    }
    fails([] { decode_sci0(2, {0, 0}, 0); }, "node table");
    fails([] { decode_sci0(2, {1, 0, 0, 0x01, 0}, 1); }, "self-referencing");
    fails([] { decode_sci0(2, {1, 0, 0, 0xf0, 0}, 1); }, "outside table");
    fails([] { decode_sci0(2, {1, 0, 'A', 0}, 1); }, "exceeds");
    // A leaf-only tree cannot terminate; it must stop at the declared output limit.

    // Deterministic malformed-stream smoke coverage, also run under sanitizers.
    std::uint32_t state = 0x12345678;
    for (unsigned trial = 0; trial < 4000; ++trial) {
        Bytes noise(trial % 128);
        for (auto& byte : noise) {
            state = state * 1664525u + 1013904223u;
            byte = static_cast<std::uint8_t>(state >> 24);
        }
        for (std::uint16_t method : {std::uint16_t{1}, std::uint16_t{2}}) {
            Bytes result;
            try { result = decode_sci0(method, noise, trial % 65); }
            catch (const std::runtime_error&) { continue; }
            require(result.size() == trial % 65, "random decode violated declared length");
        }
    }
}

void roundtrip() {
    const auto files = game();
    auto loaded = read_sci0_game(files);
    require(loaded.resources.size() == 3, "resource count");
    require(loaded.resources[0].payload == Bytes({'o','l','d'}), "raw payload");
    require(loaded.resources[1].payload == Bytes({'A','B','A','B','A','B','A'}), "LZW game payload");
    require(loaded.resources[2].payload == Bytes({'A','B'}), "Huffman game payload");
    const auto exact = rebuild_sci0_game(loaded);
    require(exact.map == files.map && exact.volumes == files.volumes, "byte-identical round trip");
    loaded.resources[0].payload = {'n','e','w','!'};
    const auto edited = rebuild_sci0_game(loaded);
    const auto reopened = read_sci0_game(edited);
    for (std::size_t i = 0; i < loaded.resources.size(); ++i) {
        require(reopened.resources[i].payload == loaded.resources[i].payload, "edited payload mismatch");
        require(reopened.resources[i].entry.id.number == loaded.resources[i].entry.id.number &&
                reopened.resources[i].entry.id.type == loaded.resources[i].entry.id.type &&
                reopened.resources[i].entry.volume == loaded.resources[i].entry.volume, "metadata changed");
    }
    require(reopened.resources[2].entry.offset == 12, "offset not recalculated after length change");
    require(reopened.resources[1].compression == 1 && reopened.resources[2].compression == 2, "unchanged compression lost");
    require(edited.volumes.at(63) == files.volumes.at(63), "unchanged compressed record changed");
    loaded.resources[1].payload = {'x'};
    require(read_sci0_game(rebuild_sci0_game(loaded)).resources[1].compression == 0, "edited compression not stored");
    loaded.resources[0].payload = Bytes(65531, 'x');
    require(read_sci0_game(rebuild_sci0_game(loaded)).resources[0].payload.size() == 65531, "max stored size");
    loaded.resources[0].payload.push_back('x');
    fails([&] { rebuild_sci0_game(loaded); }, "65531");
    loaded = read_sci0_game(files);
    loaded.resources[0].payload.clear();
    require(read_sci0_game(rebuild_sci0_game(loaded)).resources[0].payload.empty(), "empty payload edit");
    loaded.resources[0].entry.id.number++;
    fails([&] { rebuild_sci0_game(loaded); }, "metadata");
    loaded = read_sci0_game(files); loaded.resources.pop_back();
    fails([&] { rebuild_sci0_game(loaded); }, "adding/removing");
    GameFiles empty{Bytes(6, 0xff), {}};
    require(rebuild_sci0_game(read_sci0_game(empty)).map == empty.map, "empty game");
    // Two map records may reference the exact same stored record.
    auto alias = files;
    alias.map.insert(alias.map.begin(), files.map.begin(), files.map.begin() + 6);
    auto aliased = read_sci0_game(alias);
    require(rebuild_sci0_game(aliased).map == alias.map, "alias preservation");
    aliased.resources[0].payload = {'a'};
    const auto split = read_sci0_game(rebuild_sci0_game(aliased));
    require(split.resources[1].payload == Bytes({'o','l','d'}), "alias edit changed duplicate");
}

void malformed() {
    const auto files = game();
    auto bad = files; bad.volumes.erase(63);
    fails([&] { read_sci0_game(bad); }, "record 1, volume 63, offset 0: missing volume");
    for (std::size_t cut = 0; cut < 14; ++cut) {
        bad = files; bad.volumes[1].resize(cut);
        fails([&] { read_sci0_game(bad); }, "record 0");
    }
    bad = files; bad.volumes[1][3] ^= 1;
    fails([&] { read_sci0_game(bad); }, "ID differs");
    bad = files; bad.volumes[1][5] = 3; bad.volumes[1][6] = 0;
    fails([&] { read_sci0_game(bad); }, "required four bytes");
    bad = files; bad.volumes[1][7] = 4;
    fails([&] { read_sci0_game(bad); }, "length mismatch");
    bad = files; bad.volumes[1][9] = 9;
    fails([&] { read_sci0_game(bad); }, "unsupported");
    bad = files; bad.map[2] = 0xff; bad.map[3] = 0xff; bad.map[4] = 0xff;
    fails([&] { read_sci0_game(bad); }, "outside volume");
    bad = files; bad.volumes[64] = {};
    fails([&] { read_sci0_game(bad); }, "volume number");
}
} // namespace

int main() {
    try { codecs(); roundtrip(); malformed(); std::cout << "SCI0 game tests passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
