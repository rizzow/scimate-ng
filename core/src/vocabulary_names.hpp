// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "asset_binary.hpp"
#include <optional>
namespace scimate::vocabulary_detail {
struct Names {
    unsigned header;
    bool maximum_id;
    std::vector<std::optional<Bytes>> entries;
};
inline Names read_names(const Bytes& bytes, bool selectors) {
    asset_detail::Reader reader(bytes);
    const auto header = reader.le(2);
    auto valid = [&](std::size_t count) {
        if (count > (bytes.size() - 2) / 2) return false;
        for (std::size_t i = 0; i < count; ++i) {
            asset_detail::Reader slot(bytes, 2 + 2 * i, 4 + 2 * i);
            const auto offset = slot.le(2);
            if (!offset) continue;
            if (offset < 2 + count * 2 || offset > bytes.size() || bytes.size() - offset < 2) return false;
            asset_detail::Reader name(bytes, offset, bytes.size());
            const auto length = name.le(2);
            if (length > name.remaining()) return false;
        }
        return true;
    };
    std::size_t count = std::size_t(header) + 1;
    if (!valid(count) && !selectors) count = header;
    scimate::require(valid(count), "vocabulary: invalid name offset table");
    Names result{header, count == std::size_t(header) + 1, {}};
    for (std::size_t i = 0; i < count; ++i) {
        asset_detail::Reader slot(bytes, 2 + 2 * i, 4 + 2 * i);
        const auto offset = slot.le(2);
        if (!offset) { result.entries.emplace_back(std::nullopt); continue; }
        asset_detail::Reader name(bytes, offset, bytes.size());
        const auto length = name.le(2);
        result.entries.emplace_back(name.take(length));
    }
    return result;
}
}
