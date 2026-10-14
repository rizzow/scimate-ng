// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>

namespace scimate {
enum class LogLevel { warning, error, info };
using LogSink = std::function<void(LogLevel, const std::string&, const std::string&)>;
// Sinks may be called from worker threads. An empty sink restores stderr logging.
void set_log_sink(LogSink sink);
// Logging must never replace the original failure, including when a sink fails.
void log_diagnostic(LogLevel level, const std::string& operation, const std::string& message) noexcept;
std::string exception_message(std::exception_ptr error);
[[noreturn]] inline void fail(const std::string& message) { throw std::runtime_error(message); }
inline void require(bool condition, const std::string& message) { if (!condition) fail(message); }
} // namespace scimate
