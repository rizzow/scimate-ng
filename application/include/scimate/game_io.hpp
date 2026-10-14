// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "scimate/resource_game.hpp"
#include "scimate/progress.hpp"
#include "scimate/sci1.hpp"
#include <filesystem>
#include <string>

namespace scimate {
std::string volume_filename(std::uint8_t number);
Game open_sci0_game(const std::filesystem::path& directory, const ProgressCallback& progress = {});

// Validate the manifest/snapshots and load current payload and PNG edits.
Game open_sci0_export(const std::filesystem::path& directory, const ProgressCallback& progress = {},
                      std::vector<Bytes>* original_payloads = nullptr, VgaPictureEdits* pictures = nullptr,
                      VgaViewEdits* views = nullptr);

struct ExportSummary {
    std::size_t script_listings = 0;
    std::size_t script_errors = 0;
    std::size_t editable_views = 0;
    std::size_t view_errors = 0;
    std::size_t editable_pictures = 0;
    std::size_t picture_errors = 0;
    std::size_t editable_assets = 0;
    std::size_t binary_assets = 0;
    std::size_t asset_errors = 0;
};

// Creates a new destination; never overwrites an existing file or directory.
// Exports raw decoded payloads, a manifest, and original map/volume snapshots.
// Script resources also receive inspection-only .asm listings (or .error.txt).
// Supported EGA views and pictures receive editable PNG bundles under views/ and pictures/.
ExportSummary export_sci0_game(const std::filesystem::path& game_directory,
                      const std::filesystem::path& destination, const ProgressCallback& progress = {});

// Export an already decoded game without reading it again.
ExportSummary export_sci0_game(const Game& game, const std::filesystem::path& destination,
                              const ProgressCallback& progress = {});

// Import binary payload or editable resource bundles; conflicting edits fail.
// The game manifest and original snapshots must agree.
// Rebuilds and verifies the result before creating a new output directory.
void import_sci0_game(const std::filesystem::path& export_directory,
                      const std::filesystem::path& destination, const ProgressCallback& progress = {});
} // namespace scimate
