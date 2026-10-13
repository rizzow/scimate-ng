// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/diagnostics.hpp"
#include <array>
#include <string_view>
namespace scimate::asset_detail {
enum class Representation { raw, json, view, picture, midi, midi_pcm, midi_early, font, cursor, text, vocabulary };
inline constexpr std::array<std::string_view, 11> representation_names{
    "raw", "json", "view", "picture", "midi", "midi-pcm", "midi-early", "font", "cursor", "text", "vocab0"};
inline std::string_view representation_name(Representation value) {
    const auto index = static_cast<std::size_t>(value);
    scimate::require(index < representation_names.size(), "asset: unknown representation");
    return representation_names[index];
}
inline Representation parse_representation(std::string_view name) {
    for (std::size_t i = 0; i < representation_names.size(); ++i)
        if (representation_names[i] == name) return static_cast<Representation>(i);
    scimate::fail("asset manifest: unknown representation: " + std::string(name));
}
}
