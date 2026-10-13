// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/sci1.hpp"
#include <filesystem>

namespace scimate {
Game open_sci1_game(const std::filesystem::path& directory, Sci1Format format);
// Writes a new ScummVM-targeted candidate plus migration-report.txt. Container
// round-trip verification precedes directory creation. Runtime proof is manual.
void migrate_sci0_to_sci1_directory(const std::filesystem::path& source,
                                   const std::filesystem::path& destination);
} // namespace scimate
