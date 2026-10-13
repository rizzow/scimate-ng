// SPDX-License-Identifier: GPL-2.0-or-later
// Independent codecs; format provenance is recorded in docs/provenance.md.
#include "structured_resource_internal.hpp"
namespace scimate::structured_detail {
Bytes read_names(ResourceId id, const Bytes &original, const Json &edited,
                 const Document &reference) {
  auto normalized = reference.root;
  auto &rows = normalized.at("records").array();
  const auto &input = edited.at("records").array();
  require(rows.size() == input.size(),
          "vocabulary JSON: name IDs/count are fixed");
  for (std::size_t i = 0; i < rows.size(); ++i) {
    rows[i].at("name") = input[i].at("name");
    if (!std::holds_alternative<std::nullptr_t>(rows[i].at("name").value))
      text_bytes(rows[i].at("name").string());
  }
  require(normalized == edited,
          "vocabulary JSON: changed identity/layout/unknown fields");
  if (normalized == reference.root)
    return original;
  Bytes out(2 + rows.size() * 2, 0);
  word_at(out, 0, script_detail::word(original, 0, original.size()));
  std::map<Bytes, unsigned> shared;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const auto &name = rows[i].at("name");
    if (std::holds_alternative<std::nullptr_t>(name.value))
      continue;
    auto bytes = text_bytes(name.string());
    auto found = shared.find(bytes);
    unsigned offset;
    if (found != shared.end())
      offset = found->second;
    else {
      offset = out.size();
      shared.emplace(bytes, offset);
      put(out, bytes.size(), 2);
      out.insert(out.end(), bytes.begin(), bytes.end());
    }
    word_at(out, 2 + i * 2, offset);
    asset_detail::payload_limit(out);
  }
  require(names(id, out).root == edited,
          "vocabulary JSON: reconstruction differs");
  return out;
}
Bytes edit(Document reference, const Bytes &original, const Json &input) {
  auto normalized = reference.root;
  auto &records = normalized.at("records").array();
  const auto &edits = input.at("records").array();
  require(records.size() == edits.size(),
          "resource JSON: fixed record count/IDs cannot change");
  Bytes out = original;
  for (const auto &binding : reference.bindings) {
    const auto &value = edits[binding.record].at(binding.field);
    const auto &previous = records[binding.record].at(binding.field);
    if (value == previous)
      continue;
    unsigned number = 0;
    if (binding.kind == Binding::text || binding.kind == Binding::fixed_text) {
      auto bytes = text_bytes(value.string());
      require(bytes.size() == binding.width &&
                  (binding.kind == Binding::fixed_text ||
                   std::find(bytes.begin(), bytes.end(), 0) == bytes.end()),
              "resource JSON: text must retain encoded length; NUL is only "
              "allowed in fixed text fields");
      std::copy(bytes.begin(), bytes.end(), out.begin() + binding.offset);
    } else {
      if (binding.kind == Binding::boolean ||
          binding.kind == Binding::inverted_boolean) {
        auto v = std::get_if<bool>(&value.value);
        require(v != nullptr, "patch JSON: expected boolean");
        number =
            binding.kind == Binding::boolean ? unsigned(*v) : unsigned(!*v);
      } else if (binding.kind == Binding::opcode) {
        bool found = false;
        for (unsigned op = 0; op < 128; ++op) {
          if (script_detail::opcode_name(op).empty() ||
              script_detail::opcode_name(op) != value.string())
            continue;
          auto old_op = original[binding.offset] >> 1;
          auto width = (original[binding.offset] & 1) ? 1u : 2u;
          require(script_detail::compatible_opcode(op, old_op, width),
                  "script JSON: opcode must retain operand layout and interpretation");
          number = (op << 1) | (original[binding.offset] & 1);
          found = true;
          break;
        }
        require(found, "script JSON: unknown mnemonic");
      } else if (binding.kind == Binding::said_operator) {
        bool found = false;
        for (const auto &op : said_names)
          if (op.second == value.string()) {
            number = op.first;
            found = true;
            break;
          }
        require(found, "script JSON: unknown said operator");
      } else if (binding.kind == Binding::target ||
                 binding.kind == Binding::relative_target) {
        require(value.string().size() == 12 &&
                    value.string().substr(0, 8) == "address_",
                "script JSON: invalid address label");
        auto suffix = value.string().substr(8);
        require(std::all_of(suffix.begin(), suffix.end(),
                            [](char c) {
                              return (c >= '0' && c <= '9') ||
                                     (c >= 'a' && c <= 'f');
                            }),
                "script JSON: labels use four lowercase hex digits");
        std::size_t consumed = 0;
        number = std::stoul(suffix, &consumed, 16);
        require(consumed == 4 && number <= 65535,
                "script JSON: invalid address");
        if (binding.kind == Binding::relative_target) {
          int delta = int(number) - int(binding.mask);
          if (delta > 32767)
            delta -= 65536;
          if (delta < -32768)
            delta += 65536;
          require(binding.width == 2 || (delta >= -128 && delta <= 127),
                  "script JSON: relative target exceeds operand width");
          number = unsigned(delta) & (binding.width == 1 ? 255 : 65535);
        }
      } else if (binding.kind == Binding::signed_integer)
        number = unsigned(value.number(binding.width == 1 ? -128 : -32768,
                                       binding.width == 1 ? 127 : 32767)) &
                 (binding.width == 1 ? 255 : 65535);
      else
        number = value.number(
            0,
            std::min<unsigned>(binding.mask, binding.width == 1 ? 255 : 65535));
      if (binding.width == 1) {
        if (binding.kind == Binding::integer)
          out[binding.offset] = (original[binding.offset] & ~binding.mask) |
                                (number & binding.mask);
        else
          out[binding.offset] = number;
      } else if (binding.kind == Binding::big_integer) {
        out[binding.offset] = number >> 8;
        out[binding.offset + 1] = number & 255;
      } else
        word_at(out, binding.offset, number);
    }
    records[binding.record].at(binding.field) = value;
  }
  require(normalized == input,
          "resource JSON: changed immutable identity/layout or unknown fields");
  return out;
}
}
