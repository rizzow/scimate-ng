// SPDX-License-Identifier: GPL-2.0-or-later
#include "project_fixture.hpp"
#include "scimate/project.hpp"
#include <chrono>
#include <iostream>

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    using namespace scimate;
    using namespace fixture;
    try {
        if (argc == 4 && std::string(argv[1]) == "--real-game") {
            const auto project = create_project(argv[2], argv[3], "LSL3 validation");
            const auto output = project.directory / "validation-build";
            build_project(project.directory, output);
            const auto reopened = open_sci0_game(output);
            require(reopened.original.map == project.game.original.map && reopened.original.volumes == project.game.original.volumes, "real game round-trip differs");
            std::cout << project.game.resources.size() << " resources verified byte-identical\n";
            return 0;
        }
        require(argc == 2, "expected fixture root");
        const auto root = fs::path(argv[1]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(root);
        const auto source = root / "source game";
        project_fixture::create(source);
        const auto workspace = root / "project";
        fails([&] { create_project(source, workspace, "\n"); }, "control character");
        require(!fs::exists(workspace), "invalid name created files");
        std::vector<OperationProgress> progress;
        const auto created = create_project(source, workspace, "Larry's \"project\"", [&](const OperationProgress& value) { progress.push_back(value); });
        require(!progress.empty(), "creation has no progress");
        std::size_t last = 0;
        for (const auto& value : progress) {
            require(value.phase != "Loading edited resources", "creation redundantly reopens exports");
            if (value.phase == "Exporting resources") {
                require(value.completed >= last && value.completed <= value.total, "invalid resource progress");
                last = value.completed;
            }
        }
        require(last == created.game.resources.size(), "export progress never completes");
        require(created.name == "Larry's \"project\"", "project name changed");
        require(created.game.resources.size() == 6, "resources missing");
        fails([&] { create_project(source, workspace, "overwrite"); }, "already exists");
        const auto moved = root / "moved project";
        fs::rename(workspace, moved); fs::rename(source, root / "unavailable source");
        const auto reopened = open_project(moved);
        require(reopened.name == created.name, "moved project did not reopen");
        build_project(moved, root / "unchanged build");
        const auto rebuilt = open_sci0_game(root / "unchanged build");
        require(rebuilt.original.map == created.game.original.map && rebuilt.original.volumes == created.game.original.volumes, "unchanged project differs");
        project_fixture::write(moved / "payloads/0000000.bin", {'n','e','w'});
        require(open_project(moved).game.resources[0].payload == Bytes({'n','e','w'}), "edits not loaded");
        build_project(moved, root / "edited build");
        require(open_sci0_game(root / "edited build").resources[0].payload == Bytes({'n','e','w'}), "edits not built");
        fails([&] { build_project(moved, root / "edited build"); }, "already exists");
        project_fixture::write(moved / "manifest.tsv", {'b','a','d'});
        fails([&] { open_project(moved); }, "manifest");
        project_fixture::write(moved / "project.scimate", {'b','a','d'});
        fails([&] { open_project(moved); }, "descriptor");
        fs::remove_all(root);
        std::cout << "Project lifecycle tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
