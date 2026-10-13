// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <iostream>
#include "commands.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace scimate::cli {
inline constexpr std::string_view overview = R"(SCIMate — inspect, export, edit, and rebuild SCI resources

Usage: scimate <command> [options] <arguments>
       scimate help <command>

Inspect games
  list          List resource IDs and volume offsets from a resource.map.
  inspect       Validate a game's map/volumes and decode its resources.
  explain       Report room, sprite, and script clues with confidence labels.
  disassemble   Print an assembly listing for a decoded script payload.

Export and rebuild games
  export        Create a workspace with editable bundles and original snapshots.
  import        Rebuild workspace edits into a new resource directory.

Export and import individual resources
  resource-export  <type> <number> <decoded.bin> <new-bundle>
  resource-import  <bundle> <new-decoded.bin>
  view-export/import       Sprite PNGs and placement metadata.
  picture-export/import    Background, priority, and control-mask PNGs.
  sound-export/import      MIDI, channel routing, and optional PCM WAV.
  font-export/import       Font glyph PNGs and metrics.
  cursor-export/import     Cursor PNG and hotspot.
  text-export/import       Escaped text tables.

  Type-specific export: scimate <type>-export <decoded.bin> <new-bundle>
  Type-specific import: scimate <type>-import <bundle> <new-decoded.bin>
  Parser vocabulary: use resource-export vocab 0 and resource-import.
  Scripts, vocabularies, and instrument banks use JSON with generated schemas.
  Unknown layouts use explicitly labelled byte-level JSON.

Options
  -h, --help       Show help for the selected command.
  --format FORMAT  Select a resource format; sci0 is the default.
  --force          Replace the entire destination directory (export/migrate).
  --               End options, allowing paths that begin with --.

Examples
  scimate inspect ./game
  scimate export ./game ./workspace
  # Edit workspace PNGs, MIDI/WAV files, tables, or resource.json.
  scimate import ./workspace ./rebuilt-game
  scimate sound-export sound.bin ./sound-bundle
  scimate help resource-export

Output destinations must be new unless export/migrate uses --force.
Import does not overwrite a game.
Editing is SCI0 only. Rebuilding resource files does not copy the interpreter,
disk scripts, configuration, or other files needed for a complete runnable game.
Script listings are inspection-only, not compilable source.
Exit status: 0 success; 1 input/output or resource error; 2 invalid usage.
)";

