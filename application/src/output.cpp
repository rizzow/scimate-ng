// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/output.hpp"
#include "scimate/diagnostics.hpp"
#include "files.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <limits>
#include <system_error>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace scimate {
Bytes read_binary_file(const std::filesystem::path& path, std::size_t limit) {
    return detail::read_file(path, limit);
}
namespace {
namespace fs = std::filesystem;
void cleanup(const fs::path& path) noexcept {
    try {
        std::error_code error;
        fs::remove_all(path, error);
        if (error) log_diagnostic(LogLevel::warning, "output cleanup", path.string() + ": " + error.message());
    } catch (...) { log_diagnostic(LogLevel::warning, "output cleanup", "temporary output could not be removed"); }
}
fs::path reserve_staging(const fs::path& parent) {
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        auto path = parent / (".scimate-output-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(attempt));
        if (fs::create_directory(path)) return path;
    }
    fail("cannot reserve temporary output directory in " + parent.string());
}
}
void write_output_directory(const fs::path& source, const fs::path& destination, bool force,
                            const std::function<void(const fs::path&)>& write) {
    if (!force) { write(destination); return; }
    const auto input = fs::weakly_canonical(source);
    const auto output = fs::weakly_canonical(destination);
    const auto mismatch = std::mismatch(output.begin(), output.end(), input.begin(), input.end());
    bool contains_source = mismatch.first == output.end();
    // Filesystem identity also handles case-insensitive paths and aliases.
    if (fs::exists(output)) {
        for (auto ancestor = input; !ancestor.empty();) {
            std::error_code error;
            if (fs::equivalent(output, ancestor, error)) { contains_source = true; break; }
            const auto parent = ancestor.parent_path();
            if (parent == ancestor) break;
            ancestor = parent;
        }
    }
    require(output != output.root_path() && !contains_source,
            "--force destination must not be the source directory or an ancestor of it");
    require(!fs::is_symlink(fs::symlink_status(destination)), "--force destination must not be a symbolic link");
    if (!fs::exists(destination)) { write(destination); return; }
    require(fs::is_directory(destination), "--force destination must be a directory");
    const auto staging = reserve_staging(output.parent_path());
    try {
        write(staging / "replacement");
        require(fs::is_directory(fs::symlink_status(staging / "replacement")),
                "output writer did not create a replacement directory");
    }
    catch (...) { cleanup(staging); throw; }
    // Both renames stay on the same filesystem. Never delete the old output
    // until the replacement has been installed successfully.
    std::error_code error;
    fs::rename(output, staging / "previous", error);
    if (error) fail("cannot preserve previous output: " + error.message() +
                    "; replacement retained at " + (staging / "replacement").string());
    fs::rename(staging / "replacement", output, error);
    if (error) {
        std::error_code rollback;
        fs::rename(staging / "previous", output, rollback);
        fail("cannot install replacement: " + error.message() +
             (rollback ? "; rollback failed: " + rollback.message() + "; previous output retained at " +
                         (staging / "previous").string() : "; previous output restored") +
             "; replacement retained at " + (staging / "replacement").string());
    }
    cleanup(staging);
}
void write_new_file(const fs::path& path, const Bytes& contents) {
#ifdef _WIN32
    const int descriptor = _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
#endif
    if (descriptor < 0) {
        const auto error = std::error_code(errno, std::generic_category());
        fail((error.value() == EEXIST ? "destination already exists: " : "cannot create output: ") +
             path.string() + ": " + error.message());
    }
    bool closed = false;
    try {
        std::size_t offset = 0;
        while (offset < contents.size()) {
            const auto count = std::min<std::size_t>(contents.size() - offset, std::numeric_limits<int>::max());
#ifdef _WIN32
            const auto written = _write(descriptor, contents.data() + offset, static_cast<unsigned>(count));
#else
            const auto written = ::write(descriptor, contents.data() + offset, count);
#endif
            if (written < 0 && errno == EINTR) continue;
            if (written < 0) fail("failed to write output: " + path.string() + ": " +
                                  std::error_code(errno, std::generic_category()).message());
            require(written != 0, "failed to write output: " + path.string() + ": no write progress");
            offset += static_cast<std::size_t>(written);
        }
        closed = true;
#ifdef _WIN32
        const auto result = _close(descriptor);
#else
        const auto result = ::close(descriptor);
#endif
        if (result != 0) fail("failed to close output: " + path.string() + ": " +
                              std::error_code(errno, std::generic_category()).message());
        require(detail::read_file(path, contents.size()) == contents, "saved output verification failed: " + path.string());
    } catch (...) {
        if (!closed) {
#ifdef _WIN32
            _close(descriptor);
#else
            ::close(descriptor);
#endif
        }
        cleanup(path);
        throw;
    }
}
void replace_files(const std::vector<std::pair<fs::path, Bytes>>& files) {
    struct Pending { fs::path target, staging; bool backed_up = false, installed = false; };
    std::vector<Pending> pending;
    std::vector<fs::path> targets;
    try {
        for (const auto& file : files) {
            const auto target = fs::weakly_canonical(file.first);
            require(std::find(targets.begin(), targets.end(), target) == targets.end(), "duplicate replacement target: " + target.string());
            targets.push_back(target);
            require(fs::is_regular_file(fs::symlink_status(file.first)), "replacement target must be a regular file: " + file.first.string());
            pending.push_back({target, reserve_staging(target.parent_path())});
            write_new_file(pending.back().staging / "replacement", file.second);
            fs::permissions(pending.back().staging / "replacement", fs::status(target).permissions());
        }
        for (auto& file : pending) {
            fs::rename(file.target, file.staging / "previous");
            file.backed_up = true;
            fs::rename(file.staging / "replacement", file.target);
            file.installed = true;
        }
    } catch (...) {
        const auto cause = std::current_exception();
        std::string recovery;
        for (auto i = pending.rbegin(); i != pending.rend(); ++i) {
            std::error_code error;
            if (i->installed) fs::remove(i->target, error);
            if (!error && i->backed_up) fs::rename(i->staging / "previous", i->target, error);
            if (error) {
                recovery += "; recovery required at " + i->staging.string() + ": " + error.message();
                log_diagnostic(LogLevel::error, "file replacement rollback", recovery);
            } else cleanup(i->staging);
        }
        if (!recovery.empty()) fail(exception_message(cause) + recovery);
        std::rethrow_exception(cause);
    }
    for (const auto& file : pending) cleanup(file.staging);
}
} // namespace scimate
