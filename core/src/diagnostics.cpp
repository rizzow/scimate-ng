// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/diagnostics.hpp"
#include <iostream>
#include <mutex>
#include <utility>

namespace scimate {
namespace {
std::mutex sink_mutex, output_mutex;
LogSink sink;
std::string one_line(const std::string& text) {
    std::string output;
    for (unsigned char c : text) {
        if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else if (c < 32 || c == 127) output += '?';
        else output += static_cast<char>(c);
    }
    return output;
}
void stderr_log(LogLevel level, const std::string& operation, const std::string& message) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cerr << "scimate: " << (level == LogLevel::warning ? "warning: " : "")
              << one_line(operation) << ": " << one_line(message) << '\n';
}
std::string describe(const std::exception& error, unsigned depth) {
    std::string message = error.what();
    if (depth >= 16) return message + " (nested exception limit reached)";
    try { std::rethrow_if_nested(error); }
    catch (const std::exception& cause) { message += ": " + describe(cause, depth + 1); }
    catch (...) { message += ": unknown non-standard exception"; }
    return message;
}
}
void set_log_sink(LogSink replacement) {
    std::lock_guard<std::mutex> lock(sink_mutex);
    sink = std::move(replacement);
}
void log_diagnostic(LogLevel level, const std::string& operation, const std::string& message) noexcept {
    try {
        LogSink selected;
        { std::lock_guard<std::mutex> lock(sink_mutex); selected = sink; }
        if (selected) {
            try { selected(level, operation, message); return; }
            catch (...) { /* Keep the original diagnostic when a custom sink fails. */ }
        }
        stderr_log(level, operation, message);
    } catch (...) { /* Diagnostics must not throw during cleanup or error reporting. */ }
}
std::string exception_message(std::exception_ptr error) {
    if (!error) return "unknown failure";
    try { std::rethrow_exception(error); }
    catch (const std::exception& cause) { return describe(cause, 0); }
    catch (...) { return "unknown non-standard exception"; }
}
} // namespace scimate
