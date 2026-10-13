// SPDX-License-Identifier: GPL-2.0-or-later
#include "project_fixture.hpp"
#include "scimate/project.hpp"
#include "scimate/asset_io.hpp"
#include "scimate/assets.hpp"
#include "scimate/output.hpp"
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
        require(created.game.resources.size() == 7, "resources missing");
        fails([&] { create_project(source, workspace, "overwrite"); }, "already exists");
        const auto moved = root / "moved project";
        fs::rename(workspace, moved); fs::rename(source, root / "unavailable source");
        const auto reopened = open_project(moved);
        require(reopened.name == created.name, "moved project did not reopen");
        fails([&] { prepare_project_rename(reopened, ""); }, "project name");
        fails([&] { prepare_project_rename(reopened, "bad\nname"); }, "control character");
        const auto renamed = std::string("Renamed \"Larry\" project");
        project_fixture::write(moved / "project.scimate", prepare_project_rename(reopened, renamed));
        require(open_project(moved).name == renamed, "renamed project did not persist");
        fails([&] { prepare_project_rename(reopened, "stale rename"); }, "changed on disk");
        build_project(moved, root / "unchanged build");
        const auto rebuilt = open_sci0_game(root / "unchanged build");
        require(rebuilt.original.map == created.game.original.map && rebuilt.original.volumes == created.game.original.volumes, "unchanged project differs");
        project_fixture::write(moved / "payloads/0000000.bin", {'n','e','w'});
        require(open_project(moved).game.resources[0].payload == Bytes({'n','e','w'}), "edits not loaded");
        build_project(moved, root / "edited build");
        require(open_sci0_game(root / "edited build").resources[0].payload == Bytes({'n','e','w'}), "edits not built");
        fails([&] { build_project(moved, root / "edited build"); }, "already exists");
        auto textProject = open_project(moved);
        const auto originalText = textProject.game.resources[6].payload;
        const auto newText = write_sci0_text({"Hello\nLarry", "", std::string("\x82\\\t", 3)});
        const auto table = export_sci0_text_table(newText);
        const auto largest = export_sci0_text_table(write_sci0_text({std::string(max_stored_payload - 1, 'a')}));
        require(prepare_project_text_edit(textProject, 6, largest).payload.size() == max_stored_payload,
                "largest storable text rejected");
        const auto oversized = export_sci0_text_table(write_sci0_text({std::string(max_stored_payload, 'a')}));
        fails([&] { prepare_project_text_edit(textProject, 6, oversized); }, "65531");
        fails([&] { write_sci0_text({std::string(65535, 'a')}); }, "65535");
        require(import_sci0_text_table(table) == newText, "escaped text round-trip differs");
        require(import_sci0_text_table("index\ttext\n").empty(), "empty text table failed");
        fails([&] { import_sci0_text_table("index\ttext\n1\twrong\n"); }, "indices");
        fails([&] { import_sci0_text_table("index\ttext\n0\tbad\\x00\n"); }, "NUL");
        fails([&] { import_sci0_text_table("index\ttext\n0\tbad\\q\n"); }, "escape");
        fails([&] { prepare_project_text_edit(textProject, 3, table); }, "text resource");
        auto edit = prepare_project_text_edit(textProject, 6, table);
        require(edit.destination == moved / "assets/0000006/strings.tsv" && edit.modified, "wrong text edit destination");
        { auto writes = edit.related_writes; writes.emplace_back(edit.destination, edit.contents); replace_files(writes); }
        fails([&] { prepare_project_text_edit(textProject, 6, table); }, "changed on disk");
        textProject = open_project(moved);
        require(textProject.game.resources[6].payload == newText, "text edit not loaded");
        build_project(moved, root / "text build");
        require(open_sci0_game(root / "text build").resources[6].payload == newText, "text edit not built");
        project_fixture::write(moved / "payloads/0000006.bin", newText);
        edit = prepare_project_text_edit(textProject, 6, table);
        require(edit.related_writes.empty(), "matching edits should not require a reset for an unchanged save");
        edit = prepare_project_text_edit(textProject, 6, export_sci0_text_table(originalText));
        require(edit.related_writes.size() == 1, "matching dual edits must reset the redundant binary when changed");
        require(!edit.modified, "restoring text still marked modified");
        { auto writes = edit.related_writes; writes.emplace_back(edit.destination, edit.contents); replace_files(writes); }
        project_fixture::write(moved / "payloads/0000006.bin", newText);
        textProject = open_project(moved);
        edit = prepare_project_text_edit(textProject, 6, export_sci0_text_table(originalText));
        require(edit.destination == moved / "payloads/0000006.bin", "binary edit route lost");
        { auto writes = edit.related_writes; writes.emplace_back(edit.destination, edit.contents); replace_files(writes); }
        fs::remove_all(moved / "assets/0000006"); // Older projects have payloads only.
        textProject = open_project(moved);
        edit = prepare_project_text_edit(textProject, 6, table);
        require(edit.destination == moved / "payloads/0000006.bin", "legacy text edit failed");
        { auto writes = edit.related_writes; writes.emplace_back(edit.destination, edit.contents); replace_files(writes); }
        require(open_project(moved).game.resources[6].payload == newText, "legacy text edit not loaded");
        project_fixture::write(moved / "manifest.tsv", {'b','a','d'});
        fails([&] { open_project(moved); }, "manifest");
        project_fixture::write(moved / "project.scimate", {'b','a','d'});
        fails([&] { open_project(moved); }, "descriptor");
        fs::remove_all(root);
        std::cout << "Project lifecycle tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
