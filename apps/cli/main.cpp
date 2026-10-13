// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/resource_map.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
constexpr std::string_view usage =
    "Usage: scimate list --format sci0 <resource.map>\n"
    "Lists type, number, volume, and byte offset (decimal) in map order.\n"
    "SCI0 maps only; no format detection, volume validation, decompression, or editing.\n";

std::vector<std::uint8_t> read_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open map file");
    constexpr std::size_t limit = 16 * 1024 * 1024;
    std::vector<std::uint8_t> bytes;
    std::array<char, 4096> chunk{};
    while (file.read(chunk.data(), chunk.size()) || file.gcount() > 0) {
        const auto count = static_cast<std::size_t>(file.gcount());
        if (count > limit - bytes.size()) throw std::runtime_error("map exceeds CLI limit of 16 MiB");
        bytes.insert(bytes.end(), chunk.data(), chunk.data() + count);
    }
    if (file.bad() || !file.eof()) throw std::runtime_error("failed to read map file");
    return bytes;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << usage;
        return 0;
    }
    if (argc != 5 || std::string_view(argv[1]) != "list" ||
        std::string_view(argv[2]) != "--format" || std::string_view(argv[3]) != "sci0") {
        std::cerr << usage;
        return 2;
    }
    try {
        const auto entries = scimate::read_sci0_map(read_file(argv[4]));
        std::cout << "type\tnumber\tvolume\toffset\n";
        for (const auto& entry : entries) {
            std::cout << scimate::resource_type_name(entry.id.type) << '\t'
                      << entry.id.number << '\t' << static_cast<unsigned>(entry.volume)
                      << '\t' << entry.offset << '\n';
        }
        if (!std::cout) throw std::runtime_error("failed to write listing");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scimate: " << argv[4] << ": " << error.what() << '\n';
        return 1;
    }
}
