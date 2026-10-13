// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace scimate::detail {
inline Bytes read_file(const std::filesystem::path& path, std::size_t limit) {
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)))
        throw std::runtime_error("expected regular file: " + path.string());
    const auto size = std::filesystem::file_size(path);
    if (size > limit) throw std::runtime_error("file exceeds size limit: " + path.string());
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open: " + path.string());
    Bytes bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream || stream.peek() != std::char_traits<char>::eof() || stream.bad())
        throw std::runtime_error("failed to read stable file: " + path.string());
    return bytes;
}

inline void write_file(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream stream;
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    try {
        stream.open(path, std::ios::binary);
        if (!bytes.empty()) stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();
    } catch (const std::ios_base::failure&) {
        std::throw_with_nested(std::runtime_error("failed to write: " + path.string()));
    }
}

inline void create_destination(const std::filesystem::path& destination) {
    if (!std::filesystem::create_directory(destination))
        throw std::runtime_error("destination already exists: " + destination.string());
}

} // namespace scimate::detail
