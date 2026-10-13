// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_fixtures.hpp"
#include "scimate/asset_io.hpp"
#include "scimate/assets.hpp"
#include "scimate/game_io.hpp"
#include "scimate/sound.hpp"
#include "scimate/view_io.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
namespace {
using namespace fixture;
namespace fs = std::filesystem;
void write(const fs::path &p, const Bytes &b) {
  std::ofstream f(p, std::ios::binary);
  f.write(reinterpret_cast<const char *>(b.data()), b.size());
  require(bool(f), "write failed");
}
Bytes read(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(f), {});
}
Bytes sound_bytes() {
  Sound s;
  s.header = Bytes(33, 0);
  s.header[1] = 2;
  s.header[2] = 255;
  s.events = {{0, {0x90, 60, 100}}, {60, {0x80, 60, 0}}, {60, {0xcf, 127}}};
  s.end_tick = 120;
  return write_sci0_sound(s);
}
void tests(const fs::path &root) {
  auto sound = sound_bytes();
  auto font = Font{};
  font.line_height = 8;
  ViewCel glyph;
  glyph.width = 2;
  glyph.height = 2;
  glyph.transparent = 15;
  glyph.pixels = {0, 15, 15, 0};
  font.glyphs = {glyph, ViewCel{}};
  font.glyphs[1].transparent = 15;
  Cursor cursor;
  cursor.image.width = 16;
  cursor.image.height = 16;
  cursor.image.transparent = 3;
  cursor.image.pixels = Bytes(256, 3);
  const std::vector<std::pair<ResourceId, Bytes>> assets{
      {{ResourceType::sound, 5}, sound},
      {{ResourceType::font, 0}, write_sci0_font(font)},
      {{ResourceType::cursor, 0}, write_sci0_cursor(cursor)},
      {{ResourceType::text, 2},
       write_sci0_text({"hi\tthere", std::string(1, char(0x82)), ""})},
      {{ResourceType::vocab, 0},
       write_sci0_vocabulary({{"a", 1, 3}, {"an", 1, 3}})},
      {{ResourceType::vocab, 997}, {0, 0, 1, 0}},
      {{ResourceType::script, 0}, {2, 0, 5, 0, 0x48, 0, 0}},
      {{ResourceType::memory, 1}, {1, 2}},
      {{ResourceType::patch, 0}, {3, 4}},
      {{ResourceType::sound, 6}, {1, 2, 3}}};
  GameFiles game;
  for (std::size_t i = 0; i < assets.size(); ++i) {
    const auto path = root / std::to_string(i);
    const auto result =
        export_sci0_asset(assets[i].first, assets[i].second, path);
    require(result.editable == (i < 5 || i == 6), "incorrect representation");
    ResourceId id{};
    require(import_sci0_asset(path, &id) == assets[i].second &&
                id.type == assets[i].first.type &&
                id.number == assets[i].first.number,
            "standalone byte roundtrip");
    record(game, (unsigned(id.type) << 11) | id.number, 1, 0,
           assets[i].second.size(), assets[i].second);
  }
  require(fs::exists(root / "9/conversion.error.txt"),
          "unsupported sound diagnostic");
  auto changed_sound = read_sci0_sound(sound);
  changed_sound.events[0].message[1] = 64;
  write(root / "0/sound.mid", encode_sound_midi(changed_sound));
  require(read_sci0_sound(import_sci0_asset(root / "0")).events ==
              changed_sound.events,
          "edited MIDI import");
  glyph.pixels[0] = 15;
  write(root / "1/glyph-0.png", encode_ega_png(glyph));
  require(read_sci0_font(import_sci0_asset(root / "1")).glyphs[0].pixels ==
              glyph.pixels,
          "glyph PNG edit");
  cursor.image.pixels[0] = 0;
  write(root / "2/cursor.png", encode_cel_png(cursor.image));
  require(read_sci0_cursor(import_sci0_asset(root / "2")).image == cursor.image,
          "cursor PNG edit");
  write(root / "3/strings.tsv",
        {'i', 'n', 'd', 'e', 'x', '\t', 't', 'e', 'x', 't', '\n', '0', '\t',
         'e', 'd', 'i', 't', '\n'});
  require(read_sci0_text(import_sci0_asset(root / "3")) ==
              std::vector<std::string>({"edit"}),
          "escaped text edit");
  auto vocab = read(root / "4/words.tsv");
  vocab.back() = '\n';
  auto v = std::string(vocab.begin(), vocab.end());
  v.replace(v.find("an\t1\t3"), 6, "an\t2\t4");
  write(root / "4/words.tsv", Bytes(v.begin(), v.end()));
  require(read_sci0_vocabulary(import_sci0_asset(root / "4"))[1].group == 4,
          "vocabulary edit");
  auto opaque_bytes = read(root / "5/resource.json");
  auto opaque = std::string(opaque_bytes.begin(), opaque_bytes.end());
  const auto value = opaque.find("\"value\": 0");
  require(value != std::string::npos, "missing byte record");
  opaque.replace(value, 10, "\"value\": 8");
  write(root / "5/resource.json", Bytes(opaque.begin(), opaque.end()));
  require(import_sci0_asset(root / "5") == Bytes({8, 0, 1, 0}),
          "byte-level JSON edit");
  game.map.insert(game.map.end(), 6, 255);
  fs::create_directory(root / "game");
  write(root / "game/resource.map", game.map);
  write(root / "game/resource.001", game.volumes[1]);
  auto summary = export_sci0_game(root / "game", root / "export");
  require(summary.editable_assets == 6 && summary.binary_assets == 4 &&
              summary.asset_errors == 1,
          "whole game summary");
  import_sci0_game(root / "export", root / "unchanged");
  require(open_sci0_game(root / "unchanged").original.volumes == game.volumes,
          "whole game unchanged volumes");
  write(root / "export/assets/0000000/sound.mid",
        encode_sound_midi(changed_sound));
  import_sci0_game(root / "export", root / "edited");
  require(read_sci0_sound(open_sci0_game(root / "edited").resources[0].payload)
                  .events == changed_sound.events,
          "whole game sound edit");
  write(root / "export/payloads/0000000.bin", {1});
  fails([&] { import_sci0_game(root / "export", root / "conflict"); },
        "conflicting");
  require(!fs::exists(root / "conflict"), "failed import created output");
  // Removing an individual sidecar is an error; old exports with no assets
  // still import.
  fs::remove(root / "export/assets/0000000/sound.mid");
  fails([&] { import_sci0_asset(root / "export/assets/0000000"); },
        "regular file");
  fs::remove_all(root / "export/assets");
  import_sci0_game(root / "export", root / "legacy");
  require(open_sci0_game(root / "legacy").resources[0].payload == Bytes({1}),
          "old binary export compatibility");
  auto manifest = read(root / "0/asset.tsv");
  auto m = std::string(manifest.begin(), manifest.end());
  m.replace(m.find("midi"), 4, "raw");
  write(root / "0/asset.tsv", Bytes(m.begin(), m.end()));
  write(root / "0/payload.bin", sound);
  require(import_sci0_asset(root / "0") == sound,
          "legacy raw bundle compatibility");
  auto fields = read(root / "1/font.tsv");
  fields[fields.size() - 2] = 'x';
  write(root / "1/font.tsv", fields);
  fails([&] { import_sci0_asset(root / "1"); }, "manifest");
  fails([&] { export_sci0_asset({ResourceType::sound, 0}, sound, root / "0"); },
        "already exists");
  import_sci0_asset_file(root / "5", root / "binary.bin");
  fails([&] { import_sci0_asset_file(root / "5", root / "binary.bin"); },
        "already exists");
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--write-fixture") {
      write(argv[2], sound_bytes());
      return 0;
    }
    require(argc == 2, "expected fixture parent");
    auto root =
        fs::path(argv[1]) /
        ("run-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    tests(root);
    fs::remove_all(root);
    std::cout << "Asset filesystem tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
