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

#include <array>
#include <charconv>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
constexpr std::string_view usage =
    "Usage: scimate list [--format sci0|sci1|sci11] <resource.map>\n"
    "       scimate inspect [--format sci0|sci1|sci11] <game-directory>\n"
    "       scimate explain [--format sci0] <game-directory> [view|script <number>]\n"
    "       scimate disassemble [--format sci0] <decoded-script.bin>\n"
    "       scimate export [--format sci0] <game-directory> <new-export-directory>\n"
    "       scimate import [--format sci0] <export-directory> <new-game-directory>\n"
    "       scimate view-export [--format sci0] <decoded-view.bin> <new-view-directory>\n"
    "       scimate view-import [--format sci0] <view-directory> <new-view.bin>\n"
    "       scimate resource-export [--format sci0] <type> <number> <decoded.bin> <new-bundle>\n"
    "       scimate resource-import [--format sci0] <bundle> <new-decoded.bin>\n"
    "       scimate sound-export [--format sci0] <decoded-sound.bin> <new-bundle>\n"
    "       scimate sound-import [--format sci0] <bundle> <new-sound.bin>\n"
    "       scimate font-export|cursor-export|text-export <decoded.bin> <new-bundle>\n"
    "       scimate font-import|cursor-import|text-import <bundle> <new-decoded.bin>\n"
    "       scimate picture-export [--format sci0] <decoded-picture.bin> <new-picture-directory>\n"
    "       scimate picture-import [--format sci0] <picture-directory> <new-picture.bin>\n"
    "       scimate migrate [--to sci1] <SCI0-game-directory> <new-SCI1-game-directory>\n"
    "Run scimate migrate --help for the experimental format-based migration.\n"
    "Run scimate explain --help for heuristic analysis and limitations.\n"
    "SCI0 is the default format. Use -- to end options for paths beginning with --.\n"
    "Lists type, number, volume, and byte offset (decimal) in map order.\n"
    "list reads only the map; inspect validates volumes and decodes SCI0 methods 0/1/2.\n"
    "export writes payloads and originals; import rebuilds payload edits into a new directory.\n"
    "export also writes scripts/*.asm (or *.error.txt); these listings are not importable source.\n"
    "export/import includes editable view PNGs and placement metadata under views/.\n"
    "export/import includes MIDI/WAV sounds, font/cursor PNGs, text/vocabulary tables, and binary assets/.\n"
    "export/import includes background, priority, and control PNGs under pictures/.\n"
    "SCI1 means SCI1 late containers; sci11 means SCI1.1 containers (compression 0/1/2).\n"
    "Editing remains SCI0 only; no format detection, recompression, resource ID edits, or engine validation.\n";

constexpr std::string_view explain_help = R"(Usage: scimate explain [--format sci0] <game-directory>
       scimate explain [--format sci0] <game-directory> view <number>
       scimate explain [--format sci0] <game-directory> script <number>

Explain SCI0 resources using object metadata and conservative bytecode heuristics.
Reports rooms, player-view candidates, sprite loops/cels, constant room calls,
animation-controller clues and possible startup rooms, with evidence and labels.
A room candidate is an object in a script, not necessarily its script number.

Examples:
  scimate explain ./LSL3
  scimate explain ./LSL3 view 42
  scimate explain ./LSL3 script 0

[fact] means decoded data; [likely] means a convention or pattern;
[unresolved] means the available evidence is insufficient. Dynamic IDs, first
playable room, runtime timing and reachability are not proven. No game execution.
Missing vocabularies and malformed individual scripts/views produce partial
analysis with diagnostics. Duplicate resource IDs use the last map entry.
Output goes to stdout. Exit codes: 0 report (possibly partial), 1 input/output
error or absent focused resource, 2 usage error. SCI0 only. Use -- to end options.
)";

constexpr std::string_view migrate_help = R"(Usage: scimate migrate [--to sci1] <SCI0-game-directory> <new-SCI1-game-directory>

