// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include "scimate/diagnostics.hpp"
#include "scimate/format_constants.hpp"
#include <string>
#include "binary.hpp"
namespace scimate::asset_detail {
inline void require(bool value, const char *message) {
  scimate::require(value, message);
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
    require(n <= remaining(), "asset: truncated input");
    const auto value = binary_detail::read(bytes, pos, n, "asset: truncated input");
    pos += n; return value;
  }
  std::uint32_t be(unsigned n) {
    require(n <= remaining(), "asset: truncated input");
    const auto value = binary_detail::read(bytes, pos, n, "asset: truncated input", true);
    pos += n; return value;
  }
  Bytes take(std::size_t n) {
    require(n <= remaining(), "asset: truncated input");
    Bytes out(bytes.begin() + pos, bytes.begin() + pos + n);
    pos += n;
    return out;
  }
};
inline void put(Bytes &out, std::uint32_t value, unsigned n, bool big = false) {
  binary_detail::append(out, value, n, "asset: integer size limit exceeded", big);
}
inline void word_at(Bytes &out, std::size_t pos, unsigned value) {
  binary_detail::patch_word(out, pos, value, "asset: offset/size limit exceeded");
}
inline void payload_limit(const Bytes &b) {
  require(b.size() <= format::max_payload_bytes, "asset: payload exceeds 65535-byte limit");
}
} // namespace scimate::asset_detail
