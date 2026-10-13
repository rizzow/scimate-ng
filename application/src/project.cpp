// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/project.hpp"
#include "files.hpp"
#include "scimate/asset_io.hpp"
#include <iomanip>
#include <sstream>

namespace scimate {
namespace {
void validate_name(const std::string& name) {
    if (name.empty() || name.size() > 200 || name.find_first_not_of(' ') == std::string::npos)
        throw std::runtime_error("project name must contain 1 to 200 UTF-8 bytes");
    for (const unsigned char c : name)
        if (c < 32 || c == 127) throw std::runtime_error("project name contains a control character");
}

std::string read_name(const std::filesystem::path& directory) {
    const auto bytes = detail::read_file(directory / "project.scimate", 4096);
    std::istringstream input(std::string(bytes.begin(), bytes.end()));
    std::string header, name;
    std::getline(input, header);
    if (header != "SCIMATE-PROJECT 1" || !(input >> std::quoted(name)))
        throw std::runtime_error("unsupported or malformed SCIMate project descriptor");
    input >> std::ws;
    if (!input.eof()) throw std::runtime_error("unexpected data in project descriptor");
    validate_name(name);
    return name;
}
}

Bytes prepare_project_rename(const Project& project, const std::string& name) {
    validate_name(name);
    if (read_name(project.directory) != project.name)
        throw std::runtime_error("Project name changed on disk. Refresh from Disk before renaming.");
    std::ostringstream descriptor;
    descriptor << "SCIMATE-PROJECT 1\n" << std::quoted(name) << '\n';
    const auto value = descriptor.str();
    return Bytes(value.begin(), value.end());
}

Project create_project(const std::filesystem::path& source_game,
                       const std::filesystem::path& new_directory,
                       const std::string& name, const ProgressCallback& progress) {
    validate_name(name);
    // Export refuses existing destinations; write the descriptor only on success.
    auto game = open_sci0_game(source_game, progress);
    export_sci0_game(game, new_directory, progress);
    std::ostringstream descriptor;
    descriptor << "SCIMATE-PROJECT 1\n" << std::quoted(name) << '\n';
    const auto text = descriptor.str();
    detail::write_file(new_directory / "project.scimate", Bytes(text.begin(), text.end()));
    // The newly written exports represent this game exactly. Do not decode all
    // PNGs and re-render pictures just to reopen what we have just exported.
    return {std::filesystem::absolute(new_directory).lexically_normal(), name, std::move(game)};
}

Project open_project(const std::filesystem::path& directory, const ProgressCallback& progress) {
    const auto name = read_name(directory);
    return {std::filesystem::absolute(directory).lexically_normal(), name, open_sci0_export(directory, progress)};
}

ProjectTextEdit prepare_project_text_edit(const Project& project, std::size_t record,
                                          const std::string& table) {
    const auto& resource = project.game.resources.at(record);
    if (resource.entry.id.type != ResourceType::text)
        throw std::runtime_error("Select a text resource first.");
    auto payload = import_sci0_text_table(table);
    const auto original = read_sci0_game(project.game.original).resources.at(record).payload;
    if (payload != original && payload.size() > max_stored_payload)
        throw std::runtime_error("Edited text exceeds the SCI0 uncompressed payload limit of 65531 bytes.");
    std::ostringstream name;
    name << std::setfill('0') << std::setw(7) << record;
    const auto binary = project.directory / "payloads" / (name.str() + ".bin");
    const auto raw = detail::read_file(binary, 65535);
    auto current = raw;
    auto destination = binary;
    Bytes contents = payload;
    const auto bundle = project.directory / "assets" / name.str();
    if (std::filesystem::exists(bundle)) {
        if (!std::filesystem::is_directory(std::filesystem::symlink_status(bundle)))
            throw std::runtime_error("expected asset directory");
        if (detail::read_file(bundle / "original.bin", 65535) != original)
            throw std::runtime_error("asset snapshot differs from original resource");
        ResourceId id{};
        const auto asset = import_sci0_asset(bundle, &id);
        if (id.type != resource.entry.id.type || id.number != resource.entry.id.number)
            throw std::runtime_error("asset identity differs from original resource");
        if (asset != original) {
            if (raw != original)
                throw std::runtime_error("Both binary and asset files are edited. Restore one to its original before editing here.");
            current = asset;
        }
        if (raw == original) {
            if (std::filesystem::exists(bundle / "strings.tsv")) {
                destination = bundle / "strings.tsv";
                const auto normalized = export_sci0_text_table(payload);
                contents.assign(normalized.begin(), normalized.end());
            } else if (std::filesystem::exists(bundle / "payload.bin")) {
                destination = bundle / "payload.bin";
            } else {
                throw std::runtime_error("This text bundle uses another representation. Edit it in the resource folder and refresh.");
            }
        }
    }
    if (current != resource.payload)
        throw std::runtime_error("Text changed on disk. Refresh from Disk before editing again.");
    const bool modified = payload != original;
    return {destination, std::move(contents), std::move(payload), modified};
}

void build_project(const std::filesystem::path& directory,
                   const std::filesystem::path& new_game_directory, const ProgressCallback& progress) {
    // Validate the descriptor too; import re-reads edits immediately before build.
    read_name(directory);
    import_sci0_game(directory, new_game_directory, progress);
}
} // namespace scimate
