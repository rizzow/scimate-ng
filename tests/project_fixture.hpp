// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "game_fixtures.hpp"
#include "view_fixture.hpp"
#include "scimate/game_io.hpp"
#include <filesystem>
#include <fstream>
namespace project_fixture {
inline void write(const std::filesystem::path& path, const scimate::Bytes& bytes) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    fixture::require(bool(stream), "fixture write failed");
}
inline void create(const std::filesystem::path& directory) {
    std::filesystem::create_directory(directory);
    auto files = fixture::game();
    files.map.resize(files.map.size() - 6);
    auto multi = scimate::read_sci0_view(fixture::view_bytes);
    multi.loops[0].cels.push_back(multi.loops[0].cels[0]);
    const auto view = scimate::write_sci0_view(multi);
    fixture::record(files, 5, 1, 0, static_cast<unsigned>(view.size()), view);
    fixture::record(files, 0x1003, 1, 0, 7, {2,0,5,0,0x48,0,0});
    fixture::record(files, 0x0806, 1, 0, 1, {0xff});
    fixture::record(files, 0x1808, 1, 0, 13, {'H','e','l','l','o',0,'W','o','r','l','d',0,0});
    files.map.insert(files.map.end(), 6, 0xff);
    write(directory / "resource.map", files.map);
    for (const auto& volume : files.volumes) write(directory / scimate::volume_filename(volume.first), volume.second);
}
}
