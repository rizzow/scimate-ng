// SPDX-License-Identifier: GPL-2.0-or-later
// Independent codecs; format provenance is recorded in docs/provenance.md.
#include "structured_resource_internal.hpp"
namespace scimate::structured_detail {
Document describe(ResourceId id, const Bytes &b) {
  require(b.size() <= format::max_payload_bytes && unsigned(id.type) <= format::last_sci0_resource_type && id.number <= format::max_sci0_resource_number,
          "resource JSON: invalid SCI0 payload/identity");
  try {
    if (id.type == ResourceType::script)
      return script(id, b);
    if (id.type == ResourceType::vocab) {
      auto d = vocabulary(id, b);
      if (d.semantic)
        return d;
    }
    if (id.type == ResourceType::patch && id.number == 3)
      return adlib(id, b);
    if (id.type == ResourceType::patch &&
        (id.number == 0 || id.number == 1 || id.number == 4)) {
      if (b.size() >= 1155 &&
          script_detail::word(b, 1153, b.size()) == b.size() - 1155)
        return gm(id, b);
      if (id.number != 4)
        return mt32(id, b);
    }
  } catch (const std::runtime_error &error) {
    auto d = base(id, "opaque-bytes");
    d.root.object()["diagnostic"] = error.what();
    cells(d, b, 0, b.size(), "uninterpreted-byte");
    return d;
  }
  auto d = base(id, id.type == ResourceType::memory ? "memory-cells"
                                                    : "opaque-bytes");
  cells(d, b, 0, b.size(),
        id.type == ResourceType::memory ? "memory-byte" : "uninterpreted-byte");
  return d;
}
}
namespace scimate {
using namespace structured_detail;
std::string export_resource_json(ResourceId id, const Bytes &payload) {
  return json_detail::dump_json(describe(id, payload).root);
}
std::string resource_json_schema(ResourceId id, const Bytes &payload) {
  return schema_for(id, payload, describe(id, payload));
}
ResourceJsonBundle export_resource_json_bundle(ResourceId id, const Bytes& payload) {
  const auto document = describe(id, payload);
  return {dump_json(document.root), schema_for(id, payload, document), document.semantic};
}
bool resource_json_is_semantic(ResourceId id, const Bytes &payload) {
  return describe(id, payload).semantic;
}
Bytes import_resource_json(ResourceId id, const Bytes &original,
                           const std::string &text) {
  auto reference = describe(id, original);
  auto input = json_detail::parse_json(text);
  if (!reference.semantic) {
    auto normalized = reference.root;
    const auto &rows = input.at("records").array();
    require(rows.size() <= 65535, "resource JSON: payload exceeds limit");
    Bytes out;
    for (std::size_t i = 0; i < rows.size(); ++i) {
      keys(rows[i], {"id", "kind", "offset", "value"});
      require(rows[i].at("id") == Json(label(i)) &&
                  rows[i].at("offset") == Json(i) &&
                  rows[i].at("kind") == Json(id.type == ResourceType::memory
                                                 ? "memory-byte"
                                                 : "uninterpreted-byte"),
              "resource JSON: byte IDs/offsets must be consecutive");
      out.push_back(rows[i].at("value").number(0, 255));
    }
    normalized.at("records") = input.at("records");
    require(normalized == input, "resource JSON: changed immutable metadata");
    return out;
  }
  if (reference.root.at("layout").string() == "vocabulary-names")
    return read_names(id, original, input, reference);
  auto out = edit(reference, original, input);
  if (out == original)
    return original;
  if (reference.root.at("layout").string() == "sci0-script")
    script_detail::parse(out);
  if (reference.semantic) {
    auto reopened = describe(id, out);
    require(reopened.root == input,
            "resource JSON: reconstructed semantic content differs");
  }
  return out;
}
} // namespace scimate
