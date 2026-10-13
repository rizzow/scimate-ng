// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_fixtures.hpp"
#include "scimate/game_io.hpp"
#include "scimate/sci1_io.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
using namespace fixture;
namespace fs = std::filesystem;
void write(const fs::path& path, const Bytes& bytes) {
    std::ofstream file(path, std::ios::binary);
    for (auto byte : bytes) file.put(static_cast<char>(byte));
    file.close();
    require(static_cast<bool>(file), "fixture write failed");
}
Bytes read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    require(static_cast<bool>(file), "fixture read failed");
    return Bytes(std::istreambuf_iterator<char>(file), {});
}
void create_game(const fs::path& path) {
    require(fs::create_directory(path), "fixture directory exists");
    auto files = game();
    files.map.resize(files.map.size() - 6);
    record(files, 0x1003, 1, 0, 7, {2,0,5,0,0x48,0,0});
    files.map.insert(files.map.end(), 6, 0xff);
    write(path / "resource.map", files.map);
    for (const auto& volume : files.volumes) write(path / volume_filename(volume.first), volume.second);
}
void tests(const fs::path& root) {
    const auto source = root / "source game";
    const auto exported = root / "exported game";
    create_game(source);
    const auto summary = export_sci0_game(source, exported);
    require(summary.script_listings == 1 && summary.script_errors == 1, "incorrect script export summary");
    require(fs::exists(exported / "scripts/0000001.error.txt"), "malformed script diagnostic missing");
    const auto assembly = read(exported / "scripts/0000003.asm");
    require(std::string(assembly.begin(), assembly.end()).find("ret") != std::string::npos, "script listing missing ret");
    // Listings are inspection sidecars; changing one must not change imported bytes.
    write(exported / "scripts/0000003.asm", {'e','d','i','t'});
    const auto unchanged = root / "unchanged";
    import_sci0_game(exported, unchanged);
    for (const auto* name : {"resource.map", "resource.001", "resource.063"})
        require(read(source / name) == read(unchanged / name), "filesystem byte roundtrip failed");
    fails([&] { export_sci0_game(source, exported); }, "already exists");
    fails([&] { import_sci0_game(exported, source); }, "already exists");
    const auto original = read(source / "resource.001");
    write(exported / "payloads/0000000.bin", {'e','d','i','t','e','d'});
    const auto edited = root / "edited";
    import_sci0_game(exported, edited);
    const auto loaded = open_sci0_game(edited);
    require(loaded.resources[0].payload == Bytes({'e','d','i','t','e','d'}), "edited file import");
    require(loaded.resources[2].payload == Bytes({'A','B'}), "duplicate ID payload lost");
    require(read(source / "resource.001") == original, "source was modified");

    const auto saved_manifest = read(exported / "manifest.tsv");
    auto invalid_manifest = saved_manifest; invalid_manifest[0] = 'X';
    write(exported / "manifest.tsv", invalid_manifest);
    const auto failed = root / "must-not-exist";
    fails([&] { import_sci0_game(exported, failed); }, "manifest");
    require(!fs::exists(failed), "invalid manifest created output");
    write(exported / "manifest.tsv", saved_manifest);
    write(exported / "payloads/0000000.bin", Bytes(65532));
    fails([&] { import_sci0_game(exported, failed); }, "65531");
    require(!fs::exists(failed), "oversize edit created output");
    fs::remove(exported / "payloads/0000000.bin");
    fails([&] { import_sci0_game(exported, failed); }, "");
    require(!fs::exists(failed), "missing payload created output");
    fs::remove(source / "resource.063");
    fails([&] { export_sci0_game(source, failed); }, "");
    require(!fs::exists(failed), "missing volume created output");

    const auto empty = root / "empty";
    fs::create_directory(empty);
    write(empty / "resource.map", Bytes(6, 0xff));
    export_sci0_game(empty, root / "empty-export");
    import_sci0_game(root / "empty-export", root / "empty-rebuilt");
    require(open_sci0_game(root / "empty-rebuilt").resources.empty(), "empty game roundtrip");

    const auto ega = root / "large-ega-map";
    fs::create_directory(ega);
    // Duplicate map records are valid and retain map order. Reuse one empty
    // stored resource to exercise the map limit without a large volume fixture.
    Bytes large_map(11000 * 6, 0);
    large_map.insert(large_map.end(), 6, 0xff);
    write(ega / "resource.map", large_map);
    write(ega / "resource.000", {0, 0, 4, 0, 0, 0, 0, 0});
    require(open_sci1_game(ega, scimate::Sci1Format::sci1_ega).resources.size() == 11000,
            "SCI1 EGA filesystem loader rejected a valid map larger than 64 KiB");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixture") {
            create_game(argv[2]);
            return 0;
        }
        require(argc == 2, "expected fixture parent directory");
        const fs::path parent(argv[1]);
        fs::create_directories(parent);
        const auto root = parent / ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(fs::create_directory(root), "test directory already exists");
        tests(root);
        fs::remove_all(root);
        std::cout << "SCI0 filesystem tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
