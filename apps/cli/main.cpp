// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/resource_map.hpp"
#include "scimate/game_io.hpp"
#include "scimate/script.hpp"
#include "scimate/view_io.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
constexpr std::string_view usage =
    "Usage: scimate list --format sci0 <resource.map>\n"
    "       scimate inspect --format sci0 <game-directory>\n"
    "       scimate disassemble --format sci0 <decoded-script.bin>\n"
    "       scimate export --format sci0 <game-directory> <new-export-directory>\n"
    "       scimate import --format sci0 <export-directory> <new-game-directory>\n"
    "       scimate view-export --format sci0 <decoded-view.bin> <new-view-directory>\n"
    "       scimate view-import --format sci0 <view-directory> <new-view.bin>\n"
    "Lists type, number, volume, and byte offset (decimal) in map order.\n"
    "list reads only the map; inspect validates volumes and decodes SCI0 methods 0/1/2.\n"
    "export writes payloads and originals; import rebuilds payload edits into a new directory.\n"
    "export also writes scripts/*.asm (or *.error.txt); these listings are not importable source.\n"
    "export/import includes editable view PNGs and placement metadata under views/.\n"
    "SCI0 only; no format detection, recompression, resource ID edits, or engine validation.\n";

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
    const std::string_view command = argc > 1 ? argv[1] : "";
    const bool listing = command == "list";
    const bool inspect = command == "inspect";
    const bool disassemble = command == "disassemble";
    const bool exporting = command == "export";
    const bool importing = command == "import";
    const bool view_export = command == "view-export";
    const bool view_import = command == "view-import";
    if ((!listing && !inspect && !disassemble && !exporting && !importing && !view_export && !view_import) ||
        argc != ((exporting || importing || view_export || view_import) ? 6 : 5) ||
        std::string_view(argv[2]) != "--format" || std::string_view(argv[3]) != "sci0") {
        std::cerr << usage;
        return 2;
    }
    try {
        if (view_export || view_import) {
            if (view_export) scimate::export_sci0_view_file(argv[4],argv[5]);
            else scimate::import_sci0_view_file(argv[4],argv[5]);
            std::cout << (view_export ? "Exported editable view: " : "Encoded and verified view: ") << argv[5] << '\n';
            if (!std::cout) throw std::runtime_error("failed to write result");
            return 0;
        }
        if (disassemble) {
            std::cout << scimate::disassemble_sci0_script(read_file(argv[4], true));
            if (!std::cout) throw std::runtime_error("failed to write disassembly");
            return 0;
        }
        if (exporting || importing) {
            if (exporting) {
                const auto summary = scimate::export_sci0_game(argv[4], argv[5]);
                std::cout << "Scripts: " << summary.script_listings << " readable listings, "
                          << summary.script_errors << " errors (see scripts/*.error.txt).\n";
                std::cout << "Views: " << summary.editable_views << " editable PNG bundles, "
                          << summary.view_errors << " errors (see views/*.error.txt).\n";
            }
            else scimate::import_sci0_game(argv[4], argv[5]);
            std::cout << (exporting ? "Exported to " : "Rebuilt and verified: ") << argv[5] << '\n';
            if (!std::cout) throw std::runtime_error("failed to write result");
            return 0;
        }
        if (inspect) {
            const auto game = scimate::open_sci0_game(argv[4]);
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
        const auto entries = scimate::read_sci0_map(read_file(argv[4]));
        std::cout << "type\tnumber\tvolume\toffset\n";
        for (const auto& entry : entries) {
            std::cout << scimate::resource_type_name(entry.id.type) << '\t'
                      << entry.id.number << '\t' << static_cast<unsigned>(entry.volume)
                      << '\t' << entry.offset << '\n';
        }
        if (!std::cout) throw std::runtime_error("failed to write listing");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scimate: " << argv[4] << ": " << error.what() << '\n';
        return 1;
    }
}
