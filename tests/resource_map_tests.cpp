// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/resource_map.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
const Bytes terminator(6, 0xff);
// Hand-encoded: pic 42, volume 1, offset 0x123456; patch 2047,
// volume 63, offset 0x3ffffff. The latter location equals all ones,
// which must not be mistaken for a terminator without the ID word.
const Bytes valid{0x2a, 0x08, 0x56, 0x34, 0x12, 0x04,
                  0xff, 0x4f, 0xff, 0xff, 0xff, 0xff,
                  0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void expect_error(const Bytes& bytes, scimate::MapError code, std::size_t offset) {
    try {
        scimate::read_sci0_map(bytes);
    } catch (const scimate::MapParseError& error) {
        require(error.code() == code, "wrong parse error code");
        require(error.byte_offset() == offset, "wrong error offset");
        return;
    }
    throw std::runtime_error("malformed map accepted");
}
void write_fixture(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream file(path, std::ios::binary);
    for (auto byte : bytes) file.put(static_cast<char>(byte));
    file.close();
    require(static_cast<bool>(file), "failed to write fixture");
}
void tests() {
    using namespace scimate;
    const auto entries = read_sci0_map(valid);
    require(entries.size() == 2, "wrong entry count");
    require(entries[0].id.type == ResourceType::pic && entries[0].id.number == 42 &&
            entries[0].volume == 1 && entries[0].offset == 0x123456, "incorrect LE decoding");
    require(entries[1].id.type == ResourceType::patch && entries[1].id.number == 2047 &&
            entries[1].volume == 63 && entries[1].offset == 0x3ffffff, "incorrect upper boundaries");
    require(read_sci0_map(terminator).empty(), "terminated empty map rejected");
    Bytes zero(6, 0);
    zero.insert(zero.end(), terminator.begin(), terminator.end());
    auto first = read_sci0_map(zero).front();
    require(first.id.type == ResourceType::view && first.id.number == 0 &&
            first.volume == 0 && first.offset == 0, "incorrect lower boundaries");

    Bytes duplicates(valid.begin(), valid.begin() + 6);
    duplicates.insert(duplicates.end(), valid.begin(), valid.end());
    const auto repeated = read_sci0_map(duplicates);
    require(repeated.size() == 3 && repeated[0].id.number == 42 &&
            repeated[1].id.number == 42 && repeated[2].id.number == 2047,
            "order or duplicates lost");

    // Every possible cut tests both record truncation and missing terminators.
    for (std::size_t cut = 0; cut < valid.size(); ++cut) {
        expect_error(Bytes(valid.begin(), valid.begin() + cut),
                     cut % 6 == 0 ? MapError::missing_terminator : MapError::truncated_record,
                     cut - cut % 6);
    }
    for (unsigned type = 0; type < 32; ++type) {
        Bytes bytes{0, static_cast<std::uint8_t>(type << 3), 0, 0, 0, 0};
        bytes.insert(bytes.end(), terminator.begin(), terminator.end());
        if (type <= 9) {
            require(static_cast<unsigned>(read_sci0_map(bytes)[0].id.type) == type,
                    "incorrect type bits");
            require(resource_type_name(static_cast<ResourceType>(type)) != "unknown",
                    "missing type name");
        } else {
            expect_error(bytes, MapError::invalid_type, 0);
        }
    }
    Bytes bad_id{0xff, 0xff, 0, 0, 0, 0};
    expect_error(bad_id, MapError::invalid_type, 0);
    Bytes later_bad(valid.begin(), valid.begin() + 6);
    later_bad.insert(later_bad.end(), bad_id.begin(), bad_id.end());
    expect_error(later_bad, MapError::invalid_type, 6);
    Bytes trailing = valid;
    trailing.push_back(0);
    expect_error(trailing, MapError::trailing_data, 18);
    trailing = terminator;
    trailing.insert(trailing.end(), terminator.begin(), terminator.end());
    expect_error(trailing, MapError::trailing_data, 6);
    require(resource_type_name(static_cast<ResourceType>(255)) == "unknown", "unsafe type lookup");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const std::filesystem::path directory(argv[2]);
            std::filesystem::create_directories(directory);
            write_fixture(directory / "valid.map", valid);
            write_fixture(directory / "empty.map", terminator);
            write_fixture(directory / "bad.map", Bytes{0, 0, 0});
            return 0;
        }
        tests();
        std::cout << "SCI0 map tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
