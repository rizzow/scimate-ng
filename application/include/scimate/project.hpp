// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/game_io.hpp"
#include <utility>

namespace scimate {
// A movable SCI0 export workspace with a versioned project descriptor.
// No absolute source-game path is persisted or needed after creation.
struct Project {
    std::filesystem::path directory;
    std::string name;
    Game game; // Current validated edits; original retains the source snapshots.
    std::vector<Bytes> original_payloads; // Decoded once when creating/opening a project.
};
std::vector<bool> project_modified_records(const Project& project);
// Validates a text edit and chooses one authoritative file without creating
// conflicting binary/table edits. Publish contents and related_writes together
// with replace_files immediately after preparation. This is an optimistic
// check, not a cross-process lock.
struct ProjectTextEdit {
    std::filesystem::path destination;
    Bytes contents;
    Bytes payload;
    bool modified = false;
    std::vector<std::pair<std::filesystem::path, Bytes>> related_writes;
};
ProjectTextEdit prepare_project_text_edit(const Project& project, std::size_t record,
                                          const std::string& table);
// Validate the current descriptor and return replacement bytes for an atomic
// caller-owned write. Reject a name changed externally since this project loaded.
Bytes prepare_project_rename(const Project& project, const std::string& name);
Project create_project(const std::filesystem::path& source_game,
                       const std::filesystem::path& new_directory,
                       const std::string& name, const ProgressCallback& progress = {});
Project open_project(const std::filesystem::path& directory, const ProgressCallback& progress = {});
void build_project(const std::filesystem::path& directory,
                   const std::filesystem::path& new_game_directory, const ProgressCallback& progress = {});
} // namespace scimate
