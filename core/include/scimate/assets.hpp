// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/view.hpp"
#include <string>
namespace scimate {
struct Font {
  std::uint16_t reserved = 0, line_height = 0;
  std::vector<ViewCel> glyphs;
  Bytes original;
};
struct Cursor {
  ViewCel image;
  int hotspot = 0;
  Bytes original;
};
struct Word {
  std::string text;
  std::uint16_t word_class = 0, group = 0;
};
bool operator==(const Word &a, const Word &b);
Font read_sci0_font(const Bytes &payload);
Bytes write_sci0_font(const Font &font);
Cursor read_sci0_cursor(const Bytes &payload);
Bytes write_sci0_cursor(const Cursor &cursor);
std::vector<std::string> read_sci0_text(const Bytes &payload);
Bytes write_sci0_text(const std::vector<std::string> &strings);
std::vector<Word> read_sci0_vocabulary(const Bytes &payload);
Bytes write_sci0_vocabulary(const std::vector<Word> &words);
} // namespace scimate
