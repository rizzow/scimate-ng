// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
namespace scimate {
struct SoundEvent {
  std::uint32_t tick = 0;
  Bytes message;
};
bool operator==(const SoundEvent &a, const SoundEvent &b);
struct Sound {
  Bytes header; // 33-byte late voice/device pairs, or 17-byte early packed nibbles.
  std::vector<SoundEvent> events;
  std::uint32_t end_tick = 0;
  Bytes trailer; // Retained bytes after the MIDI stop event.
  Bytes sample_header,
      samples; // Optional 44-byte digital header and unsigned 8-bit mono PCM.
  unsigned sample_rate = 0;
  std::size_t sample_prefix = 0;
  Bytes original;
};
Sound read_sci0_sound(const Bytes &payload, unsigned header_bytes = 33);
Bytes write_sci0_sound(const Sound &sound);
// Standard MIDI format 0/1, PPQN timing; tempo maps converted to SCI's 60 Hz
// clock.
Bytes encode_sound_midi(const Sound &sound);
void decode_sound_midi(const Bytes &midi, Sound &sound);
Bytes encode_sound_wave(const Sound &sound);
void decode_sound_wave(const Bytes &wave, Sound &sound);
} // namespace scimate
