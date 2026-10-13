// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/structured_resource.hpp"
#include "asset_binary.hpp"
#include "json.hpp"
#include "resource_charset.hpp"
#include "script_internal.hpp"
#include "vocabulary_names.hpp"
#include <algorithm>
namespace scimate::structured_detail {
using namespace json_detail;
using asset_detail::put;
using asset_detail::Reader;
using asset_detail::require;
using asset_detail::word_at;
using A = Json::Array;
using O = Json::Object;
struct Binding {
  std::size_t record, offset, width;
  std::string field;
  enum Kind {
    integer,
    signed_integer,
    boolean,
    inverted_boolean,
    text,
    fixed_text,
    opcode,
    target,
    relative_target,
    big_integer,
    said_operator
  } kind = integer;
  unsigned mask = 65535;
};
struct Document {
  Json root;
  std::vector<Binding> bindings;
  bool semantic = false;
};
extern const std::map<unsigned, std::string> said_names;
std::string label(std::size_t offset);
Document base(ResourceId id, const std::string& layout);
void add(Document& d, Json record);
void field(Document& d, const Bytes& bytes, std::size_t offset, unsigned width,
           const std::string& name, Binding::Kind kind = Binding::integer, unsigned mask = 65535);
void cells(Document& d, const Bytes& bytes, std::size_t begin, std::size_t end,
           const std::string& kind, unsigned width = 1);
Document script(ResourceId id, const Bytes& bytes);
Document names(ResourceId id, const Bytes& bytes);
Document vocabulary(ResourceId id, const Bytes& bytes);
Document adlib(ResourceId id, const Bytes& bytes);
Document mt32(ResourceId id, const Bytes& bytes);
Document gm(ResourceId id, const Bytes& bytes);
Document describe(ResourceId id, const Bytes& bytes);
Bytes read_names(ResourceId id, const Bytes& original, const Json& edited, const Document& reference);
Bytes edit(Document reference, const Bytes& original, const Json& input);
std::string schema_for(ResourceId id, const Bytes& payload, const Document& document);
}
