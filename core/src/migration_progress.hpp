// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/progress.hpp"
#include "scimate/diagnostics.hpp"
#include <chrono>
#include <type_traits>
namespace scimate::migration_detail {
inline void migration_progress(const ProgressCallback& progress, const OperationProgress& value) noexcept {
    if (!progress) return;
    try { progress(value); }
    catch (...) {
        try { log_diagnostic(LogLevel::warning, "Migration progress", exception_message(std::current_exception())); }
        catch (...) { /* Reporting must never interrupt or hide conversion errors. */ }
    }
}
template<class Action> auto migration_stage(const ProgressCallback& progress, const std::string& name, Action action)
    -> decltype(action()) {
    using Result = decltype(action());
    const auto started = std::chrono::steady_clock::now();
    if (progress) migration_progress(progress, {name, 0, 1, "Starting"});
    const auto complete = [&](const char* status) noexcept {
        if (!progress) return;
        try {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        migration_progress(progress, {name, 1, 1, std::string(status) + " in " + std::to_string(elapsed) + " ms"});
        } catch (...) { /* Preserve the conversion's result or original exception. */ }
    };
    try {
        if constexpr (std::is_void_v<Result>) { action(); complete("Completed"); }
        else { Result result = action(); complete("Completed"); return result; }
    } catch (...) { complete("Failed"); throw; }
}
}
