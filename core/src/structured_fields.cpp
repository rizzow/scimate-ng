// SPDX-License-Identifier: GPL-2.0-or-later
// Independent codecs; format provenance is recorded in docs/provenance.md.
#include "structured_resource_internal.hpp"
namespace scimate::structured_detail {
std::string label(std::size_t offset) {
  return "address_" + script_detail::hex(offset);
}
const std::map<unsigned, std::string> said_names{
    {0xf0, ","}, {0xf1, "&"}, {0xf2, "/"},  {0xf3, "("},
    {0xf4, ")"}, {0xf5, "["}, {0xf6, "]"},  {0xf7, "#"},
    {0xf8, "<"}, {0xf9, ">"}, {0xff, "end"}};
Document base(ResourceId id, const std::string &layout) {
  return {O{{"$schema", "resource.schema.json"},
            {"format", "scimate-resource"},
            {"version", 1},
            {"encoding", "cp437"},
            {"resource", O{{"type", std::string(resource_type_name(id.type))},
                           {"number", unsigned(id.number)}}},
            {"layout", layout},
            {"records", A{}}},
          {},
          false};
}
void add(Document &d, Json record) {
  d.root.at("records").array().push_back(std::move(record));
}
void field(Document &d, const Bytes &bytes, std::size_t offset, unsigned width,
           const std::string &name, Binding::Kind kind,
           unsigned mask) {
  require(offset <= bytes.size() && width <= bytes.size() - offset,
          "resource JSON: field outside payload");
  auto &records = d.root.at("records").array();
  require(!records.empty(), "resource JSON: missing record");
  Reader r(bytes, offset, offset + width);
  unsigned value =
      kind != Binding::text && kind != Binding::fixed_text && width <= 2
          ? (kind == Binding::big_integer ? r.be(width) : r.le(width))
          : 0;
  Json item;
  if (kind == Binding::text || kind == Binding::fixed_text)
    item = byte_text(r.take(width));
  else if (kind == Binding::opcode)
    item = script_detail::opcode_name(value >> 1);
  else if (kind == Binding::said_operator) {
    auto found = said_names.find(value);
    require(found != said_names.end(), "script JSON: unknown said operator");
    item = found->second;
  } else if (kind == Binding::target)
    item = label(value);
  else if (kind == Binding::boolean || kind == Binding::inverted_boolean)
    item = kind == Binding::boolean ? value != 0 : value == 0;
  else if (kind == Binding::signed_integer)
    item = script_detail::signed_value(value, width);
  else
    item = value & mask;
  records.back().object()[name] = item;
  d.bindings.push_back({records.size() - 1, offset, width, name, kind, mask});
}
void cells(Document &d, const Bytes &b, std::size_t begin, std::size_t end,
           const std::string &kind, unsigned width) {
  require(width >= 1 && width <= 4, "resource JSON: invalid cell width");
  require(begin <= end && end <= b.size(), "resource JSON: invalid range");
  for (auto pos = begin; pos < end;) {
    auto size = std::min<std::size_t>(width, end - pos);
    add(d, O{{"id", label(pos)}, {"kind", kind}, {"offset", pos}});
    field(d, b, pos, size, "value");
    pos += size;
  }
}
}
