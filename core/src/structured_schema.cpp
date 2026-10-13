// SPDX-License-Identifier: GPL-2.0-or-later
// Independent codecs; format provenance is recorded in docs/provenance.md.
#include "structured_resource_internal.hpp"
namespace scimate::structured_detail {
std::string schema_for(ResourceId id, const Bytes &payload, const Document& document) {
  const auto &records = document.root.at("records").array();
  std::map<std::pair<std::size_t, std::string>, Binding> bindings;
  for (const auto &binding : document.bindings)
    bindings.emplace(std::make_pair(binding.record, binding.field), binding);
  Json::Object shapes;
  for (std::size_t i = 0; i < records.size(); ++i) {
    O properties;
    A required;
    for (const auto &field : records[i].object()) {
      required.emplace_back(field.first);
      Json schema;
      auto binding = bindings.find({i, field.first});
      if (binding != bindings.end()) {
        const auto &b = binding->second;
        if (b.kind == Binding::integer || b.kind == Binding::big_integer ||
            b.kind == Binding::signed_integer) {
          auto min = b.kind == Binding::signed_integer
                         ? (b.width == 1 ? -128 : -32768)
                         : 0;
          auto max =
              b.kind == Binding::signed_integer
                  ? (b.width == 1 ? 127 : 32767)
                  : int(std::min<unsigned>(b.mask, b.width == 1 ? 255 : 65535));
          schema = O{{"type", "integer"}, {"minimum", min}, {"maximum", max}};
        } else if (b.kind == Binding::boolean ||
                   b.kind == Binding::inverted_boolean)
          schema = O{{"type", "boolean"}};
        else if (b.kind == Binding::text || b.kind == Binding::fixed_text)
          schema = O{{"type", "string"},
                     {"minLength", b.width},
                     {"maxLength", b.width},
                     {"description",
                      "CP437 text; encoded length and addresses stay fixed."}};
        else if (b.kind == Binding::opcode) {
          A choices;
          auto old = payload[b.offset] >> 1;
          auto width = (payload[b.offset] & 1) ? 1u : 2u;
          for (unsigned op = 0; op < 128; ++op)
            if (script_detail::compatible_opcode(op, old, width))
              choices.emplace_back(script_detail::opcode_name(op));
          schema = O{{"type", "string"}, {"enum", choices}};
        } else if (b.kind == Binding::said_operator) {
          A ops;
          for (const auto &op : said_names)
            ops.emplace_back(op.second);
          schema = O{{"type", "string"}, {"enum", ops}};
        } else
          schema = O{{"type", "string"}, {"pattern", "^address_[0-9a-f]{4}$"}};
      } else if (field.first == "name" &&
                 document.root.at("layout").string() == "vocabulary-names")
        schema = O{{"type", A{"string", "null"}},
                   {"description",
                    "Editable CP437 name. Null means no name at this ID."}};
      else if (field.first == "id" || field.first == "offset" ||
               field.first == "index" || field.first == "object" ||
               field.first == "instrument")
        schema =
            std::holds_alternative<std::string>(field.second.value)
                ? Json(O{{"type", "string"}})
                : Json(O{
                      {"type", "integer"}, {"minimum", 0}, {"maximum", 65535}});
      else
        schema = O{{"const", field.second}};
      properties.emplace(field.first, std::move(schema));
    }
    Json shape = O{{"type", "object"},
                   {"properties", properties},
                   {"required", required},
                   {"additionalProperties", false}};
    shapes.emplace(dump_json(shape), std::move(shape));
  }
  A choices;
  for (auto &shape : shapes)
    choices.push_back(std::move(shape.second));
  O properties;
  A required;
  const bool byte_level = !document.semantic;
  if (byte_level && choices.empty()) {
    choices.emplace_back(O{
        {"type", "object"},
        {"required", A{"id", "kind", "offset", "value"}},
        {"properties",
         O{{"id", O{{"type", "string"}, {"pattern", "^address_[0-9a-f]{4}$"}}},
           {"kind", O{{"const", id.type == ResourceType::memory
                                    ? "memory-byte"
                                    : "uninterpreted-byte"}}},
           {"offset",
            O{{"type", "integer"}, {"minimum", 0}, {"maximum", 65534}}},
           {"value",
            O{{"type", "integer"}, {"minimum", 0}, {"maximum", 255}}}}},
        {"additionalProperties", false}});
  }
  for (const auto &field : document.root.object()) {
    required.emplace_back(field.first);
    properties[field.first] =
        field.first == "records"
            ? Json(O{{"type", "array"},
                     {"minItems", byte_level ? std::size_t(0) : records.size()},
                     {"maxItems",
                      byte_level ? std::size_t(65535) : records.size()},
                     {"items", choices.empty() ? Json(false)
                                               : Json(O{{"anyOf", choices}})}})
            : Json(O{{"const", field.second}});
  }
  return dump_json(O{
      {"$schema", "https://json-schema.org/draft/2020-12/schema"},
      {"title", "SCIMate " + document.root.at("layout").string() + " resource"},
      {"description",
       "Generated for this resource. Import additionally checks immutable IDs, "
       "field interpretations, encoded text and script targets."},
      {"type", "object"},
      {"properties", properties},
      {"required", required},
      {"additionalProperties", false}});
}
}
