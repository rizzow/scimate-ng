// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/sci1_io.hpp"
#include "scimate/game_io.hpp"
#include "files.hpp"
#include "scimate/workspace.hpp"
#include <algorithm>
#include <cctype>
#include <map>

namespace scimate {
namespace {
std::string folded_filename(std::string name) {
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return name;
}
std::map<std::string,Bytes> collect_auxiliary_files(const std::filesystem::path& source,
                                                   const Sci1Migration& migration, std::string& report) {
    std::map<std::string,std::filesystem::path> available;
    for (const auto& entry : std::filesystem::directory_iterator(source))
        if (std::filesystem::is_regular_file(entry.symlink_status())) {
            const auto name = entry.path().filename().string();
            if (!available.emplace(folded_filename(name),entry.path()).second)
                throw std::runtime_error("ambiguous case-insensitive source filename: " + name);
        }
    std::map<std::string,Bytes> copied;
    std::size_t bytes = migration.files.map.size();
    for (const auto& volume : migration.files.volumes) bytes += volume.second.size();
    for (const auto& reference : migration.auxiliary_file_references) {
        const auto name = folded_filename(reference);
        if (name == workspace_detail::map_file || name == "migration-report.txt" || copied.count(name)) continue;
        const auto extension = std::filesystem::path(name).extension().string();
        const bool volume = name.size() == 12 && name.compare(0,9,"resource.") == 0 &&
            std::all_of(name.begin()+9,name.end(),[](unsigned char ch) { return ch >= '0' && ch <= '9'; });
        if (volume) continue;
        if (extension == ".exe" || extension == ".com" || extension == ".drv" || extension == ".bat") {
            report += "Unsupported external program reference: " + reference + ".\n";
            continue;
        }
        const auto file = available.find(name);
        if (file == available.end()) {
            report += "Unresolved external filename candidate: " + reference + ".\n";
            continue;
        }
        auto payload = detail::read_file(file->second,max_game_bytes-bytes);
        bytes += payload.size();
        copied.emplace(name,std::move(payload));
        report += "Copied referenced auxiliary file: " + name + ".\n";
    }
    return copied;
}
}
Game open_sci1_game(const std::filesystem::path& directory, Sci1Format format, const ProgressCallback& progress) {
    if (progress) progress({"Reading resource map", 0, 0, directory.string()});
    GameFiles files;
    // Packed EGA maps have no 16-bit directory offsets; use the same bound as
    // the core reader instead of rejecting valid maps larger than 64 KiB.
    files.map = detail::read_file(directory / workspace_detail::map_file,
        format == Sci1Format::sci1_ega ? max_map_bytes : 65535);
    std::size_t total = files.map.size();
    for (const auto& entry : read_sci1_map(files.map,format)) {
        if (files.volumes.count(entry.volume)) continue;
        if (progress) progress({"Reading resource volumes", 0, 0, volume_filename(entry.volume)});
        auto bytes = detail::read_file(directory/volume_filename(entry.volume),max_game_bytes-total);
        total += bytes.size();
        files.volumes.emplace(entry.volume,std::move(bytes));
    }
    if (progress) progress({"Decoding resources", 0, 0, "SCI1 container"});
    return read_sci1_game(std::move(files),format);
}
void migrate_sci0_to_sci1_directory(const std::filesystem::path& source,
                                   const std::filesystem::path& destination, Sci1Graphics graphics, const ProgressCallback& progress) {
    auto game = open_sci0_game(source, progress);
    if (progress) progress({"Migrating and verifying resources", 0, 0, graphics == Sci1Graphics::vga ? "Converting to VGA graphics" : "Retaining EGA graphics"});
    auto result = migrate_sci0_to_sci1(game,graphics);
    if (progress) progress({"Collecting referenced auxiliary files", 0, 0, source.string()});
    const auto auxiliary = collect_auxiliary_files(source,result,result.report);
    if (progress) progress({"Writing migrated resources", 0, 0, destination.string()});
    detail::create_destination(destination);
    for (const auto& volume : result.files.volumes)
        detail::write_file(destination/volume_filename(volume.first),volume.second);
    detail::write_file(destination/workspace_detail::map_file,result.files.map);
    for (const auto& file : auxiliary) detail::write_file(destination/file.first,file.second);
    detail::write_file(destination/"migration-report.txt",Bytes(result.report.begin(),result.report.end()));
}
void import_sci1_game(const std::filesystem::path& workspace, const std::filesystem::path& destination, Sci1Graphics graphics, const ProgressCallback& progress) {
    VgaPictureEdits pictures;
    auto game = open_sci0_export(workspace,progress,nullptr,graphics == Sci1Graphics::vga ? &pictures : nullptr);
    if (progress) progress({"Migrating and verifying workspace resources", 0, 0, graphics == Sci1Graphics::vga ? "Converting to VGA graphics" : "Retaining EGA graphics"});
    auto result = migrate_sci0_to_sci1(game,graphics,pictures);
    if (progress) progress({"Collecting referenced auxiliary files", 0, 0, workspace.string()});
    const auto auxiliary = collect_auxiliary_files(workspace/"original",result,result.report);
    result.report += "Built from editable workspace: "+workspace.string()+"; VGA picture PNG overrides: "+std::to_string(pictures.size())+".\n";
    if (progress) progress({"Writing imported SCI1 resources", 0, 0, destination.string()});
    detail::create_destination(destination);
    for (const auto& volume : result.files.volumes) detail::write_file(destination/volume_filename(volume.first),volume.second);
    detail::write_file(destination/workspace_detail::map_file,result.files.map);
    for (const auto& file : auxiliary) detail::write_file(destination/file.first,file.second);
    detail::write_file(destination/"migration-report.txt",Bytes(result.report.begin(),result.report.end()));
}
} // namespace scimate
