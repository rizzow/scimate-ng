// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_fixtures.hpp"
#include "scimate/assets.hpp"
#include "scimate/sound.hpp"
#include <iostream>
namespace {
using namespace fixture;
Sound sound_fixture() {
  Sound s;
  s.header = Bytes(33, 0);
  s.header[1] = 3;
  s.header[2] = 255;
  s.events = {
      {0, {0xc0, 7}},        {0, {0x90, 60, 100}},         {300, {0x80, 60, 0}},
      {300, {0xcf, 127}},    {301, {0xf0, 0x41, 1, 0xf7}}, {400, {0xcf, 9}},
      {401, {0xbf, 0x60, 1}}};
  s.end_tick = 500;
  return s;
}
void tests() {
  Font f;
  f.reserved = 7;
  f.line_height = 8;
  ViewCel g;
  g.width = 9;
  g.height = 2;
  g.transparent = 15;
  g.pixels = {0, 15, 0, 15, 0, 15, 0, 15, 0, 15, 0, 15, 0, 15, 0, 15, 0, 15};
  f.glyphs = {g};
  auto font_bytes = write_sci0_font(f);
  auto font = read_sci0_font(font_bytes);
  require(font.glyphs == f.glyphs && font.reserved == 7, "font bit rows");
  require(write_sci0_font(font) == font_bytes, "font unchanged");
  auto padded = font_bytes;
  padded.push_back(99);
  font = read_sci0_font(padded);
  require(write_sci0_font(font) == padded,
          "font trailing bytes retained unchanged");
  font.glyphs[0].pixels[0] = 15;
  require(read_sci0_font(write_sci0_font(font)).glyphs == font.glyphs,
          "font edit");
  auto bad = font_bytes;
  bad[6] = 0;
  fails([&] { read_sci0_font(bad); }, "offset");
  f.glyphs[0].pixels[0] = 2;
  fails([&] { write_sci0_font(f); }, "black or white");
  Cursor c;
  c.image.width = 16;
  c.image.height = 16;
  c.image.transparent = 3;
  c.image.pixels = Bytes(256, 3);
  c.image.pixels[0] = 0;
  c.image.pixels[1] = 15;
  c.hotspot = 8;
  auto cursor_bytes = write_sci0_cursor(c);
  auto cursor = read_sci0_cursor(cursor_bytes);
  require(cursor.image == c.image && cursor.hotspot == 8,
          "cursor mask/hotspot");
  cursor_bytes[4] |= 0x40;
  cursor = read_sci0_cursor(cursor_bytes);
  require(cursor.image.pixels[1] == 15 &&
              write_sci0_cursor(cursor) == cursor_bytes,
          "SCI0 white mask alias and unchanged bytes");
  c.hotspot = 4;
  fails([&] { write_sci0_cursor(c); }, "hotspot");
  require(read_sci0_text(
              write_sci0_text({"", "hi\nthere", std::string(1, char(0x82))})) ==
              std::vector<std::string>(
                  {"", "hi\nthere", std::string(1, char(0x82))}),
          "text byte strings");
  fails([] { read_sci0_text({'a'}); }, "terminator");
  fails([] { write_sci0_text({std::string("a\0b", 3)}); }, "NUL");
  std::vector<Word> words{{"a", 1, 1}, {"an", 2, 7}, {"an", 3, 7}, {"b", 4, 4}};
  auto vocab_bytes = write_sci0_vocabulary(words);
  require(read_sci0_vocabulary(vocab_bytes) == words,
          "vocab prefix/duplicate roundtrip");
  require(vocab_bytes[0] == 52 && vocab_bytes[2] > 52, "vocab alpha index");
  auto broken = vocab_bytes;
  broken[52] = 2;
  fails([&] { read_sci0_vocabulary(broken); }, "prefix");
  fails([] { write_sci0_vocabulary({{"b", 1, 1}, {"a", 1, 1}}); }, "sorted");
  Sound s = sound_fixture();
  auto sci = write_sci0_sound(s);
  auto reopened = read_sci0_sound(sci);
  require(reopened.events == s.events && reopened.end_tick == 500,
          "SCI MIDI delta/status/sysex");
  require(write_sci0_sound(reopened) == sci, "sound unchanged bytes");
  auto midi = encode_sound_midi(reopened);
  auto edited = reopened;
  decode_sound_midi(midi, edited);
  require(edited.events == s.events && edited.end_tick == 500,
          "MIDI 60Hz roundtrip including cues/loop");
  require(write_sci0_sound(edited) == sci,
          "unchanged MIDI preserves SCI bytes");
  // Running status and extended SCI delay markers.
  Bytes running(33, 0);
  running.insert(running.end(), {0, 0x90, 60, 100, 0xf8, 10, 61, 100, 0, 0xfc});
  auto notes = read_sci0_sound(running);
  require(notes.events.size() == 2 && notes.events[1].tick == 250 &&
              notes.events[1].message == Bytes({0x90, 61, 100}),
          "SCI running status");
  Bytes sample(33, 0);
  sample[0] = 2;
  sample.insert(sample.end(), {0, 0xfc, 0xfc});
  Bytes header(44, 0);
  header[14] = 0x11;
  header[15] = 0x2b;
  header[32] = 3;
  sample.insert(sample.end(), header.begin(), header.end());
  sample.insert(sample.end(), {0, 128, 255, 99});
  auto pcm = read_sci0_sound(sample);
  require(pcm.sample_rate == 11025 && pcm.samples == Bytes({0, 128, 255}),
          "SCI sample header");
  auto wave = encode_sound_wave(pcm);
  auto modified = pcm;
  decode_sound_wave(wave, modified);
  require(write_sci0_sound(modified) == sample,
          "unchanged WAV preserves sample/trailer");
  modified.samples.push_back(80);
  modified.sample_rate = 22050;
  auto changed = write_sci0_sound(modified);
  auto reread = read_sci0_sound(changed);
  require(reread.samples == modified.samples && reread.sample_rate == 22050 &&
              changed.back() == 99,
          "PCM edit/trailer preservation");
  auto bad_wave = wave;
  bad_wave[22] = 2;
  fails([&] { decode_sound_wave(bad_wave, pcm); }, "mono PCM");
  auto bad_midi = midi;
  bad_midi[12] = 0x80;
  fails([&] { decode_sound_midi(bad_midi, edited); }, "PPQN");
  // Tempo map in a separate format-1 track: ticks 60 and 120 -> SCI ticks 15
  // and 45.
  Bytes multi = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 2, 0, 60};
  auto track = [&](const Bytes &b) {
    multi.insert(multi.end(), {'M', 'T', 'r', 'k'});
    put(multi, b.size() >> 24, 1);
    put(multi, b.size() >> 16, 1);
    put(multi, b.size() >> 8, 1);
    put(multi, b.size(), 1);
    multi.insert(multi.end(), b.begin(), b.end());
  };
  track({0, 0xff, 0x51, 3, 3, 0xd0, 0x90, 60, 0xff, 0x51, 3, 7, 0xa1, 0x20, 60,
         0xff, 0x2f, 0});
  track({60, 0x90, 60, 100, 60, 0x80, 60, 0, 0, 0xff, 0x2f, 0});
  decode_sound_midi(multi, edited);
  require(edited.events.size() == 2 && edited.events[0].tick == 15 &&
              edited.events[1].tick == 45,
          "MIDI format1 tempo map");
  for (std::size_t size = 0; size < sci.size(); ++size)
    fails([&] { read_sci0_sound(Bytes(sci.begin(), sci.begin() + size)); }, "");
  for (std::size_t size = 0; size < midi.size(); ++size)
    fails(
        [&] {
          decode_sound_midi(Bytes(midi.begin(), midi.begin() + size), edited);
        },
        "");
  for (std::size_t size = 0; size < wave.size(); ++size)
    fails(
        [&] {
          decode_sound_wave(Bytes(wave.begin(), wave.begin() + size), modified);
        },
        "");
}
} // namespace
int main() {
  try {
    tests();
    std::cout << "SCI0 asset codec tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
