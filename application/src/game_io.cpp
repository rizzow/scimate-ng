// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/game_io.hpp"
#include "scimate/script.hpp"
#include "scimate/view_io.hpp"
#include "scimate/picture_io.hpp"
#include "scimate/asset_io.hpp"
#include "files.hpp"
#include "scimate/workspace.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace scimate {
namespace {
using detail::read_file;
using detail::write_file;
using detail::create_destination;
void report(const ProgressCallback& callback, const std::string& phase,
            std::size_t done = 0, std::size_t total = 0, const std::string& detail = {}) {
    if (callback) callback({phase, done, total, detail});
}
std::string resource_label(const Resource& resource) {
    return std::string(resource_type_name(resource.entry.id.type)) + " " + std::to_string(resource.entry.id.number);
}

std::string payload_filename(std::size_t index) {
    return workspace_detail::record_stem(index) + ".bin";
}

std::string manifest(const Game& game) {
    std::ostringstream text;
    text << "SCIMATE-SCI0-EXPORT\t1\n"
         << "index\ttype\tnumber\tvolume\toffset\tcompression\tdecoded_size\tfile\n";
    for (std::size_t i = 0; i < game.resources.size(); ++i) {
        const auto& r = game.resources[i];
        text << i << '\t' << static_cast<unsigned>(r.entry.id.type) << '\t' << r.entry.id.number
             << '\t' << static_cast<unsigned>(r.entry.volume) << '\t' << r.entry.offset
             << '\t' << r.compression << '\t' << r.payload.size() << '\t' << payload_filename(i) << '\n';
    }
    return text.str();
}

void write_game(const GameFiles& files, const std::filesystem::path& directory) {
    for (const auto& volume : files.volumes)
        write_file(directory / volume_filename(volume.first), volume.second);
    write_file(directory / workspace_detail::map_file, files.map);
}
} // namespace

std::string volume_filename(std::uint8_t number) {
    std::ostringstream name;
    name << "resource." << std::setfill('0') << std::setw(3) << static_cast<unsigned>(number);
    return name.str();
}

Game open_sci0_game(const std::filesystem::path& directory, const ProgressCallback& progress) {
    report(progress, "Reading resource map", 0, 0, directory.string());
    GameFiles files;
    files.map = read_file(directory / workspace_detail::map_file, max_map_bytes);
    std::size_t total = files.map.size();
    const auto entries = read_sci0_map(files.map);
    for (const auto& entry : entries) {
        if (files.volumes.count(entry.volume) != 0) continue;
        report(progress, "Reading resource volumes", 0, 0, volume_filename(entry.volume));
        auto bytes = read_file(directory / volume_filename(entry.volume), max_game_bytes - total);
        total += bytes.size();
        files.volumes.emplace(entry.volume, std::move(bytes));
    }
    report(progress, "Decoding resources", 0, 0, std::to_string(entries.size()) + " map entries");
    return read_sci0_game(std::move(files));
}

ExportSummary export_sci0_game(const std::filesystem::path& game_directory,
                      const std::filesystem::path& destination, const ProgressCallback& progress) {
    return export_sci0_game(open_sci0_game(game_directory, progress), destination, progress);
}

