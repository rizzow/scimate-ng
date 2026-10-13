// SPDX-License-Identifier: GPL-2.0-or-later
// Independent codecs; format provenance is recorded in docs/provenance.md.
#include "structured_resource_internal.hpp"
namespace scimate::structured_detail {
Document names(ResourceId id, const Bytes &b) {
  const auto table = vocabulary_detail::read_names(b, id.number == 997);
  auto d = base(id, "vocabulary-names");
  d.semantic = true;
  d.root.object()["count_convention"] = table.maximum_id ? "maximum-id" : "count";
  for (std::size_t i = 0; i < table.entries.size(); ++i)
    add(d, O{{"id", i}, {"kind", "name"},
             {"name", table.entries[i] ? Json(byte_text(*table.entries[i])) : Json{}}});
  return d;
}
Document vocabulary(ResourceId id, const Bytes &b) {
  if (id.number == 995 || id.number == 997 || id.number == 998 ||
      id.number == 999)
    return names(id, b);
  if (id.number == 996) {
    require(b.size() % 4 == 0, "vocabulary JSON: invalid class table length");
    auto d = base(id, "vocabulary-classes");
    d.semantic = true;
    for (std::size_t pos = 0; pos < b.size(); pos += 4) {
      add(d, O{{"id", pos / 4}, {"kind", "class-location"}, {"offset", pos}});
      field(d, b, pos, 2, "reserved");
      field(d, b, pos + 2, 2, "script");
    }
    return d;
  }
  if (id.number == 900 || id.number == 910) {
    require(b.size() % 20 == 0, "vocabulary JSON: invalid grammar length");
    auto d = base(id, "vocabulary-grammar");
    d.semantic = true;
    for (std::size_t pos = 0; pos < b.size(); pos += 20) {
      add(d, O{{"id", pos / 20}, {"kind", "grammar-rule"}, {"offset", pos}});
      field(d, b, pos, 2, "rule_id");
      for (int i = 0; i < 9; ++i)
        field(d, b, pos + 2 + i * 2, 2, "symbol_" + std::to_string(i));
    }
    return d;
  }
  if (id.number == 994) {
    auto d = base(id, "vocabulary-property-offsets");
    d.semantic = true;
    require(b.size() % 2 == 0, "vocabulary JSON: odd property offset table");
    cells(d, b, 0, b.size(), "property-offset", 2);
    return d;
  }
  if (id.number == 901 || id.number == 911) {
    auto d = base(id, "vocabulary-suffixes");
    d.semantic = true;
    Reader r(b);
    require(r.remaining() > 0, "vocabulary JSON: empty suffix table");
    r.byte();
    auto string_field = [&](const char *name) {
      auto pos = r.pos;
      while (r.remaining() && b[r.pos])
        r.byte();
      require(r.remaining() > 0, "vocabulary JSON: unterminated suffix");
      field(d, b, pos, r.pos - pos, name, Binding::text);
      r.byte();
    };
    std::size_t index = 0;
    while (r.remaining() > 1 && b[r.pos + 1] != 255) {
      add(d, O{{"id", index++}, {"kind", "suffix-rule"}, {"offset", r.pos}});
      string_field("alternative_suffix");
      auto pos = r.pos;
      r.le(2);
      field(d, b, pos, 2, "result_class", Binding::big_integer);
      r.byte();
      string_field("word_suffix");
      pos = r.pos;
      r.le(2);
      field(d, b, pos, 2, "class_mask", Binding::big_integer);
      if (r.remaining())
        r.byte();
    }
    return d;
  }
  if (id.number == 913) {
    auto d = base(id, "vocabulary-alternate-inputs");
    d.semantic = true;
    Reader r(b);
    std::size_t index = 0;
    while (r.remaining() && b[r.pos]) {
      add(d,
          O{{"id", index++}, {"kind", "alternate-input"}, {"offset", r.pos}});
      for (auto name : {"input", "replacement"}) {
        auto pos = r.pos;
        while (r.remaining() && b[r.pos])
          r.byte();
        require(r.remaining() > 0,
                "vocabulary JSON: unterminated alternate input");
        field(d, b, pos, r.pos - pos, name, Binding::text);
        r.byte();
      }
    }
    return d;
  }
  return base(id, "opaque-bytes");
}
}
