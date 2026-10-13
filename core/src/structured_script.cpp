// SPDX-License-Identifier: GPL-2.0-or-later
// Independent codecs; format provenance is recorded in docs/provenance.md.
#include "structured_resource_internal.hpp"
namespace scimate::structured_detail {
Document script(ResourceId id, const Bytes &b) {
  auto parsed = script_detail::parse(b, false, false);
  auto d = base(id, "sci0-script");
  d.semantic = true;
  for (const auto &section : parsed.sections) {
    add(d, O{{"id", "section_" + script_detail::hex(section.start)},
             {"kind", "section"},
             {"offset", section.start},
             {"type", section.type},
             {"size", section.end - section.start}});
    if (section.type == 2) {
      for (const auto &ins : parsed.instructions)
        if (ins.start >= section.start + 4 && ins.end <= section.end) {
          add(d, O{{"id", label(ins.start)},
                   {"kind", "instruction"},
                   {"offset", ins.start},
                   {"operand_width", ins.width}});
          field(d, b, ins.start, 1, "mnemonic", Binding::opcode);
          auto widths = script_detail::operand_widths(ins.opcode, ins.width);
          std::size_t pos = ins.start + 1;
          for (std::size_t n = 0; n < widths.size(); ++n) {
            auto name = "operand_" + std::to_string(n);
            bool relative = n == 0 && script_detail::relative_operand(ins.opcode);
            if (relative) {
              d.root.at("records").array().back().object()[name] =
                  label(script_detail::target(ins));
              d.bindings.push_back(
                  {d.root.at("records").array().size() - 1, pos, widths[n],
                   name, Binding::relative_target, unsigned(ins.end)});
            } else
              field(d, b, pos, widths[n], name,
                    (ins.opcode == 26 || ins.opcode == 28)
                        ? Binding::signed_integer
                        : Binding::integer);
            pos += widths[n];
          }
        }
    } else if (section.type == 5) {
      Reader r(b, section.start + 4, section.end);
      while (r.remaining()) {
        auto offset = r.pos;
        Bytes text;
        while (r.remaining() && b[r.pos])
          text.push_back(r.byte());
        require(r.remaining() > 0, "script JSON: unterminated string");
        r.byte();
        add(d, O{{"id", label(offset)},
                 {"kind", "string"},
                 {"offset", offset},
                 {"capacity", text.size()}});
        field(d, b, offset, text.size(), "text", Binding::text);
      }
    } else if (section.type == 1 || section.type == 6) {
      for (const auto &obj : parsed.objects)
        if (obj.pointer >= section.start && obj.pointer < section.end) {
          add(d, O{{"id", label(obj.pointer)},
                   {"kind", obj.is_class ? "class" : "instance"},
                   {"offset", obj.pointer}});
          field(d, b, section.start + 6, 2, "reserved_header_word");
          field(d, b, section.start + 8, 2, "method_area_word");
          auto pos = obj.pointer;
          for (std::size_t i = 0; i < obj.values.size(); ++i, pos += 2) {
            add(d, O{{"id", label(pos)},
                     {"kind", "property"},
                     {"object", label(obj.pointer)},
                     {"index", i},
                     {"offset", pos}});
            field(d, b, pos, 2,
                  i == 0   ? "species"
                  : i == 1 ? "superclass"
                  : i == 2 ? "info"
                  : i == 3 ? "name_pointer"
                           : "value",
                  i == 3 ? Binding::target : Binding::integer);
          }
          if (obj.is_class)
            for (std::size_t i = 0; i < obj.selectors.size(); ++i, pos += 2) {
              add(d, O{{"id", label(pos)},
                       {"kind", "property-selector"},
                       {"index", i},
                       {"offset", pos}});
              field(d, b, pos, 2, "selector");
            }
          pos += 2;
          auto ids = pos;
          auto addresses = pos + obj.methods.size() * 2 + 2;
          for (std::size_t i = 0; i < obj.methods.size(); ++i) {
            add(d, O{{"id", label(ids + i * 2)},
                     {"kind", "method"},
                     {"object", label(obj.pointer)},
                     {"index", i},
                     {"offset", ids + i * 2}});
            field(d, b, ids + i * 2, 2, "selector");
            field(d, b, addresses + i * 2, 2, "entry", Binding::target);
          }
        }
    } else if (section.type == 7 || section.type == 8) {
      for (auto pos = section.start + 6; pos < section.end; pos += 2) {
        add(d, O{{"id", label(pos)},
                 {"kind", section.type == 7 ? "export" : "relocation"},
                 {"offset", pos},
                 {"index", (pos - section.start - 6) / 2}});
        field(d, b, pos, 2, "address", Binding::target);
      }
    } else if (section.type == 10)
      cells(d, b, section.start + 4, section.end, "local", 2);
    else if (section.type == 3 && (section.end - section.start - 4) % 4 == 0) {
      for (auto pos = section.start + 4; pos < section.end; pos += 4) {
        add(d, O{{"id", label(pos)}, {"kind", "synonym"}, {"offset", pos}});
        field(d, b, pos, 2, "source_group");
        field(d, b, pos + 2, 2, "replacement_group");
      }
    } else if (section.type == 4) {
      for (auto pos = section.start + 4; pos < section.end;) {
        add(d, O{{"id", label(pos)},
                 {"kind", b[pos] >= 0xf0 ? "said-operator" : "said-word-group"},
                 {"offset", pos}});
        if (b[pos] >= 0xf0) {
          field(d, b, pos, 1, "operator", Binding::said_operator);
          ++pos;
        } else {
          require(section.end - pos >= 2, "script JSON: truncated said group");
          field(d, b, pos, 2, "word_group", Binding::big_integer);
          pos += 2;
        }
      }
    } else
      cells(d, b, section.start + 4, section.end, "uninterpreted-section-byte");
  }
  return d;
}
}
