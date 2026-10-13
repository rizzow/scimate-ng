// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <string>
namespace scimate::asset_detail {
inline void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
struct Reader {
  const Bytes &bytes;
  std::size_t pos = 0, end;
  explicit Reader(const Bytes &b) : bytes(b), end(b.size()) {}
  Reader(const Bytes &b, std::size_t begin, std::size_t limit)
      : bytes(b), pos(begin), end(limit) {
    require(begin <= limit && limit <= b.size(), "asset: invalid bounds");
  }
  std::size_t remaining() const { return end - pos; }
  std::uint8_t byte() {
    require(pos < end, "asset: truncated input");
    return bytes[pos++];
  }
  std::uint32_t le(unsigned n) {
    std::uint32_t v = 0;
    for (unsigned i = 0; i < n; ++i)
      v |= std::uint32_t(byte()) << (i * 8);
    return v;
  }
  std::uint32_t be(unsigned n) {
    std::uint32_t v = 0;
    while (n--)
      v = (v << 8) | byte();
    return v;
  }
  Bytes take(std::size_t n) {
    require(n <= remaining(), "asset: truncated input");
    Bytes out(bytes.begin() + pos, bytes.begin() + pos + n);
    pos += n;
    return out;
  }
};
inline void put(Bytes &out, std::uint32_t value, unsigned n, bool big = false) {
  for (unsigned i = 0; i < n; ++i)
    out.push_back(value >> ((big ? n - 1 - i : i) * 8));
}
inline void word_at(Bytes &out, std::size_t pos, unsigned value) {
  require(pos + 2 <= out.size() && value <= 65535,
          "asset: offset/size limit exceeded");
  out[pos] = value & 255;
  out[pos + 1] = value >> 8;
}
inline void payload_limit(const Bytes &b) {
  require(b.size() <= 65535, "asset: payload exceeds 65535-byte limit");
}
} // namespace scimate::asset_detail
