// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <filesystem>
#include <utility>
namespace scimate {
Bytes read_binary_file(const std::filesystem::path& path, std::size_t limit);
void write_new_file(const std::filesystem::path& path, const Bytes& contents);
// Roll back related file replacements on failure, retaining unrecoverable backups.
void replace_files(const std::vector<std::pair<std::filesystem::path, Bytes>>& files);
}
