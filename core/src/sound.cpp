// SPDX-License-Identifier: GPL-2.0-or-later
// SCI0 format knowledge adapted from SCI Companion Resources/Sound.cpp.
// Copyright (c) 2015 Philip Fortier. GPL version 2 or (at your option) any
// later version; distributed without any warranty. See COPYING.
#include "scimate/sound.hpp"
#include "asset_binary.hpp"
#include <algorithm>
namespace scimate {
using namespace asset_detail;
namespace {
constexpr std::uint32_t max_tick = 0x0fffffff;
int parameters(unsigned status) {
  return (status >> 4) == 12 || (status >> 4) == 13 ? 1 : 2;
}
void validate_message(const Bytes &message) {
  require(!message.empty(), "sound: empty MIDI message");
  auto status = message[0];
  if (status == 0xf0) {
    require(message.size() >= 2 && message.back() == 0xf7,
            "sound: unterminated SysEx");
    for (std::size_t i = 1; i + 1 < message.size(); ++i)
      require(message[i] < 128, "sound: invalid SysEx data");
  } else {
    require(status >= 0x80 && status < 0xf0 &&
                message.size() == std::size_t(1 + parameters(status)),
            "sound: unsupported MIDI status/length");
    for (std::size_t i = 1; i < message.size(); ++i)
      require(message[i] < 128, "sound: MIDI parameter outside 0..127");
  }
}
void variable(Bytes &out, std::uint32_t value) {
  require(value <= max_tick, "MIDI: delta exceeds four-byte limit");
  Bytes bytes{std::uint8_t(value & 127)};
  while (value >>= 7)
    bytes.push_back(std::uint8_t((value & 127) | 128));
  out.insert(out.end(), bytes.rbegin(), bytes.rend());
}
std::uint32_t variable(Reader &r) {
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) {
    auto b = r.byte();
    value = (value << 7) | (b & 127);
    if (!(b & 128))
      return value;
  }
  throw std::runtime_error("MIDI: invalid variable-length quantity");
}
void tag(Bytes &out, const char *text) {
  out.insert(out.end(), text, text + 4);
}
void expect_tag(Reader &r, const char *text) {
  auto bytes = r.take(4);
  require(bytes == Bytes(text, text + 4), "audio: invalid chunk identifier");
}
void sci_delta(Bytes &out, std::uint32_t delta) {
  while (delta >= 240) {
    out.push_back(0xf8);
    delta -= 240;
    payload_limit(out);
  }
  out.push_back(delta);
}
} // namespace
bool operator==(const SoundEvent &a, const SoundEvent &b) {
  return a.tick == b.tick && a.message == b.message;
}
Sound read_sci0_sound(const Bytes &payload, unsigned header_bytes) {
  payload_limit(payload);
  Reader r(payload);
  Sound s;
  require(header_bytes == 17 || header_bytes == 33, "sound: header must be 17 or 33 bytes");
  s.header = r.take(header_bytes);
  require(s.header[0] == 0 || (header_bytes == 33 && s.header[0] == 2),
          "sound: unsupported SCI0 sample flag/header");
  unsigned status = 0;
  std::uint32_t tick = 0;
  for (;;) {
    auto delay = r.byte();
    if (delay == 0xfc) {
      s.end_tick = tick;
      break;
    }
    std::uint32_t delta = 0;
    while (delay == 0xf8) {
      delta += 240;
      delay = r.byte();
    }
    require(delay <= 240 && delta <= max_tick - tick &&
                delay <= max_tick - tick - delta,
            "sound: invalid delay/time limit");
    tick += delta + delay;
    auto next = r.byte();
    if (next == 0xfc) {
      s.end_tick = tick;
      break;
    }
    if (next >= 128)
      status = next;
    else {
      require(status >= 128 && status < 0xf0, "sound: invalid running status");
      --r.pos;
    }
    Bytes message{std::uint8_t(status)};
    if (status == 0xf0) {
      for (;;) {
        auto b = r.byte();
        message.push_back(b);
        if (b == 0xf7)
          break;
      }
    } else {
      require(status >= 128 && status < 0xf0, "sound: unsupported status");
      auto data = r.take(parameters(status));
      message.insert(message.end(), data.begin(), data.end());
    }
    validate_message(message);
    s.events.push_back({tick, std::move(message)});
  }
  s.trailer = r.take(r.remaining());
  if (s.header[0] == 2) {
    Reader sample(s.trailer);
    while (sample.remaining() && sample.bytes[sample.pos] == 0xfc)
      ++sample.pos;
    s.sample_prefix = sample.pos;
    if (sample.remaining()) {
      s.sample_header = sample.take(44);
      Reader info(s.sample_header, 14, 44);
      s.sample_rate = info.le(2);
      info.pos = 32;
      auto length = info.le(2);
      require(s.sample_rate > 0 && length <= sample.remaining(),
              "sound: invalid PCM rate/length");
      s.samples = sample.take(length);
    }
  }
  s.original = payload;
  return s;
}
Bytes write_sci0_sound(const Sound &s) {
  require((s.header.size() == 17 || s.header.size() == 33) &&
              (s.header[0] == 0 || (s.header.size() == 33 && s.header[0] == 2)) &&
              s.end_tick <= max_tick,
          "sound: invalid header/end tick");
  std::uint32_t tick = 0;
  for (const auto &e : s.events) {
    require(e.tick >= tick && e.tick <= s.end_tick,
            "sound: events must be ordered within end tick");
    validate_message(e.message);
    tick = e.tick;
  }
  if (!s.original.empty()) {
    auto old = read_sci0_sound(s.original, s.header.size());
    if (old.header == s.header && old.events == s.events &&
        old.end_tick == s.end_tick && old.trailer == s.trailer &&
        old.sample_header == s.sample_header && old.samples == s.samples &&
        old.sample_rate == s.sample_rate)
      return s.original;
  }
  Bytes out = s.header;
  tick = 0;
  for (const auto &e : s.events) {
    sci_delta(out, e.tick - tick);
    out.insert(out.end(), e.message.begin(), e.message.end());
    payload_limit(out);
    tick = e.tick;
  }
  sci_delta(out, s.end_tick - tick);
  out.push_back(0xfc);
  auto trailer = s.trailer;
  if (!s.sample_header.empty()) {
    require(s.header[0] == 2 && s.sample_header.size() == 44 &&
                s.sample_rate > 0 && s.sample_rate <= 65535 &&
                s.samples.size() <= 65535,
            "sound: invalid PCM header/rate/length");
    // Preserve marker padding and unknown sample-header bytes, plus any
    // trailing data.
    auto old = read_sci0_sound(s.original, s.header.size());
    require(old.sample_prefix == s.sample_prefix && !old.sample_header.empty(),
            "sound: inconsistent sample snapshot");
    auto header = s.sample_header;
    word_at(header, 14, s.sample_rate);
    word_at(header, 32, s.samples.size());
    require(s.sample_prefix + 44 + old.samples.size() <= trailer.size(),
            "sound: truncated sample trailer");
    Bytes revised(trailer.begin(), trailer.begin() + s.sample_prefix);
    revised.insert(revised.end(), header.begin(), header.end());
    revised.insert(revised.end(), s.samples.begin(), s.samples.end());
    revised.insert(revised.end(),
                   trailer.begin() + s.sample_prefix + 44 + old.samples.size(),
                   trailer.end());
    trailer = std::move(revised);
  }
  out.insert(out.end(), trailer.begin(), trailer.end());
  payload_limit(out);
  auto reopened = read_sci0_sound(out, s.header.size());
  require(reopened.events == s.events && reopened.end_tick == s.end_tick &&
              reopened.header == s.header && reopened.samples == s.samples &&
              reopened.sample_rate == s.sample_rate,
          "sound: serialization verification failed");
  return out;
}
Bytes encode_sound_midi(const Sound &s) {
  Bytes track{0,    0xff, 0x51, 3,
              0x07, 0xa1, 0x20}; // 500000 us/quarter, 30 PPQN = 60 ticks/s.
  std::uint32_t tick = 0;
  for (const auto &e : s.events) {
    require(e.tick >= tick && e.tick <= s.end_tick,
            "sound: invalid event order");
    validate_message(e.message);
    variable(track, e.tick - tick);
    tick = e.tick;
    if (e.message[0] == 0xf0) {
      track.push_back(0xf0);
      variable(track, e.message.size() - 1);
      track.insert(track.end(), e.message.begin() + 1, e.message.end());
    } else
      track.insert(track.end(), e.message.begin(), e.message.end());
  }
  require(s.end_tick >= tick, "sound: invalid end tick");
  variable(track, s.end_tick - tick);
  track.insert(track.end(), {0xff, 0x2f, 0});
  Bytes out;
  tag(out, "MThd");
  put(out, 6, 4, true);
  put(out, 0, 2, true);
  put(out, 1, 2, true);
  put(out, 30, 2, true);
  tag(out, "MTrk");
  put(out, track.size(), 4, true);
  out.insert(out.end(), track.begin(), track.end());
  return out;
}
void decode_sound_midi(const Bytes &midi, Sound &s) {
  require(midi.size() <= 16 * 1024 * 1024, "MIDI: input limit exceeded");
  Reader r(midi);
  expect_tag(r, "MThd");
  require(r.be(4) == 6, "MIDI: header length must be six");
  auto format = r.be(2), tracks = r.be(2), division = r.be(2);
  require(format <= 1 && tracks > 0 && tracks <= 256 &&
              (format != 0 || tracks == 1) && division > 0 && division < 0x8000,
          "MIDI: only format 0/1 with PPQN timing is supported");
  struct Item {
    std::uint32_t tick, tempo;
    Bytes message;
  };
  std::vector<Item> items;
  std::uint32_t end_tick = 0;
  for (unsigned track = 0; track < tracks; ++track) {
    expect_tag(r, "MTrk");
    auto length = r.be(4);
    require(length <= r.remaining(), "MIDI: truncated track");
    Reader t(midi, r.pos, r.pos + length);
    r.pos += length;
    std::uint32_t tick = 0;
    unsigned status = 0;
    bool ended = false;
    while (t.remaining()) {
      auto delta = variable(t);
      require(delta <= max_tick - tick, "MIDI: track duration limit exceeded");
      tick += delta;
      auto next = t.byte();
      if (next == 0xff) {
        auto type = t.byte();
        auto data = t.take(variable(t));
        status = 0;
        if (type == 0x2f) {
          require(data.empty() && t.remaining() == 0,
                  "MIDI: invalid end-of-track");
          ended = true;
          break;
        }
        if (type == 0x51) {
          require(data.size() == 3, "MIDI: invalid tempo");
          unsigned tempo =
              (unsigned(data[0]) << 16) | (unsigned(data[1]) << 8) | data[2];
          require(tempo > 0, "MIDI: zero tempo");
          items.push_back({tick, tempo, {}});
        } else
          require(type != 0x54, "MIDI: SMPTE offset is unsupported");
      } else if (next == 0xf0) {
        auto data = t.take(variable(t));
        Bytes message{0xf0};
        message.insert(message.end(), data.begin(), data.end());
        validate_message(message);
        items.push_back({tick, 0, std::move(message)});
        status = 0;
      } else {
        require(next != 0xf7, "MIDI: split/escaped SysEx is unsupported");
        if (next >= 128)
          status = next;
        else {
          require(status >= 128 && status < 0xf0,
                  "MIDI: invalid running status");
          --t.pos;
        }
        require(status >= 128 && status < 0xf0, "MIDI: unsupported status");
        Bytes message{std::uint8_t(status)};
        auto data = t.take(parameters(status));
        message.insert(message.end(), data.begin(), data.end());
        validate_message(message);
        items.push_back({tick, 0, std::move(message)});
      }
      require(items.size() <= 65535, "MIDI: event limit exceeded");
    }
    require(ended, "MIDI: missing end-of-track");
    end_tick = std::max(end_tick, tick);
  }
  require(r.remaining() == 0, "MIDI: unexpected trailing data");
  std::stable_sort(
      items.begin(), items.end(),
      [](const Item &a, const Item &b) { return a.tick < b.tick; });
  std::uint64_t units = 0;
  std::uint32_t tick = 0, tempo = 500000;
  auto to_sci = [&]() {
    auto time = (units * 60 + std::uint64_t(division) * 500000) /
                (std::uint64_t(division) * 1000000);
    require(time <= max_tick, "MIDI: SCI duration limit exceeded");
    return std::uint32_t(time);
  };
  std::vector<SoundEvent> events;
  for (auto &item : items) {
    units += std::uint64_t(item.tick - tick) * tempo;
    tick = item.tick;
    if (item.tempo)
      tempo = item.tempo;
    else
      events.push_back({to_sci(), std::move(item.message)});
  }
  units += std::uint64_t(end_tick - tick) * tempo;
  s.end_tick = to_sci();
  s.events = std::move(events);
}
Bytes encode_sound_wave(const Sound &s) {
  require(!s.sample_header.empty() && s.sample_rate > 0 &&
              s.sample_rate <= 65535 && s.samples.size() <= 65535,
          "WAV: no valid sample");
  Bytes out;
  tag(out, "RIFF");
  put(out, 36 + s.samples.size() + (s.samples.size() % 2), 4);
  tag(out, "WAVE");
  tag(out, "fmt ");
  put(out, 16, 4);
  put(out, 1, 2);
  put(out, 1, 2);
  put(out, s.sample_rate, 4);
  put(out, s.sample_rate, 4);
  put(out, 1, 2);
  put(out, 8, 2);
  tag(out, "data");
  put(out, s.samples.size(), 4);
  out.insert(out.end(), s.samples.begin(), s.samples.end());
  if (s.samples.size() % 2)
    out.push_back(0);
  return out;
}
void decode_sound_wave(const Bytes &wave, Sound &s) {
  require(wave.size() <= 16 * 1024 * 1024 && !s.sample_header.empty(),
          "WAV: input limit or sample layout unsupported");
  Reader r(wave);
  expect_tag(r, "RIFF");
  require(r.le(4) == wave.size() - 8, "WAV: inconsistent RIFF length");
  expect_tag(r, "WAVE");
  bool fmt = false, data = false;
  unsigned rate = 0;
  Bytes samples;
  while (r.remaining()) {
    auto name = r.take(4);
    auto length = r.le(4);
    require(length <= r.remaining(), "WAV: truncated chunk");
    Reader chunk(wave, r.pos, r.pos + length);
    r.pos += length;
    if (name == Bytes{'f', 'm', 't', ' '}) {
      require(!fmt && length >= 16, "WAV: invalid format chunk");
      require(chunk.le(2) == 1 && chunk.le(2) == 1,
              "WAV: only mono PCM is supported");
      rate = chunk.le(4);
      auto byte_rate = chunk.le(4), align = chunk.le(2), bits = chunk.le(2);
      require(rate > 0 && rate <= 65535 && byte_rate == rate && align == 1 &&
                  bits == 8,
              "WAV: only unsigned 8-bit mono PCM with SCI0 rate is supported");
      fmt = true;
    } else if (name == Bytes{'d', 'a', 't', 'a'}) {
      require(!data && length <= 65535, "WAV: invalid data length");
      samples = chunk.take(length);
      data = true;
    }
    if (length % 2)
      r.byte();
  }
  require(fmt && data, "WAV: missing format/data chunk");
  s.sample_rate = rate;
  s.samples = std::move(samples);
}
} // namespace scimate
