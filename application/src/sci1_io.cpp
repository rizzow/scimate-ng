// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/sci1_io.hpp"
#include "scimate/game_io.hpp"
#include "files.hpp"

namespace scimate {
Game open_sci1_game(const std::filesystem::path& directory, Sci1Format format) {
    GameFiles files;
    // Packed EGA maps have no 16-bit directory offsets; use the same bound as
    // the core reader instead of rejecting valid maps larger than 64 KiB.
    files.map = detail::read_file(directory / "resource.map",
        format == Sci1Format::sci1_ega ? max_map_bytes : 65535);
    std::size_t total = files.map.size();
    for (const auto& entry : read_sci1_map(files.map,format)) {
        if (files.volumes.count(entry.volume)) continue;
        auto bytes = detail::read_file(directory/volume_filename(entry.volume),max_game_bytes-total);
        total += bytes.size();
        files.volumes.emplace(entry.volume,std::move(bytes));
    }
    return read_sci1_game(std::move(files),format);
}
void migrate_sci0_to_sci1_directory(const std::filesystem::path& source,
                                   const std::filesystem::path& destination) {
    const auto result = migrate_sci0_to_sci1(open_sci0_game(source));
    detail::create_destination(destination);
    for (const auto& volume : result.files.volumes)
        detail::write_file(destination/volume_filename(volume.first),volume.second);
    detail::write_file(destination/"resource.map",result.files.map);
    detail::write_file(destination/"migration-report.txt",Bytes(result.report.begin(),result.report.end()));
}
} // namespace scimate