ExportSummary export_sci0_game(const Game& game, const std::filesystem::path& destination,
                              const ProgressCallback& progress) {
    const auto text = manifest(game);
    ExportSummary summary;
    create_destination(destination);
    std::filesystem::create_directory(destination / "original");
    std::filesystem::create_directory(destination / "payloads");
    std::filesystem::create_directory(destination / "scripts");
    std::filesystem::create_directory(destination / "views");
    std::filesystem::create_directory(destination / "pictures");
    std::filesystem::create_directory(destination / "assets");
    report(progress, "Saving original resources", 0, 0, destination.string());
    write_game(game.original, destination / "original");
    for (std::size_t i = 0; i < game.resources.size(); ++i) {
        report(progress, "Exporting resources", i, game.resources.size(), resource_label(game.resources[i]));
        write_file(destination / "payloads" / payload_filename(i), game.resources[i].payload);
        if (game.resources[i].entry.id.type == ResourceType::view) {
            const auto stem = std::filesystem::path(payload_filename(i)).stem().string();
            bool supported = true;
            try { read_sci0_view(game.resources[i].payload); }
            catch (const std::runtime_error& error) {
                supported = false;
                ++summary.view_errors;
                const std::string diagnostic = std::string("View PNG export failed; raw payload remains available.\n") + error.what() + "\n";
                write_file(destination / "views" / (stem + ".error.txt"),Bytes(diagnostic.begin(),diagnostic.end()));
            }
            if (supported) {
                export_sci0_view(game.resources[i].payload,destination / "views" / stem);
                ++summary.editable_views;
            } else {
                const auto result = export_sci0_asset(game.resources[i].entry.id,game.resources[i].payload,destination / "assets" / stem);
                if (result.editable) ++summary.editable_assets;
                else ++summary.binary_assets;
                if (result.conversion_error) ++summary.asset_errors;
            }
        }
        if (game.resources[i].entry.id.type == ResourceType::pic) {
            const auto stem = std::filesystem::path(payload_filename(i)).stem().string();
            bool supported = true;
            try { read_sci0_picture(game.resources[i].payload); }
            catch (const std::runtime_error& error) {
                supported = false;
                ++summary.picture_errors;
                const std::string diagnostic = std::string("Picture PNG export failed; raw payload remains available.\n") + error.what() + "\n";
                write_file(destination / "pictures" / (stem + ".error.txt"),Bytes(diagnostic.begin(),diagnostic.end()));
            }
            if (supported) {
                export_sci0_picture(game.resources[i].payload,destination / "pictures" / stem);
                ++summary.editable_pictures;
            } else {
                const auto result = export_sci0_asset(game.resources[i].entry.id,game.resources[i].payload,destination / "assets" / stem);
                if (result.editable) ++summary.editable_assets;
                else ++summary.binary_assets;
                if (result.conversion_error) ++summary.asset_errors;
            }
        }
        if (game.resources[i].entry.id.type != ResourceType::view && game.resources[i].entry.id.type != ResourceType::pic) {
            const auto stem = std::filesystem::path(payload_filename(i)).stem();
            const auto result = export_sci0_asset(game.resources[i].entry.id, game.resources[i].payload, destination / "assets" / stem);
            if (result.editable) ++summary.editable_assets;
            else ++summary.binary_assets;
            if (result.conversion_error) ++summary.asset_errors;
        }
        if (game.resources[i].entry.id.type == ResourceType::script) {
            std::string listing;
            std::string extension = ".asm";
            try {
                listing = disassemble_sci0_script(game.resources[i].payload, game.resources[i].entry.id.number);
                ++summary.script_listings;
            } catch (const std::runtime_error& error) {
                extension = ".error.txt";
                ++summary.script_errors;
                listing = "Script disassembly failed; raw payload remains available.\n" + std::string(error.what()) + "\n";
            }
            const auto stem = std::filesystem::path(payload_filename(i)).stem().string();
            write_file(destination / "scripts" / (stem + extension), Bytes(listing.begin(), listing.end()));
        }
    }
    report(progress, "Exporting resources", game.resources.size(), game.resources.size(), "Writing manifest");
    // Written last so an interrupted export lacks its completion manifest.
    write_file(destination / "manifest.tsv", Bytes(text.begin(), text.end()));
    return summary;
}

