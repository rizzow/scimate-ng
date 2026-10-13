// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/output.hpp"
#include "scimate/asset_io.hpp"
#include "scimate/diagnostics.hpp"
#include "scimate/structured_resource.hpp"
#include <chrono>
#include <iostream>
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Action> void fails(Action action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("expected operation to fail");
}
}
int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    using namespace scimate;
    try {
        check(argc == 2, "expected fixture root");
        const auto root = fs::path(argv[1]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(root / "source");
        fs::create_directory(root / "target");
        write_new_file(root / "target/old", {1});
        fails([&] { write_new_file(root / "target/old", {2}); });
        check(read_binary_file(root / "target/old", 1) == Bytes{1}, "exclusive create overwrote existing file");
        fails([&] { write_output_directory(root / "source", root / "target", true, [&](const auto& target) {
            fs::create_directory(target); write_new_file(target / "new", {2}); throw std::runtime_error("generation failed");
        }); });
        check(read_binary_file(root / "target/old", 1) == Bytes{1}, "failed generation removed original");
        fails([&] { write_output_directory(root / "source", root / "target", true, [](const auto&) {}); });
        check(fs::exists(root / "target/old"), "invalid writer removed original");
        fails([&] { write_output_directory(root / "source", root, true, [](const auto&) {}); });
        write_output_directory(root / "source", root / "target", true, [&](const auto& target) {
            fs::create_directory(target); write_new_file(target / "new", {3});
        });
        check(!fs::exists(root / "target/old") && read_binary_file(root / "target/new", 1) == Bytes{3}, "replacement retained old contents");
        write_new_file(root / "second", {4});
        fails([&] { replace_files({{root / "second", {5}}, {root / "missing", {6}}}); });
        check(read_binary_file(root / "second", 1) == Bytes{4}, "failed preparation changed an existing file");
        replace_files({{root / "second", {5}}, {root / "target/new", {6}}});
        check(read_binary_file(root / "second", 1) == Bytes{5} && read_binary_file(root / "target/new", 1) == Bytes{6}, "related replacements failed");
        const auto bundle = export_resource_json_bundle({ResourceType::memory, 0}, {1,2});
        check(!bundle.semantic && import_resource_json({ResourceType::memory, 0}, {1,2}, bundle.json) == Bytes({1,2}), "JSON bundle did not round-trip");
        Bytes ambiguous(34, 0); ambiguous[17] = ambiguous[33] = 0xfc;
        const auto exported = export_sci0_asset({ResourceType::sound, 0}, ambiguous, root / "ambiguous");
        check(exported.conversion_error && !exported.editable, "ambiguous sound selected a guessed format");
        check(import_sci0_asset(root / "ambiguous") == ambiguous, "ambiguous sound bytes changed");
        std::string logged;
        set_log_sink([&](LogLevel, const std::string&, const std::string& message) { logged = message; });
        log_diagnostic(LogLevel::error, "fixture", "expected diagnostic");
        set_log_sink({});
        check(logged == "expected diagnostic", "custom diagnostic sink missed error");
        std::exception_ptr nested;
        try { try { throw std::runtime_error("cause"); } catch (...) { std::throw_with_nested(std::runtime_error("context")); } }
        catch (...) { nested = std::current_exception(); }
        check(exception_message(nested) == "context: cause", "nested failure lost context");
        fs::remove_all(root);
        return 0;
    } catch (...) {
        log_diagnostic(LogLevel::error, "output tests", exception_message(std::current_exception()));
        return 1;
    }
}