inline std::string command_help(std::string_view command, bool sci1 = false,
                                std::string_view migration_help = {}) {
    if (!command_spec(command)) return {};
    std::string body;
    bool format_option = true;
    if (command == "list") {
        body = R"(Usage: scimate list [--format FORMAT] <resource.map>

List type, number, volume, and byte offset as a tab-separated table.
Map order and duplicate resource IDs are preserved. Volume files are not read.

Example: scimate list ./game/resource.map
)";
    } else if (command == "inspect") {
        body = R"(Usage: scimate inspect [--format FORMAT] <game-directory>

Read resource.map and referenced volumes, validate their records, and decode
every resource. Print metadata as a tab-separated table. The game is unchanged.

Example: scimate inspect ./game
)";
    } else if (command == "export") {
        body = R"(Usage: scimate export [--format sci0] [--force] <game-directory> <workspace>

Create a new workspace containing:
  manifest.tsv and original/  Resource identities and original map/volumes.
  payloads/                  Decoded binary payloads for every resource.
  views/                     Editable sprite PNGs and placement metadata.
  pictures/                  Background, priority, and control-mask PNGs.
  assets/                    MIDI/WAV, font/cursor PNGs, text/vocabulary tables,
                             and JSON records with generated schemas.
  scripts/                   Inspection-only assembly listings.

Unknown layouts expose byte-level JSON and conversion diagnostics.
Keep the manifest and original snapshots unchanged. Duplicate IDs stay separate.
The destination must not exist unless --force replaces its entire directory.
Output is generated before replacement. The source game is unchanged.
--force refuses symbolic links and destinations containing the source directory.

Example: scimate export ./game ./workspace
Next: edit workspace files, then scimate import ./workspace ./rebuilt-game
)";
    } else if (command == "import") {
        body = R"(Usage: scimate import [--format sci0] <workspace> <new-game-directory>

Load binary payload and editable bundle changes, rebuild the resource map and
volumes, and verify the saved result. Unchanged imports preserve original files
byte-for-byte. Conflicting binary/bundle edits are rejected.

The destination must not exist. Keep manifest.tsv and original snapshots
unchanged. Resource IDs cannot be added, removed, or renamed. Edited packaged
payloads cannot exceed 65,531 bytes; changed records are stored uncompressed.

This rebuilds resource files, not a complete runnable game folder. It does not
compile script source or copy an interpreter, configuration, or disk patch files.

Example: scimate import ./workspace ./rebuilt-game
)";
    } else if (command == "explain") {
        body = R"(Usage: scimate explain [--format sci0] [--json] <game-directory>
       scimate explain [--format sci0] [--json] <game-directory> <type> <number>

Explain SCI0 resources using a structured static-analysis model.

Report room instances, player-view candidates, sprite animations, constant room
transitions, and possible opening rooms using metadata and bytecode heuristics.
Focus on a resource with a decimal number from 0 to 2047, or inspect the game.
Types: view, pic, script, text, sound, memory, vocab, font, cursor, patch.
The reference section includes outgoing uses and incoming references to a focus.
Recognized uses include view/picture properties and constant assignments,
external script calls, and class dependencies. No match does not mean unused.
--json emits schema version 1: evidence nodes with explicit resource links,
offsets, confidence, diagnostics, and the whole-game reference graph. Focus
filters evidence nodes/text output; JSON references always cover the whole game.

  [fact]        Decoded data.
  [likely]      An engine convention or recognized pattern.
  [unresolved]  Evidence is insufficient.

A room candidate is an object, not necessarily its script number. Dynamic IDs,
runtime timing, reachability, and the first playable room are not proven.
Malformed individual scripts/views and missing vocabularies produce partial
reports with diagnostics. Duplicate IDs use the first map entry. No game runs.

Examples:
  scimate explain ./game
  scimate explain ./game view 42
  scimate explain ./game script 0
  scimate explain ./game pic 7
  scimate explain --json ./game > explanation.json
)";
    } else if (command == "disassemble") {
        body = R"(Usage: scimate disassemble [--format sci0] <decoded-script.bin>

Print an assembly listing to stdout with opcode mnemonics, labels, objects,
methods, strings, exports, and locals. Unresolved names remain numeric.
Input is a decoded payload, not a compressed volume record or SCI patch file.
The listing is not decompiled source and cannot be compiled or imported.

Example: scimate disassemble ./workspace/payloads/0000000.bin
)";
    } else if (command == "resource-export") {
        body = R"(Usage: scimate resource-export [--format sci0] <type> <number>
                               <decoded.bin> <new-bundle>

Export one decoded payload as standard media/text or structured JSON.
The type is one of:
  view, pic, script, text, sound, memory, vocab, font, cursor, patch
The resource number is decimal, 0..2047. Vocabulary 0 uses an editable parser
dictionary; known vocabulary, script, and instrument-bank layouts use named JSON
records. Edit resource.json; resource.schema.json describes fields and ranges.
Script edits preserve instruction widths, addresses, and string byte lengths.
Unknown layouts use labelled byte records; engine semantics are not validated.
Unsupported media conversions also include conversion.error.txt.

Keep asset.tsv and original.bin unchanged. The destination must not exist.
Input is a decoded payload, not a compressed record or standalone patch file.

Examples:
  scimate resource-export sound 42 sound.bin ./sound-bundle
  scimate resource-export vocab 0 vocab.bin ./dictionary
  scimate resource-export script 100 script.bin ./script-bundle
Next: scimate resource-import ./dictionary edited-vocab.bin
)";
    } else if (command == "resource-import") {
        body = R"(Usage: scimate resource-import [--format sci0] <bundle> <new-decoded.bin>

Import a bundle created by resource-export or a sound/font/cursor/text export.
asset.tsv identifies its type and number. Import validates editable files,
preserves unchanged original bytes, and saves the reconstructed decoded payload.
For view-export/picture-export bundles, use view-import/picture-import instead.

Keep asset.tsv and original.bin unchanged. The output file must not exist.
This writes a decoded payload, not a patch file or packaged game. To rebuild a
whole workspace, use scimate import instead.

Example: scimate resource-import ./dictionary edited-vocab.bin
)";
    } else if (command == "migrate" && sci1) {
        format_option = false;
        body = migration_help.empty() ? R"(Usage: scimate migrate [--to sci1] [--force] <SCI0-game-directory> <SCI1-directory>

Create an experimental SCI1 late compatibility candidate from a SCI0 game.
The destination must not exist unless --force replaces its entire directory.
Review migration-report.txt before use.
Unsupported transformations fail explicitly; successful conversion does not
prove runtime detection, graphics/sound compatibility, or gameplay.

Example: scimate migrate --to sci1 ./game ./sci1-candidate
)" : std::string(migration_help);
    } else {
        const auto separator = command.find('-');
        if (separator == std::string_view::npos) return {};
        const auto type = command.substr(0, separator);
        const auto action = command.substr(separator + 1);
        if (action != "export" && action != "import") return {};
        if (type != "view" && type != "picture" && type != "sound" &&
            type != "font" && type != "cursor" && type != "text") return {};
        const bool exporting = action == "export";
        body = "Usage: scimate " + std::string(command) + " [--format sci0] " +
            (exporting ? "<decoded.bin> <new-bundle>\n" : "<bundle> <new-decoded.bin>\n");
        body += exporting ? "\nCreate an editable bundle from a decoded payload. The bundle must not exist.\n"
                          : "\nReconstruct a decoded payload from an editable bundle. Output must not exist.\n";
        if (type == "view") body += R"(
Edit loop-N-cel-N.png and view.tsv. Use exact EGA colors and binary alpha.
Frames may be resized to 1..320 × 1..200. Position offsets are -128..127;
transparent indices are 0..15. Mirrored loops follow their source frames.
Keep loop/cel identities and original.bin unchanged.
)";
        if (type == "picture") body += R"(
