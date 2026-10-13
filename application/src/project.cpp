// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/project.hpp"
#include "files.hpp"
#include <iomanip>
#include <sstream>

namespace scimate {
namespace {
void validate_name(const std::string& name) {
    if (name.empty() || name.size() > 200 || name.find_first_not_of(' ') == std::string::npos)
        throw std::runtime_error("project name must contain 1 to 200 UTF-8 bytes");
    for (const unsigned char c : name)
        if (c < 32 || c == 127) throw std::runtime_error("project name contains a control character");
}

std::string read_name(const std::filesystem::path& directory) {
    const auto bytes = detail::read_file(directory / "project.scimate", 4096);
    std::istringstream input(std::string(bytes.begin(), bytes.end()));
    std::string header, name;
    std::getline(input, header);
    if (header != "SCIMATE-PROJECT 1" || !(input >> std::quoted(name)))
        throw std::runtime_error("unsupported or malformed SCIMate project descriptor");
    input >> std::ws;
    if (!input.eof()) throw std::runtime_error("unexpected data in project descriptor");
    validate_name(name);
    return name;
}
}

Project create_project(const std::filesystem::path& source_game,
                       const std::filesystem::path& new_directory,
                       const std::string& name) {
    validate_name(name);
    // Export refuses existing destinations; write the descriptor only on success.
    export_sci0_game(source_game, new_directory);
    std::ostringstream descriptor;
    descriptor << "SCIMATE-PROJECT 1\n" << std::quoted(name) << '\n';
    const auto text = descriptor.str();
    detail::write_file(new_directory / "project.scimate", Bytes(text.begin(), text.end()));
    return open_project(new_directory);
}

Project open_project(const std::filesystem::path& directory) {
    const auto name = read_name(directory);
    return {std::filesystem::absolute(directory).lexically_normal(), name, open_sci0_export(directory)};
}

void build_project(const std::filesystem::path& directory,
                   const std::filesystem::path& new_game_directory) {
    // Validate the descriptor too; import re-reads edits immediately before build.
    read_name(directory);
    import_sci0_game(directory, new_game_directory);
}
} // namespace scimate
