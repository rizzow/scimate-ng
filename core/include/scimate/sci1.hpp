// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <string>
#include <map>
#include "scimate/picture.hpp"
#include "scimate/view.hpp"
#include "scimate/progress.hpp"

namespace scimate {
// Explicit DOS resource formats, not interpreter auto-detection.
enum class Sci1Format { sci1_late, sci11, sci1_ega, sci1_vga = sci1_ega };
std::vector<ResourceMapEntry> read_sci1_map(const Bytes& bytes, Sci1Format format);
Bytes decode_sci1(std::uint16_t method, const Bytes& input, std::size_t expected_size);
Game read_sci1_game(GameFiles files, Sci1Format format);
// Packs decoded resources as uncompressed records in resource.000. SCI1.1
// offsets are word-aligned. Does not convert resource payloads or scripts.
GameFiles write_sci1_game(const std::vector<Resource>& resources, Sci1Format format);

struct Sci1Migration {
    GameFiles files;
    std::string report;
    std::vector<std::string> auxiliary_file_references;
};
// Experimental format-based SCI0 -> SCI1 late/ScummVM compatibility export.
// Remaps kernels and bridges SCI0 file I/O. The default retains EGA graphics;
// appended bridges preserve original script addresses. Unsupported cases fail.
// A generated container is NOT evidence of successful interpreter playback.
// VGA additionally installs portable parser bytecode, converts graphics and
// changes the game name. Unsupported runtime picture/synonym cases report errors.
enum class Sci1Graphics { ega, vga };
using VgaPictureEdits = std::map<unsigned,VgaPicture>;
using VgaViewEdits = std::map<unsigned,View>;
Sci1Migration migrate_sci0_to_sci1(const Game& game, Sci1Graphics graphics = Sci1Graphics::ega,
                                 const VgaPictureEdits& pictures = {}, const ProgressCallback& progress = {},
                                 const VgaViewEdits& views = {});
} // namespace scimate