Edit visual.png, priority.png (sprite occlusion), and control.png (region masks).
All planes must remain opaque 320×190 PNGs using exact EGA colors; mask colors
represent indices 0..15. Keep picture.tsv and original.bin unchanged.
Edits retain original vector commands and append verified pixel strokes.
)";
        if (type == "sound") body += R"(
Edit sound.mid, channels.tsv, and sample.wav when present. SCI0 late sounds use
a 33-byte header and 60 ticks/second. MIDI import accepts formats 0/1 with PPQN
timing and tempo changes. Preserve channel 15 loop/cue messages used by scripts.
PCM WAV must remain unsigned 8-bit mono with a sample rate of 1..65,535 Hz.
Early SCI0 17-byte headers are supported when late-header parsing fails;
their voice/device fields are packed nibbles (0..15). Ambiguous inputs prefer
the late header. Unsupported sound layouts use byte-level JSON with diagnostics.
)";
        if (type == "font") body += R"(
Edit glyph-N.png and font.tsv. Glyphs use opaque black/white pixels; update
metadata dimensions when resizing PNGs. Line height is 1..255; glyph dimensions
are 0..255 wide and 0..200 high. Empty glyphs use at least a 1×1 placeholder PNG.
Keep the number of glyphs, indices, filenames, and original.bin unchanged.
)";
        if (type == "cursor") body += R"(
Edit cursor.png and cursor.tsv. SCI0 cursors are 16×16, black/white with binary
transparency. Hotspot 0 means (0,0); hotspot 8 means (8,8). Gray pixels and other
hotspots are unsupported. Opaque EGA index 3 is reserved for transparency.
)";
        if (type == "text") body += R"(
Edit strings.tsv. Text uses sequential indices and byte-preserving ASCII escapes:
\\, \n, \r, \t, and \xNN. NUL bytes are unsupported. Adding/removing strings
changes indices referenced by scripts; character encodings are not guessed.
)";
        if (type != "view" && type != "picture") {
            body += "\nKeep asset.tsv and original.bin unchanged. Unknown layouts use resource.json.\n";
            if (exporting) body += "This alias records number 0; use resource-export for another resource number.\n";
        }
        body += "\nInput/output payloads are decoded bytes, not compressed records or patch files.\n";
        body += "Unchanged imports preserve original bytes. Use game import to rebuild a workspace.\n";
        body += "\nExample: scimate " + std::string(command) +
            (exporting ? " " + std::string(type) + ".bin ./" + std::string(type) + "-bundle\n"
                       : " ./" + std::string(type) + "-bundle edited-" + std::string(type) + ".bin\n");
    }
    body += "\nOptions: -h, --help; -- ends options for paths beginning with --.\n";
    if (format_option) {
        body += "Formats: ";
        body += sci1 && (command == "list" || command == "inspect")
            ? "sci0 (default), sci1 (SCI1 late), sci1-ega (SCI1 EGA), sci11 (SCI1.1).\n"
            : "sci0 (default).\n";
    }
    body += "Exit status: 0 success; 1 input/output or resource error; 2 invalid usage.\n";
    return body;
}

inline std::optional<int> show_help(int argc, char** argv, bool sci1 = false,
                                  std::string_view migration_help = {}) {
    if (argc == 1 || (argc == 2 && (std::string_view(argv[1]) == "--help" ||
        std::string_view(argv[1]) == "-h" || std::string_view(argv[1]) == "help"))) {
        std::cout << overview;
        if (sci1) std::cout << "\nExperimental SCI1 support\n"
            "  list/inspect --format sci1|sci11   Read SCI1 late/SCI1.1 containers.\n"
            "  migrate                         Create an unverified compatibility candidate.\n"
            "  Run scimate help migrate for scope and limitations.\n";
        return std::cout ? 0 : 1;
    }
    if (argc < 2) return {};
    std::string_view command = argv[1];
    bool requested = false;
    if (command == "help") {
        if (argc != 3) {std::cerr << "Usage: scimate help <command>\n";return 2;}
        command = argv[2];requested = true;
    } else {
        for (int i = 2; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--") break;
            if (arg == "--help" || arg == "-h") {requested = true;break;}
        }
    }
    if (!requested) return {};
    const auto help = command_help(command, sci1, migration_help);
    if (help.empty()) {
        std::cerr << "Unknown command: " << command << "\nRun scimate --help for available commands.\n";
        return 2;
    }
    std::cout << help;
    return std::cout ? 0 : 1;
}

inline void usage_error(std::string_view command, bool sci1 = false,
                        std::string_view migration_help = {}) {
    const auto help = command_help(command, sci1, migration_help);
    if (help.empty()) {
        std::cerr << "Unknown command: " << command
                  << "\nRun scimate --help for available commands.\n";
        return;
    }
    std::cerr << "Invalid arguments for scimate " << command << ".\n";
    // Include the entire usage block, including alternate invocation forms.
    std::cerr << help.substr(0, help.find("\n\n"))
              << "\nRun scimate help " << command << " for options and examples.\n";
}
} // namespace scimate::cli