Experimental format-based export targeting ScummVM's SCI1 late runtime path.
Writes a directory map, resource.000 and migration-report.txt. Remaps kernel
calls and adds FileIO bridges while retaining EGA assets and parser gameplay.
Parser and SCI0 sound adaptation are not implemented; such calls are rejected.

Examples:
  scimate migrate /path/to/source-game ./migrated-sci1
  scimate inspect --format sci1 ./migrated-sci1

The source is an actual SCI0 game directory, not the exported payload bundle.
The destination must not exist. Resources are internally round-trip checked;
runtime compatibility is UNVERIFIED until you validate the result in ScummVM.
This is not a general SCI0 -> SCI1/SCI1.1 converter. No VGA artwork conversion.
See docs/sci1-migration.md for the manual detection and gameplay proof checklist.
Exit codes: 0 candidate written, 1 conversion/I/O error, 2 usage error.
)";

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
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << usage;
        return 0;
    }
    if (argc == 3 && ((std::string_view(argv[1]) == "explain" && std::string_view(argv[2]) == "--help") ||
                      (std::string_view(argv[1]) == "help" && std::string_view(argv[2]) == "explain"))) {
        std::cout << explain_help;
        return 0;
    }
    if (argc == 3 && ((std::string_view(argv[1]) == "migrate" && std::string_view(argv[2]) == "--help") ||
                      (std::string_view(argv[1]) == "help" && std::string_view(argv[2]) == "migrate"))) {
        std::cout << migrate_help; return 0;
    }
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
    bool valid = true, format_seen = false, options = true, target_seen = false;
    std::string_view format = "sci0";
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (options && arg == "--") { options = false; continue; }
        if (options && arg == "--format") {
            if (format_seen || i+1 == argc) { valid = false; break; }
            format = argv[++i]; format_seen = true;
            if (format != "sci0" && format != "sci1" && format != "sci11") { valid = false; break; }
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
            scimate::migrate_sci0_to_sci1_directory(paths[0], paths[1]);
            std::cout << "Wrote experimental SCI1 late candidate: " << paths[1]
                      << "\nRuntime proof remains UNVERIFIED; see migration-report.txt.\n";
            if (!std::cout) throw std::runtime_error("failed to write migration result");
            return 0;
        }
        const auto sci1_format = format == "sci11" ? scimate::Sci1Format::sci11 : scimate::Sci1Format::sci1_late;
        if (explain) {
            std::optional<scimate::ResourceId> focus;
            if (paths.size() == 3) {
                unsigned number = 0;
                const std::string_view kind = paths[1], text = paths[2];
                const auto parsed = std::from_chars(text.data(), text.data()+text.size(), number);
                if ((kind != "view" && kind != "script") || parsed.ec != std::errc{} ||
                    parsed.ptr != text.data()+text.size() || number > 2047) {
                    std::cerr << "Focus must be view or script and a decimal number from 0 to 2047.\n" << explain_help;
                    return 2;
                }
                focus = scimate::ResourceId{kind == "view" ? scimate::ResourceType::view : scimate::ResourceType::script,
                                            static_cast<std::uint16_t>(number)};
            }
            std::cout << scimate::explain_sci0_game(scimate::open_sci0_game(paths[0]), focus);
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
                const auto summary = scimate::export_sci0_game(paths[0], paths[1]);
                std::cout << "Scripts: " << summary.script_listings << " readable listings, "
                          << summary.script_errors << " errors (see scripts/*.error.txt).\n";
                std::cout << "Pictures: " << summary.editable_pictures << " editable PNG bundles, "
                          << summary.picture_errors << " errors (see pictures/*.error.txt).\n";
                std::cout << "Other resources: " << summary.editable_assets << " editable bundles, "
                          << summary.binary_assets << " binary bundles, " << summary.asset_errors << " conversion diagnostics (see assets/).\n";
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
