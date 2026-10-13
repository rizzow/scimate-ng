// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/resource_map.hpp"
#include "scimate/game_io.hpp"
#include "scimate/sci1_io.hpp"
#include "scimate/script.hpp"
#include "scimate/explain.hpp"
#include "scimate/view_io.hpp"
#include "scimate/picture_io.hpp"
#include "scimate/asset_io.hpp"
#include "help.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
constexpr std::string_view migrate_help = R"(Usage: scimate migrate [--to sci1] [--force] <SCI0-game-directory> <SCI1-game-directory>

Experimental format-based export targeting ScummVM's SCI1 runtime paths.
Parser-bearing games target SCI1 EGA with converted words, grammar and suffix
resource IDs; other games target SCI1 late. Remaps kernel calls and adds FileIO
bridges, retaining EGA assets and validated SCI0 late sound compatibility.
Writes resource.map, resource.000 and migration-report.txt describing the target.

Examples:
  scimate migrate /path/to/source-game ./migrated-sci1
  scimate inspect --format sci1-ega ./migrated-sci1

The source is an actual SCI0 game directory, not the exported payload bundle.
The destination must not exist unless --force replaces its entire directory.
Resources are internally round-trip checked;
runtime compatibility is UNVERIFIED until you validate the result in ScummVM.
This is not a general SCI0 -> SCI1/SCI1.1 converter. No VGA artwork conversion.
See docs/sci1-migration.md for the manual detection and gameplay proof checklist.
Exit codes: 0 candidate written, 1 conversion/I/O error, 2 usage error.
)";

void write_directory(const std::filesystem::path& source,
                     const std::filesystem::path& destination, bool force,
                     const std::function<void(const std::filesystem::path&)>& write) {
    namespace fs = std::filesystem;
    if (!force) { write(destination); return; }
    const auto input = fs::weakly_canonical(source);
    const auto output = fs::weakly_canonical(destination);
    auto mismatch = std::mismatch(output.begin(), output.end(), input.begin(), input.end());
    if (output == output.root_path() || mismatch.first == output.end())
        throw std::runtime_error("--force destination must not be the source directory or an ancestor of it");
    if (fs::is_symlink(fs::symlink_status(destination)))
        throw std::runtime_error("--force destination must not be a symbolic link");
    if (!fs::exists(destination)) { write(destination); return; }
    if (!fs::is_directory(destination))
        throw std::runtime_error("--force destination must be a directory");
    // Generate the complete output before removing the previous directory.
    fs::path staging;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        staging = output.parent_path() / (".scimate-output-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(attempt));
        if (fs::create_directory(staging)) break;
        staging.clear();
    }
    if (staging.empty()) throw std::runtime_error("cannot create temporary output directory");
    try {
        write(staging / "output");
        fs::remove_all(output);
        fs::rename(staging / "output", output);
        fs::remove(staging);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        throw;
    }
}

std::vector<std::uint8_t> read_file(const char* path, bool script = false) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error(script ? "cannot open script payload" : "cannot open map file");
    const std::size_t limit = script ? 65535 : 16 * 1024 * 1024;
    std::vector<std::uint8_t> bytes;
    std::array<char, 4096> chunk{};
    while (file.read(chunk.data(), chunk.size()) || file.gcount() > 0) {
        const auto count = static_cast<std::size_t>(file.gcount());
        if (count > limit - bytes.size()) throw std::runtime_error(script ? "script exceeds 65535-byte limit" : "map exceeds CLI limit of 16 MiB");
        bytes.insert(bytes.end(), chunk.data(), chunk.data() + count);
    }
    if (file.bad() || !file.eof()) throw std::runtime_error(script ? "failed to read script payload" : "failed to read map file");
    return bytes;
}
} // namespace

