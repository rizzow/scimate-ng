// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/diagnostics.hpp"
#include <charconv>
#include <istream>
#include <string>
#include <vector>
namespace scimate::table_detail {
inline std::vector<std::string> row(std::istream& input, const char* context) {
    std::string line;
    require(bool(std::getline(input, line)), std::string(context) + ": missing row");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> result;
    std::size_t begin = 0;
    for (;;) {
        const auto end = line.find('\t', begin);
        result.push_back(line.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos) return result;
        begin = end + 1;
    }
}
template<class Integer> Integer integer(const std::string& text, Integer min, Integer max, const char* context) {
    Integer value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value >= min && value <= max,
            std::string(context) + ": integer outside allowed range");
    return value;
}
inline void end(std::istream& input, const char* context) {
    std::string extra;
    require(!std::getline(input, extra) && !input.bad(), std::string(context) + ": unexpected trailing row or read failure");
}
}
