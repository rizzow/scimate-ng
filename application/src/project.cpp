// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/project.hpp"
#include "files.hpp"
#include "scimate/workspace.hpp"
#include "scimate/asset_io.hpp"
#include <iomanip>
#include <sstream>
#include <set>

namespace scimate {
namespace {
void validate_name(const std::string& name) {
    if (name.empty() || name.size() > 200 || name.find_first_not_of(' ') == std::string::npos)
        throw std::runtime_error("project name must contain 1 to 200 UTF-8 bytes");
    for (const unsigned char c : name)
        if (c < 32 || c == 127) throw std::runtime_error("project name contains a control character");
}

std::string read_name(const std::filesystem::path& directory) {
    const auto bytes = detail::read_file(directory / workspace_detail::project_file, 4096);
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
    detail::write_file(new_directory / workspace_detail::project_file, Bytes(text.begin(), text.end()));
    // The newly written exports represent this game exactly. Do not decode all
    // PNGs and re-render pictures just to reopen what we have just exported.
    std::vector<Bytes> originals;
    for (const auto& resource : game.resources) originals.push_back(resource.payload);
    return {std::filesystem::absolute(new_directory).lexically_normal(), name, std::move(game), std::move(originals)};
}

Project open_project(const std::filesystem::path& directory, const ProgressCallback& progress, bool vga_workspace) {
    const auto descriptor = directory / workspace_detail::project_file;
    const bool has_descriptor = std::filesystem::exists(std::filesystem::symlink_status(descriptor));
    auto name = has_descriptor ? read_name(directory) : directory.lexically_normal().filename().u8string();
    if (!has_descriptor && name.empty()) name = std::filesystem::absolute(directory).lexically_normal().filename().u8string();
    if (!has_descriptor && name.empty()) name = "SCI project";
    validate_name(name);
    std::vector<Bytes> originals;
    VgaPictureEdits pictures;
    VgaViewEdits views;
    auto game = open_sci0_export(directory, progress, &originals,
        vga_workspace ? &pictures : nullptr, vga_workspace ? &views : nullptr);
    // Adopt only a fully validated export. Never replace an existing descriptor.
    if (!has_descriptor) {
        if (std::filesystem::exists(std::filesystem::symlink_status(descriptor))) name = read_name(directory);
        else {
            std::ostringstream output;
            output << "SCIMATE-PROJECT 1\n" << std::quoted(name) << '\n';
            const auto contents = output.str();
            detail::write_file(descriptor, Bytes(contents.begin(), contents.end()));
        }
    }
    return {std::filesystem::absolute(directory).lexically_normal(), name, std::move(game), std::move(originals),
        vga_workspace, std::move(pictures), std::move(views)};
}

ProjectTextEdit prepare_project_text_edit(const Project& project, std::size_t record,
                                          const std::string& table) {
    const auto& resource = project.game.resources.at(record);
    if (resource.entry.id.type != ResourceType::text)
        throw std::runtime_error("Select a text resource first.");
    auto payload = import_sci0_text_table(table);
    const auto original = project.original_payloads.size() == project.game.resources.size()
        ? project.original_payloads.at(record) : read_sci0_game(project.game.original).resources.at(record).payload;
    if (payload != original && payload.size() > max_stored_payload)
        throw std::runtime_error("Edited text exceeds the SCI0 uncompressed payload limit of 65531 bytes.");
    const auto name = workspace_detail::record_stem(record);
    const auto binary = project.directory / "payloads" / (name + ".bin");
    const auto raw = detail::read_file(binary, 65535);
    auto current = raw;
    auto destination = binary;
    Bytes contents = payload;
    std::vector<std::pair<std::filesystem::path, Bytes>> related_writes;
    const auto bundle = project.directory / "assets" / name;
    if (std::filesystem::exists(bundle)) {
        if (!std::filesystem::is_directory(std::filesystem::symlink_status(bundle)))
            throw std::runtime_error("expected asset directory");
        if (detail::read_file(bundle / workspace_detail::original_file, 65535) != original)
            throw std::runtime_error("asset snapshot differs from original resource");
        ResourceId id{};
        const auto asset = import_sci0_asset(bundle, &id);
        if (id.type != resource.entry.id.type || id.number != resource.entry.id.number)
            throw std::runtime_error("asset identity differs from original resource");
        current = workspace_detail::reconcile(original, raw, asset, "text record " + std::to_string(record));
        if (raw == original || raw == asset) {
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
        if (raw != original && asset != original && payload != current)
            related_writes.emplace_back(binary, original);
    }
    if (current != resource.payload)
        throw std::runtime_error("Text changed on disk. Refresh from Disk before editing again.");
    const bool modified = payload != original;
    return {destination, std::move(contents), std::move(payload), modified, std::move(related_writes)};
}

std::vector<bool> project_modified_records(const Project& project) {
    std::vector<Bytes> fallback;
    const auto* originals = &project.original_payloads;
    if (originals->size() != project.game.resources.size()) {
        const auto original = read_sci0_game(project.game.original);
        for (const auto& resource : original.resources) fallback.push_back(resource.payload);
        originals = &fallback;
    }
    scimate::require(originals->size() == project.game.resources.size(), "project resource count differs from original");
    std::vector<bool> modified;
    std::set<std::pair<ResourceType, unsigned>> seen;
    for (std::size_t i = 0; i < originals->size(); ++i) {
        const auto id = project.game.resources[i].entry.id;
        const bool first = seen.emplace(id.type, id.number).second;
        const bool graphics_edit = first && ((id.type == ResourceType::pic && project.vga_pictures.count(id.number)) ||
            (id.type == ResourceType::view && project.vga_views.count(id.number)));
        modified.push_back(project.game.resources[i].payload != originals->at(i) || graphics_edit);
    }
    return modified;
}

void build_project(const std::filesystem::path& directory,
                   const std::filesystem::path& new_game_directory, const ProgressCallback& progress) {
    // Validate the descriptor too; import re-reads edits immediately before build.
    read_name(directory);
    import_sci0_game(directory, new_game_directory, progress);
}
} // namespace scimate
