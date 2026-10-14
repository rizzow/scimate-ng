// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "parser_candidates.hpp"

namespace scimate::parser_detail {
namespace tree_field {
inline constexpr unsigned major = 0, minor = 1, terminal = 2, word = 3, child = 4, sibling = 5, words = 6;
}
inline constexpr unsigned tree_node_limit = 500, tree_root_index = 1, tree_header_words = 1;
}
