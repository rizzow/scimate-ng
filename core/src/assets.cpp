// SPDX-License-Identifier: GPL-2.0-or-later
// Format adaptation from SCI Companion Font.cpp, Cursor.cpp, Text.cpp,
// Vocab000.cpp. Copyright (c) 2015 Philip Fortier. GPL version 2 or (at your
// option) any later version; distributed without any warranty. See COPYING.
#include "scimate/assets.hpp"
#include "asset_binary.hpp"
#include <algorithm>
namespace scimate {
using namespace asset_detail;
Font read_sci0_font(const Bytes &payload) {
  payload_limit(payload);
  Reader r(payload);
  Font f;
  f.reserved = r.le(2);
  auto count = r.le(2);
  f.line_height = r.le(2);
  require(count <= 256 && f.line_height > 0 && f.line_height <= 255,
          "font: invalid count/line height");
  require(count * 2 <= r.remaining(), "font: truncated offset table");
  for (unsigned i = 0; i < count; ++i) {
    auto offset = r.le(2);
    require(offset >= 6 + count * 2 && offset + 2 <= payload.size(),
            "font: invalid glyph offset");
    Reader glyph(payload, offset, payload.size());
    ViewCel cel;
    cel.width = glyph.byte();
    cel.height = glyph.byte();
    cel.transparent = 15;
    require(cel.height <= 200, "font: glyph height exceeds 200");
    auto bits = glyph.take(((cel.width + 7) / 8) * cel.height);
    for (unsigned y = 0; y < cel.height; ++y)
      for (unsigned x = 0; x < cel.width; ++x)
        cel.pixels.push_back(
            (bits[y * ((cel.width + 7) / 8) + x / 8] & (128 >> (x % 8))) ? 0
                                                                         : 15);
    f.glyphs.push_back(std::move(cel));
  }
  f.original = payload;
  return f;
}
Bytes write_sci0_font(const Font &f) {
  require(f.glyphs.size() <= 256 && f.line_height > 0 && f.line_height <= 255,
          "font: invalid count/line height");
  for (const auto &cel : f.glyphs) {
    require(cel.width <= 255 && cel.height <= 200 &&
                cel.pixels.size() == std::size_t(cel.width) * cel.height,
            "font: invalid glyph dimensions");
    for (auto pixel : cel.pixels)
      require(pixel == 0 || pixel == 15, "font: glyphs must be black or white");
  }
  if (!f.original.empty()) {
    auto old = read_sci0_font(f.original);
    if (old.reserved == f.reserved && old.line_height == f.line_height &&
        old.glyphs == f.glyphs)
      return f.original;
  }
  Bytes out;
  put(out, f.reserved, 2);
  put(out, f.glyphs.size(), 2);
  put(out, f.line_height, 2);
  out.resize(6 + f.glyphs.size() * 2);
  for (std::size_t i = 0; i < f.glyphs.size(); ++i) {
    word_at(out, 6 + i * 2, out.size());
    const auto &cel = f.glyphs[i];
    out.push_back(cel.width);
    out.push_back(cel.height);
    for (unsigned y = 0; y < cel.height; ++y)
      for (unsigned x = 0; x < cel.width; x += 8) {
        unsigned bits = 0;
        for (unsigned k = 0; k < 8 && x + k < cel.width; ++k)
          if (cel.pixels[y * cel.width + x + k] == 0)
            bits |= 128 >> k;
        out.push_back(bits);
      }
    payload_limit(out);
  }
  require(read_sci0_font(out).glyphs == f.glyphs,
          "font: serialization verification failed");
  return out;
}
Cursor read_sci0_cursor(const Bytes &payload) {
  require(payload.size() == 68, "cursor: SCI0 cursor must be 68 bytes");
  Reader r(payload);
  r.le(2);
  auto hot = r.le(2);
  Cursor c;
  c.hotspot = (hot & 255) ? 8 : 0;
  c.image.width = 16;
  c.image.height = 16;
  c.image.transparent = 3;
  c.image.pixels.resize(256);
  unsigned mask[16];
  for (auto &row : mask)
    row = r.le(2);
  for (int y = 0; y < 16; ++y) {
    auto bits = r.le(2);
    for (int x = 0; x < 16; ++x)
      c.image.pixels[y * 16 + x] = (bits & (1 << (15 - x)))      ? 15
                                   : (mask[y] & (1 << (15 - x))) ? 3
                                                                 : 0;
  }
  c.original = payload;
  return c;
}
Bytes write_sci0_cursor(const Cursor &c) {
  require((c.hotspot == 0 || c.hotspot == 8) && c.image.width == 16 &&
              c.image.height == 16 && c.image.pixels.size() == 256,
          "cursor: invalid dimensions/hotspot");
  for (auto p : c.image.pixels)
    require(p == 0 || p == 15 || p == 3,
            "cursor: only black, white and transparency are supported");
  if (!c.original.empty()) {
    auto old = read_sci0_cursor(c.original);
    if (old.hotspot == c.hotspot && old.image == c.image)
      return c.original;
  }
  Bytes out(68, 0);
  word_at(out, 2, c.hotspot == 8 ? 255 : 0);
  for (int y = 0; y < 16; ++y) {
    unsigned mask = 0, bits = 0;
    for (int x = 0; x < 16; ++x) {
      auto p = c.image.pixels[y * 16 + x];
      if (p == 3)
        mask |= 1 << (15 - x);
      if (p == 15)
        bits |= 1 << (15 - x);
    }
    word_at(out, 4 + y * 2, mask);
    word_at(out, 36 + y * 2, bits);
  }
  require(read_sci0_cursor(out).image == c.image,
          "cursor: serialization verification failed");
  return out;
}
std::vector<std::string> read_sci0_text(const Bytes &payload) {
  payload_limit(payload);
  std::vector<std::string> strings;
  std::string current;
  for (auto b : payload) {
    if (!b) {
      strings.push_back(current);
      current.clear();
    } else
      current.push_back(char(b));
  }
  require(payload.empty() || payload.back() == 0,
          "text: missing NUL terminator");
  return strings;
}
Bytes write_sci0_text(const std::vector<std::string> &strings) {
  Bytes out;
  for (const auto &s : strings) {
    require(s.find('\0') == std::string::npos,
            "text: embedded NUL is unsupported");
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
    payload_limit(out);
  }
  return out;
}
bool operator==(const Word &a, const Word &b) {
  return a.text == b.text && a.word_class == b.word_class && a.group == b.group;
}
std::vector<Word> read_sci0_vocabulary(const Bytes &payload) {
  payload_limit(payload);
  Reader r(payload);
  r.take(52);
  std::vector<Word> words;
  std::string previous;
  while (r.remaining()) {
    auto prefix = r.byte();
    require(prefix <= previous.size(), "vocab: invalid shared prefix");
    Word w;
    w.text = previous.substr(0, prefix);
    for (;;) {
      auto b = r.byte();
      require((b & 127) != 0 && w.text.size() < 255, "vocab: invalid word");
      w.text.push_back(char(b & 127));
      if (b & 128)
        break;
    }
    auto info = r.be(3);
    w.group = info & 4095;
    w.word_class = info >> 12;
    require(words.empty() || previous <= w.text, "vocab: words must be sorted");
    previous = w.text;
    words.push_back(std::move(w));
  }
  return words;
}
Bytes write_sci0_vocabulary(const std::vector<Word> &words) {
  Bytes out(52, 0);
  std::string previous;
  for (const auto &w : words) {
    require(!w.text.empty() && w.text.size() <= 255 && w.word_class <= 4095 &&
                w.group <= 4095,
            "vocab: invalid word/class/group");
    for (unsigned char b : w.text)
      require(b > 0 && b < 128, "vocab: words must be 7-bit bytes");
    require(previous <= w.text, "vocab: words must be sorted");
    if (w.text[0] >= 'a' && w.text[0] <= 'z') {
      auto offset = (w.text[0] - 'a') * 2;
      if (out[offset] == 0 && out[offset + 1] == 0)
        word_at(out, offset, out.size());
    }
    std::size_t prefix = 0;
    while (prefix < std::min(previous.size(), w.text.size()) &&
           previous[prefix] == w.text[prefix])
      ++prefix;
    // Duplicate words need a final character in SCI0's high-bit-terminated
    // layout.
    if (prefix == w.text.size())
      --prefix;
    out.push_back(prefix);
    out.insert(out.end(), w.text.begin() + prefix, w.text.end());
    out.back() |= 128;
    put(out, (std::uint32_t(w.word_class) << 12) | w.group, 3, true);
    payload_limit(out);
    previous = w.text;
  }
  require(read_sci0_vocabulary(out) == words,
          "vocab: serialization verification failed");
  return out;
}
} // namespace scimate
