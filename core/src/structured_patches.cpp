// SPDX-License-Identifier: GPL-2.0-or-later
// Independent codecs; format provenance is recorded in docs/provenance.md.
#include "structured_resource_internal.hpp"
namespace scimate::structured_detail {
Document adlib(ResourceId id, const Bytes &b) {
  require(b.size() == 1344 || b.size() == 2690,
          "patch JSON: unsupported AdLib bank size");
  auto d = base(id, "adlib-bank");
  d.semantic = true;
  unsigned count = b.size() == 1344 ? 48 : 96;
  const char *names[] = {"keyboard_scale_level",
                         "frequency_multiplier",
                         "reserved",
                         "attack_rate",
                         "sustain_level",
                         "envelope_type",
                         "decay_rate",
                         "release_rate",
                         "total_level",
                         "amplitude_modulation",
                         "vibrato",
                         "keyboard_scale_rate",
                         "reserved_algorithm"};
  const unsigned masks[] = {3,  15, 255, 15,  15,  255, 15,
                            15, 63, 255, 255, 255, 255};
  for (unsigned instrument = 0; instrument < count; ++instrument) {
    auto start =
        instrument * 28 + (b.size() == 2690 && instrument >= 48 ? 2 : 0);
    for (unsigned op = 0; op < 2; ++op) {
      add(d, O{{"id", "instrument_" + std::to_string(instrument) +
                          "_operator_" + std::to_string(op)},
               {"kind", "adlib-operator"},
               {"instrument", instrument},
               {"role", op == 0 ? "modulator" : "carrier"},
               {"offset", start + op * 13}});
      for (unsigned i = 0; i < 13; ++i) {
        std::string name = names[i];
        auto kind = Binding::integer;
        unsigned mask = masks[i];
        if (i == 5 || i == 9 || i == 10 || i == 11)
          kind = Binding::boolean;
        if (op == 0 && i == 2) {
          name = "feedback";
          mask = 7;
        }
        if (op == 0 && i == 12) {
          name = "additive_algorithm";
          kind = Binding::inverted_boolean;
        }
        field(d, b, start + op * 13 + i, 1, name, kind, mask);
      }
      field(d, b, start + 26 + op, 1, "waveform", Binding::integer, 3);
    }
  }
  return d;
}
Document mt32(ResourceId id, const Bytes &b) {
  require(b.size() >= 492, "patch JSON: truncated MT-32 header");
  auto count = b[491];
  require(count <= 64 && 492 + std::size_t(count) * 246 <= b.size(),
          "patch JSON: invalid MT-32 timbre count");
  auto d = base(id, "mt32-bank");
  d.semantic = true;
  add(d, O{{"id", "display"}, {"kind", "mt32-display"}, {"offset", 0}});
  field(d, b, 0, 20, "display_message_0", Binding::fixed_text);
  field(d, b, 20, 20, "display_message_1", Binding::fixed_text);
  field(d, b, 40, 20, "goodbye_message", Binding::fixed_text);
  field(d, b, 60, 2, "volume");
  field(d, b, 62, 1, "reverb");
  cells(d, b, 63, 107, "mt32-reverb-byte");
  auto patches = [&](std::size_t begin, unsigned first) {
    for (unsigned i = 0; i < 48; ++i) {
      auto pos = begin + i * 8;
      add(d, O{{"id", "program_" + std::to_string(first + i)},
               {"kind", "mt32-program"},
               {"offset", pos}});
      field(d, b, pos, 1, "timbre_group");
      field(d, b, pos + 1, 1, "timbre_number");
      field(d, b, pos + 2, 1, "key_shift");
      field(d, b, pos + 3, 1, "fine_tune");
      field(d, b, pos + 4, 1, "bend_range");
      field(d, b, pos + 5, 1, "assign_mode");
      field(d, b, pos + 6, 1, "reverb_switch");
      field(d, b, pos + 7, 1, "reserved");
    }
  };
  patches(107, 0);
  for (unsigned timbre = 0; timbre < count; ++timbre) {
    auto pos = 492 + timbre * 246;
    add(d, O{{"id", "timbre_" + std::to_string(timbre)},
             {"kind", "mt32-timbre"},
             {"offset", pos}});
    field(d, b, pos, 10, "name", Binding::fixed_text);
    field(d, b, pos + 10, 1, "structure_1_2");
    field(d, b, pos + 11, 1, "structure_3_4");
    field(d, b, pos + 12, 1, "partial_mute");
    field(d, b, pos + 13, 1, "no_sustain");
    for (unsigned partial = 0; partial < 4; ++partial) {
      add(d, O{{"id", "timbre_" + std::to_string(timbre) + "_partial_" +
                          std::to_string(partial)},
               {"kind", "mt32-partial"},
               {"offset", pos + 14 + partial * 58}});
      for (unsigned parameter = 0; parameter < 58; ++parameter)
        field(d, b, pos + 14 + partial * 58 + parameter, 1,
              "parameter_" + std::to_string(parameter));
    }
  }
  auto pos = 492 + std::size_t(count) * 246;
  if (pos + 2 <= b.size() && b[pos] == 0xab && b[pos + 1] == 0xcd) {
    pos += 2;
    require(b.size() - pos >= 384, "patch JSON: truncated MT-32 program bank");
    patches(pos, 48);
    pos += 384;
  }
  if (pos < b.size()) {
    Reader rhythm(b, pos, b.size());
    require(rhythm.be(2) == 0xdcba && rhythm.remaining() >= 265,
            "patch JSON: invalid MT-32 rhythm extension");
    pos += 2;
    cells(d, b, pos, pos + 256, "mt32-rhythm-byte");
    pos += 256;
    cells(d, b, pos, pos + 9, "mt32-partial-reserve");
    pos += 9;
  }
  require(pos == b.size(), "patch JSON: unknown trailing MT-32 data");
  return d;
}
Document gm(ResourceId id, const Bytes &b) {
  require(b.size() >= 1155 &&
              script_detail::word(b, 1153, b.size()) == b.size() - 1155,
          "patch JSON: invalid GM initialization length");
  auto d = base(id, "gm-mapping");
  d.semantic = true;
  for (unsigned program = 0; program < 128; ++program) {
    add(d, O{{"id", program}, {"kind", "gm-program"}});
    field(d, b, program, 1, "program_map");
    field(d, b, 128 + program, 1, "key_shift", Binding::signed_integer);
    field(d, b, 256 + program, 1, "volume_adjustment", Binding::signed_integer);
    field(d, b, 384 + program, 1, "percussion_map");
    field(d, b, 513 + program, 1, "velocity_map");
  }
  add(d, O{{"id", "rhythm"}, {"kind", "gm-rhythm"}});
  field(d, b, 512, 1, "volume_adjustment", Binding::signed_integer);
  for (unsigned map = 0; map < 4; ++map)
    for (unsigned velocity = 0; velocity < 128; ++velocity) {
      auto pos = 641 + map * 128 + velocity;
      add(d, O{{"id", "velocity_" + std::to_string(map) + "_" +
                          std::to_string(velocity)},
               {"kind", "gm-velocity"},
               {"map", map},
               {"input", velocity}});
      field(d, b, pos, 1, "output");
    }
  Reader r(b, 1155, b.size());
  unsigned status = 0;
  std::size_t index = 0;
  while (r.remaining()) {
    auto begin = r.pos;
    auto next = r.byte();
    bool explicit_status = next >= 128;
    if (explicit_status)
      status = next;
    else
      --r.pos;
    require(status >= 128 && status <= 0xf0,
            "patch JSON: invalid GM initialization status");
    add(d, O{{"id", index++},
             {"kind", "gm-initialization-message"},
             {"offset", begin},
             {"status", status}});
    if (status == 0xf0) {
      unsigned parameter = 0;
      while (r.remaining() && b[r.pos] != 0xf7) {
        field(d, b, r.pos, 1, "sysex_" + std::to_string(parameter++),
              Binding::integer, 127);
        r.byte();
      }
      require(r.remaining() > 0, "patch JSON: unterminated SysEx");
      r.byte();
      status = 0;
    } else {
      auto count = (status >> 4) == 12 || (status >> 4) == 13 ? 1u : 2u;
      for (unsigned i = 0; i < count; ++i) {
        field(d, b, r.pos, 1, "parameter_" + std::to_string(i),
              Binding::integer, 127);
        r.byte();
      }
    }
  }
  return d;
}
}
