// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/file_io.hpp"
#include <filesystem>
#include <functional>
#include <utility>
namespace scimate {
// Build before replacing; restore the previous directory if installation fails.
// Failed rollback preserves both outputs and reports their recovery paths.
void write_output_directory(const std::filesystem::path& source,
                            const std::filesystem::path& destination, bool force,
                            const std::function<void(const std::filesystem::path&)>& write);
}