int main(int argc, char** argv) {
    if (const auto result = scimate::cli::show_help(argc, argv, true, migrate_help)) return *result;
    const std::string_view command = argc > 1 ? argv[1] : "";
    const bool migrating = command == "migrate";
    const bool listing = command == "list";
    const bool inspect = command == "inspect";
    const bool explain = command == "explain";
    const bool disassemble = command == "disassemble";
    const bool exporting = command == "export";
    const bool importing = command == "import";
    const bool view_export = command == "view-export";
    const bool view_import = command == "view-import";
    const bool picture_export = command == "picture-export";
    const bool picture_import = command == "picture-import";
    const bool resource_export = command == "resource-export", resource_import = command == "resource-import";
    const bool asset_export = command == "sound-export" || command == "font-export" || command == "cursor-export" || command == "text-export";
    const bool asset_import = command == "sound-import" || command == "font-import" || command == "cursor-import" || command == "text-import";
    std::vector<const char*> paths;
    bool valid = true, format_seen = false, options = true, target_seen = false, force = false, json = false;
    std::string_view format = "sci0";
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (options && arg == "--") { options = false; continue; }
        if (options && arg == "--json" && explain && !json) {
            json = true;
        } else if (options && arg == "--force" && (exporting || migrating)) {
            force = true;
        } else if (options && arg == "--format") {
            if (format_seen || i+1 == argc) { valid = false; break; }
            format = argv[++i]; format_seen = true;
            if (format != "sci0" && format != "sci1" && format != "sci11" && format != "sci1-ega") { valid = false; break; }
        } else if (options && arg == "--to" && migrating) {
            if (target_seen || i+1 == argc || std::string_view(argv[i+1]) != "sci1") { valid = false; break; }
            target_seen = true; ++i;
        } else if (options && arg.substr(0,2) == "--") { valid = false; break; }
        else paths.push_back(argv[i]);
    }
    if (format != "sci0" && !listing && !inspect) valid = false;
    if (!valid || (!migrating && !listing && !inspect && !explain && !disassemble && !exporting && !importing && !view_export && !view_import && !picture_export && !picture_import && !resource_export && !resource_import && !asset_export && !asset_import) ||
        (explain ? (paths.size() != 1 && paths.size() != 3) : paths.size() != (resource_export ? 4u : (resource_import || asset_export || asset_import) ? 2u : ((migrating || exporting || importing || view_export || view_import || picture_export || picture_import) ? 2u : 1u)))) {
        scimate::cli::usage_error(command, true, migrate_help);
        return 2;
    }
    try {
        if (migrating) {
            write_directory(paths[0], paths[1], force, [&](const auto& destination) {
                scimate::migrate_sci0_to_sci1_directory(paths[0], destination);
            });
            std::cout << "Wrote experimental SCI1 candidate: " << paths[1]
                      << "\nRuntime proof remains UNVERIFIED; see migration-report.txt.\n";
            if (!std::cout) throw std::runtime_error("failed to write migration result");
            return 0;
        }
        const auto sci1_format = format == "sci11" ? scimate::Sci1Format::sci11 :
                                format == "sci1-ega" ? scimate::Sci1Format::sci1_ega : scimate::Sci1Format::sci1_late;
        if (explain) {
            std::optional<scimate::ResourceId> focus;
            if (paths.size() == 3) {
                unsigned number = 0;
                const std::string_view kind = paths[1], text = paths[2];
                const auto parsed = std::from_chars(text.data(), text.data()+text.size(), number);
                std::optional<scimate::ResourceType> type;
                for (unsigned value = 0; value <= 9; ++value)
                    if (scimate::resource_type_name(static_cast<scimate::ResourceType>(value)) == kind) type = static_cast<scimate::ResourceType>(value);
                if (!type || parsed.ec != std::errc{} ||
                    parsed.ptr != text.data()+text.size() || number > 2047) {
                    std::cerr << "Focus must be an SCI0 resource type and a decimal number from 0 to 2047.\n"
                              << scimate::cli::command_help("explain", true);
                    return 2;
                }
                focus = scimate::ResourceId{*type, static_cast<std::uint16_t>(number)};
            }
            const auto analysis = scimate::analyze_sci0_game(scimate::open_sci0_game(paths[0]), focus);
            std::cout << (json ? scimate::format_explanation_json(analysis) : scimate::format_explanation_text(analysis));
            if (!std::cout) throw std::runtime_error("failed to write explanation");
            return 0;
        }
        if (resource_export || resource_import || asset_export || asset_import) {
            const auto alias_type = command.substr(0,command.find('-'));
            scimate::ResourceId id{scimate::ResourceType::sound,0};
            auto parse_type = [](std::string_view name) {
                for (unsigned type=0;type<=9;++type)
                    if (scimate::resource_type_name(static_cast<scimate::ResourceType>(type))==name) return static_cast<scimate::ResourceType>(type);
                throw std::invalid_argument("resource type must be view, pic, script, text, sound, memory, vocab, font, cursor, or patch");
            };
            if (resource_export || asset_export) {
                const char* payload=paths[0];const char* destination=paths[1];
                if (resource_export) {
                    unsigned number=0;const std::string_view number_text=paths[1];
                    const auto parsed=std::from_chars(number_text.data(),number_text.data()+number_text.size(),number);
                    if (parsed.ec!=std::errc{} || parsed.ptr!=number_text.data()+number_text.size() || number>2047) {std::cerr<<"Resource number must be 0..2047.\n";return 2;}
                    try {id.type=parse_type(paths[0]);} catch(const std::invalid_argument& error) {std::cerr<<error.what()<<'\n';return 2;}
                    id.number=static_cast<std::uint16_t>(number);payload=paths[2];destination=paths[3];
                } else id.type=parse_type(alias_type);
                scimate::export_sci0_asset_file(id,payload,destination);
                std::cout<<"Exported resource bundle: "<<destination<<'\n';
            } else {
                if (asset_import) {
                    scimate::import_sci0_asset(paths[0],&id);
                    if (id.type!=parse_type(alias_type)) throw std::runtime_error("bundle resource type does not match import command");
                }
                scimate::import_sci0_asset_file(paths[0],paths[1]);std::cout<<"Encoded and verified resource: "<<paths[1]<<'\n';
            }
            if (!std::cout) throw std::runtime_error("failed to write result");
            return 0;
        }
        if (picture_export || picture_import) {
            if (picture_export) scimate::export_sci0_picture_file(paths[0],paths[1]);
            else scimate::import_sci0_picture_file(paths[0],paths[1]);
            std::cout << (picture_export ? "Exported editable picture: " : "Encoded and verified picture: ") << paths[1] << '\n';
            if (!std::cout) throw std::runtime_error("failed to write result");
            return 0;
        }
        if (view_export || view_import) {
            if (view_export) scimate::export_sci0_view_file(paths[0],paths[1]);
            else scimate::import_sci0_view_file(paths[0],paths[1]);
            std::cout << (view_export ? "Exported editable view: " : "Encoded and verified view: ") << paths[1] << '\n';
            if (!std::cout) throw std::runtime_error("failed to write result");
            return 0;
        }
        if (disassemble) {
            std::cout << scimate::disassemble_sci0_script(read_file(paths[0], true));
            if (!std::cout) throw std::runtime_error("failed to write disassembly");
            return 0;
        }
        if (exporting || importing) {
            if (exporting) {
                scimate::ExportSummary summary;
                write_directory(paths[0], paths[1], force, [&](const auto& destination) {
                    summary = scimate::export_sci0_game(paths[0], destination);
                });
                std::cout << "Scripts: " << summary.script_listings << " readable listings, "
                          << summary.script_errors << " errors (see scripts/*.error.txt).\n";
                std::cout << "Pictures: " << summary.editable_pictures << " editable PNG bundles, "
                          << summary.picture_errors << " errors (see pictures/*.error.txt).\n";
                std::cout << "Other resources: " << summary.editable_assets << " editable bundles, "
                          << summary.binary_assets << " byte-level bundles, " << summary.asset_errors << " conversion diagnostics (see assets/).\n";
                std::cout << "Views: " << summary.editable_views << " editable PNG bundles, "
                          << summary.view_errors << " errors (see views/*.error.txt).\n";
            }
            else scimate::import_sci0_game(paths[0], paths[1]);
            std::cout << (exporting ? "Exported to " : "Rebuilt and verified: ") << paths[1] << '\n';
            if (!std::cout) throw std::runtime_error("failed to write result");
            return 0;
        }
        if (inspect) {
            const auto game = format == "sci0" ? scimate::open_sci0_game(paths[0]) : scimate::open_sci1_game(paths[0], sci1_format);
            std::cout << "index\ttype\tnumber\tvolume\toffset\tcompression\tdecoded_size\n";
            for (std::size_t i = 0; i < game.resources.size(); ++i) {
                const auto& r = game.resources[i];
                std::cout << i << '\t' << scimate::resource_type_name(r.entry.id.type) << '\t'
                          << r.entry.id.number << '\t' << static_cast<unsigned>(r.entry.volume) << '\t'
                          << r.entry.offset << '\t' << r.compression << '\t' << r.payload.size() << '\n';
            }
            if (!std::cout) throw std::runtime_error("failed to write listing");
            return 0;
        }
        const auto bytes = read_file(paths[0]);
        const auto entries = format == "sci0" ? scimate::read_sci0_map(bytes) : scimate::read_sci1_map(bytes, sci1_format);
        std::cout << "type\tnumber\tvolume\toffset\n";
        for (const auto& entry : entries) {
            std::cout << scimate::resource_type_name(entry.id.type) << '\t'
                      << entry.id.number << '\t' << static_cast<unsigned>(entry.volume)
                      << '\t' << entry.offset << '\n';
        }
        if (!std::cout) throw std::runtime_error("failed to write listing");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scimate: " << paths[0] << ": " << error.what() << '\n';
        return 1;
    }
}
