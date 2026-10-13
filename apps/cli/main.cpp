// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/resource_map.hpp"
#include "scimate/game_io.hpp"
#include "scimate/script.hpp"
#include "scimate/explain.hpp"
#include "scimate/view_io.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
constexpr std::string_view usage =
    "Usage: scimate list [--format sci0] <resource.map>\n"
    "       scimate inspect [--format sci0] <game-directory>\n"
    "       scimate explain [--format sci0] <game-directory> [view|script <number>]\n"
    "       scimate disassemble [--format sci0] <decoded-script.bin>\n"
    "       scimate export [--format sci0] <game-directory> <new-export-directory>\n"
    "       scimate import [--format sci0] <export-directory> <new-game-directory>\n"
    "       scimate view-export [--format sci0] <decoded-view.bin> <new-view-directory>\n"
    "       scimate view-import [--format sci0] <view-directory> <new-view.bin>\n"
    "Run scimate explain --help for heuristic analysis and limitations.\n"
    "SCI0 is the default format. Use -- to end options for paths beginning with --.\n"
    "Lists type, number, volume, and byte offset (decimal) in map order.\n"
    "list reads only the map; inspect validates volumes and decodes SCI0 methods 0/1/2.\n"
    "export writes payloads and originals; import rebuilds payload edits into a new directory.\n"
    "export also writes scripts/*.asm (or *.error.txt); these listings are not importable source.\n"
    "export/import includes editable view PNGs and placement metadata under views/.\n"
    "SCI0 only; no format detection, recompression, resource ID edits, or engine validation.\n";

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
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << usage;
        return 0;
    }
    if (argc == 3 && ((std::string_view(argv[1]) == "explain" && std::string_view(argv[2]) == "--help") ||
                      (std::string_view(argv[1]) == "help" && std::string_view(argv[2]) == "explain"))) {
        std::cout << explain_help;
        return 0;
    }
    const std::string_view command = argc > 1 ? argv[1] : "";
    const bool listing = command == "list";
    const bool inspect = command == "inspect";
    const bool explain = command == "explain";
    const bool disassemble = command == "disassemble";
    const bool exporting = command == "export";
    const bool importing = command == "import";
    const bool view_export = command == "view-export";
    const bool view_import = command == "view-import";
    std::vector<const char*> paths;
    bool valid = true, format_seen = false, options = true;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (options && arg == "--") { options = false; continue; }
        if (options && arg == "--format") {
            if (format_seen || i+1 == argc || std::string_view(argv[i+1]) != "sci0") { valid = false; break; }
            format_seen = true; ++i;
        } else if (options && arg.substr(0,2) == "--") { valid = false; break; }
        else paths.push_back(argv[i]);
    }
    if (!valid || (!listing && !inspect && !explain && !disassemble && !exporting && !importing && !view_export && !view_import) ||
        (explain ? (paths.size() != 1 && paths.size() != 3) : paths.size() != ((exporting || importing || view_export || view_import) ? 2u : 1u))) {
        std::cerr << (explain ? explain_help : usage);
        return 2;
    }
    try {
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
                std::cout << "Views: " << summary.editable_views << " editable PNG bundles, "
                          << summary.view_errors << " errors (see views/*.error.txt).\n";
            }
            else scimate::import_sci0_game(paths[0], paths[1]);
            std::cout << (exporting ? "Exported to " : "Rebuilt and verified: ") << paths[1] << '\n';
            if (!std::cout) throw std::runtime_error("failed to write result");
            return 0;
        }
        if (inspect) {
            const auto game = scimate::open_sci0_game(paths[0]);
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
        const auto entries = scimate::read_sci0_map(read_file(paths[0]));
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