Game open_sci0_export(const std::filesystem::path& export_directory, const ProgressCallback& progress,
                      std::vector<Bytes>* original_payloads) {
    auto game = open_sci0_game(export_directory / "original", progress);
    if (original_payloads) {
        original_payloads->clear();
        for (const auto& resource : game.resources) original_payloads->push_back(resource.payload);
    }
    const auto text = manifest(game);
    const auto saved = read_file(export_directory / "manifest.tsv", text.size());
    if (saved != Bytes(text.begin(), text.end()))
        throw std::runtime_error("manifest does not match original game (unsupported version or metadata edits)");
    std::size_t total = 0;
    for (std::size_t i = 0; i < game.resources.size(); ++i) {
        report(progress, "Loading edited resources", i, game.resources.size(), resource_label(game.resources[i]));
        auto bytes = read_file(export_directory / "payloads" / payload_filename(i), 65535);
        const auto& original = game.resources[i].payload;
        const auto view_directory = export_directory / "views" / std::filesystem::path(payload_filename(i)).stem();
        if (game.resources[i].entry.id.type == ResourceType::view && std::filesystem::exists(view_directory)) {
            if (!std::filesystem::is_directory(std::filesystem::symlink_status(view_directory)))
                throw std::runtime_error("expected view directory: " + view_directory.string());
            if (read_file(view_directory / workspace_detail::original_file,65535) != original)
                throw std::runtime_error("view snapshot differs from original resource");
            auto edited = import_sci0_view(view_directory);
            bytes = workspace_detail::reconcile(original, bytes, edited, "record " + std::to_string(i));
        }
        const auto picture_directory = export_directory / "pictures" / std::filesystem::path(payload_filename(i)).stem();
        if (game.resources[i].entry.id.type == ResourceType::pic && std::filesystem::exists(picture_directory)) {
            if (!std::filesystem::is_directory(std::filesystem::symlink_status(picture_directory)))
                throw std::runtime_error("expected picture directory: " + picture_directory.string());
            if (read_file(picture_directory / workspace_detail::original_file,65535) != original)
                throw std::runtime_error("picture snapshot differs from original resource");
            auto edited = import_sci0_picture(picture_directory);
            bytes = workspace_detail::reconcile(original, bytes, edited, "record " + std::to_string(i));
        }
        const auto asset_directory = export_directory / "assets" / std::filesystem::path(payload_filename(i)).stem();
        if (std::filesystem::exists(asset_directory)) {
            if (!std::filesystem::is_directory(std::filesystem::symlink_status(asset_directory)))
                throw std::runtime_error("expected asset directory: " + asset_directory.string());
            if (read_file(asset_directory / workspace_detail::original_file,65535) != original)
                throw std::runtime_error("asset snapshot differs from original resource");
            ResourceId id{};
            auto edited = import_sci0_asset(asset_directory, &id);
            if (id.type != game.resources[i].entry.id.type || id.number != game.resources[i].entry.id.number)
                throw std::runtime_error("asset identity differs from original resource");
            bytes = workspace_detail::reconcile(original, bytes, edited, "record " + std::to_string(i));
        }
        if (bytes.size() > max_decoded_bytes - total) throw std::runtime_error("decoded game exceeds 256 MiB limit");
        total += bytes.size();
        game.resources[i].payload = std::move(bytes);
    }
    report(progress, "Loading edited resources", game.resources.size(), game.resources.size(), "Validated workspace");
    return game;
}

void import_sci0_game(const std::filesystem::path& export_directory,
                      const std::filesystem::path& destination, const ProgressCallback& progress) {
    const auto game = open_sci0_export(export_directory, progress);
    report(progress, "Reconstructing and verifying resources");
    const auto rebuilt = rebuild_sci0_game(game);
    create_destination(destination);
    report(progress, "Writing rebuilt resources", 0, 0, destination.string());
    write_game(rebuilt, destination);
    report(progress, "Verifying saved resources");
    const auto reopened = open_sci0_game(destination);
    if (reopened.original.map != rebuilt.map || reopened.original.volumes != rebuilt.volumes)
        throw std::runtime_error("saved game differs from verified rebuild");
}
} // namespace scimate
