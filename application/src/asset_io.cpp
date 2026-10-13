// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/asset_io.hpp"
#include "files.hpp"
#include "scimate/assets.hpp"
#include "scimate/picture_io.hpp"
#include "scimate/sound.hpp"
#include "scimate/view_io.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <sstream>
namespace scimate {
namespace {
using Files = std::map<std::string, Bytes>;
void check(bool ok, const char *error =
                        "asset manifest: invalid structure or metadata") {
  if (!ok)
    throw std::runtime_error(error);
}
Bytes bytes(const std::string &s) { return Bytes(s.begin(), s.end()); }
std::string text(const Bytes &b) { return std::string(b.begin(), b.end()); }
unsigned integer(const std::string &s, unsigned max = 65535) {
  unsigned value = 0;
  auto r = std::from_chars(s.data(), s.data() + s.size(), value);
  check(r.ec == std::errc{} && r.ptr == s.data() + s.size() && value <= max,
        "asset table: integer outside allowed range");
  return value;
}
std::vector<std::string> row(std::istream &input) {
  std::string line;
  check(bool(std::getline(input, line)), "asset table: missing row");
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  std::vector<std::string> fields;
  std::size_t begin = 0;
  for (;;) {
    auto end = line.find('\t', begin);
    fields.push_back(
        line.substr(begin, end == std::string::npos ? end : end - begin));
    if (end == std::string::npos)
      break;
    begin = end + 1;
  }
  return fields;
}
void end(std::istream &input) {
  std::string extra;
  check(!std::getline(input, extra), "asset table: unexpected trailing row");
}
std::istringstream table(const std::filesystem::path &path) {
  return std::istringstream(text(detail::read_file(path, 1024 * 1024)));
}
std::string escape(const std::string &s) {
  const char *hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (c == '\\')
      out += "\\\\";
    else if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      out += "\\r";
    else if (c == '\t')
      out += "\\t";
    else if (c < 32 || c >= 127) {
      out += "\\x";
      out += hex[c >> 4];
      out += hex[c & 15];
    } else
      out += char(c);
  }
  return out;
}
std::string unescape(const std::string &s) {
  auto nibble = [](char c) -> unsigned {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    throw std::runtime_error("asset text: invalid hex escape");
  };
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    unsigned char c = s[i];
    check(c >= 32 && c < 127,
          "asset text: use byte escapes for non-ASCII characters");
    if (c != '\\') {
      out += char(c);
      continue;
    }
    check(++i < s.size(), "asset text: truncated escape");
    c = s[i];
    if (c == '\\')
      out += '\\';
    else if (c == 'n')
      out += '\n';
    else if (c == 'r')
      out += '\r';
    else if (c == 't')
      out += '\t';
    else if (c == 'x') {
      check(i + 2 < s.size(), "asset text: truncated hex escape");
      out += char((nibble(s[i + 1]) << 4) | nibble(s[i + 2]));
      i += 2;
    } else
      check(false, "asset text: unsupported escape");
  }
  return out;
}
std::string descriptor(ResourceId id, const std::string &representation) {
  return "SCIMATE-SCI0-ASSET\t1\ntype\tnumber\trepresentation\n" +
         std::to_string(unsigned(id.type)) + "\t" + std::to_string(id.number) +
         "\t" + representation + "\n";
}
std::string glyph_name(std::size_t index) {
  return "glyph-" + std::to_string(index) + ".png";
}
std::string prepare(ResourceId id, const Bytes &payload, Files &files) {
  if (id.type == ResourceType::sound) {
    const auto sound = read_sci0_sound(payload);
    files["sound.mid"] = encode_sound_midi(sound);
    std::ostringstream channels;
    channels << "channel\tvoices\tdevices\n";
    for (unsigned i = 0; i < 16; ++i)
      channels << i << '\t' << unsigned(sound.header[1 + i * 2]) << '\t'
               << unsigned(sound.header[2 + i * 2]) << '\n';
    files["channels.tsv"] = bytes(channels.str());
    if (!sound.sample_header.empty())
      files["sample.wav"] = encode_sound_wave(sound);
    return sound.sample_header.empty() ? "midi" : "midi-pcm";
  }
  if (id.type == ResourceType::font) {
    auto font = read_sci0_font(payload);
    std::ostringstream meta;
    meta << "line_height\t" << font.line_height
         << "\nindex\twidth\theight\tfile\n";
    for (std::size_t i = 0; i < font.glyphs.size(); ++i) {
      auto glyph = font.glyphs[i];
      meta << i << '\t' << glyph.width << '\t' << glyph.height << '\t'
           << glyph_name(i) << '\n';
      if (!glyph.width || !glyph.height) {
        glyph.width = std::max<unsigned>(1, glyph.width);
        glyph.height = std::max<unsigned>(1, glyph.height);
        glyph.pixels.assign(std::size_t(glyph.width) * glyph.height, 15);
      }
      files[glyph_name(i)] = encode_ega_png(glyph);
    }
    files["font.tsv"] = bytes(meta.str());
    return "font";
  }
  if (id.type == ResourceType::cursor) {
    auto cursor = read_sci0_cursor(payload);
    files["cursor.png"] = encode_cel_png(cursor.image);
    files["cursor.tsv"] =
        bytes("hotspot\t" + std::to_string(cursor.hotspot) + "\n");
    return "cursor";
  }
  if (id.type == ResourceType::text) {
    auto strings = read_sci0_text(payload);
    std::ostringstream out;
    out << "index\ttext\n";
    for (std::size_t i = 0; i < strings.size(); ++i)
      out << i << '\t' << escape(strings[i]) << '\n';
    files["strings.tsv"] = bytes(out.str());
    return "text";
  }
  if (id.type == ResourceType::vocab && id.number == 0) {
    auto words = read_sci0_vocabulary(payload);
    std::ostringstream out;
    out << "word\tclass\tgroup\n";
    for (const auto &w : words)
      out << escape(w.text) << '\t' << w.word_class << '\t' << w.group << '\n';
    files["words.tsv"] = bytes(out.str());
    return "vocab0";
  }
  files["payload.bin"] = payload;
  return "raw";
}
Bytes load(ResourceId id, const std::string &representation,
           const Bytes &original, const std::filesystem::path &path) {
  if (representation == "raw")
    return detail::read_file(path / "payload.bin", 65535);
  if (representation == "midi" || representation == "midi-pcm") {
    check(id.type == ResourceType::sound);
    auto sound = read_sci0_sound(original);
    check(representation ==
          (sound.sample_header.empty() ? "midi" : "midi-pcm"));
    decode_sound_midi(detail::read_file(path / "sound.mid", 16 * 1024 * 1024),
                      sound);
    auto channels = table(path / "channels.tsv");
    check(row(channels) ==
          std::vector<std::string>({"channel", "voices", "devices"}));
    for (unsigned i = 0; i < 16; ++i) {
      auto f = row(channels);
      check(f.size() == 3 && f[0] == std::to_string(i));
      sound.header[1 + i * 2] = integer(f[1], 255);
      sound.header[2 + i * 2] = integer(f[2], 255);
    }
    end(channels);
    if (representation == "midi-pcm")
      decode_sound_wave(
          detail::read_file(path / "sample.wav", 16 * 1024 * 1024), sound);
    return write_sci0_sound(sound);
  }
  if (representation == "font") {
    check(id.type == ResourceType::font);
    auto font = read_sci0_font(original);
    auto meta = table(path / "font.tsv");
    auto f = row(meta);
    check(f.size() == 2 && f[0] == "line_height");
    font.line_height = integer(f[1], 255);
    check(row(meta) ==
          std::vector<std::string>({"index", "width", "height", "file"}));
    for (std::size_t i = 0; i < font.glyphs.size(); ++i) {
      auto fields = row(meta);
      check(fields.size() == 4 && fields[0] == std::to_string(i) &&
            fields[3] == glyph_name(i));
      auto width = integer(fields[1], 255), height = integer(fields[2], 200);
      auto glyph = decode_ega_png(
          detail::read_file(path / glyph_name(i), 16 * 1024 * 1024));
      check(glyph.width == std::max(1u, width) &&
                glyph.height == std::max(1u, height),
            "font: PNG dimensions disagree with metadata");
      for (auto pixel : glyph.pixels)
        check(pixel == 0 || pixel == 15, "font: glyphs must be black or white");
      glyph.width = width;
      glyph.height = height;
      if (!width || !height)
        glyph.pixels.clear();
      font.glyphs[i] = std::move(glyph);
    }
    end(meta);
    return write_sci0_font(font);
  }
  if (representation == "cursor") {
    check(id.type == ResourceType::cursor);
    auto cursor = read_sci0_cursor(original);
    auto meta = table(path / "cursor.tsv");
    auto f = row(meta);
    check(f.size() == 2 && f[0] == "hotspot");
    cursor.hotspot = integer(f[1], 8);
    end(meta);
    cursor.image = decode_cel_png(
        detail::read_file(path / "cursor.png", 16 * 1024 * 1024), 3);
    return write_sci0_cursor(cursor);
  }
  if (representation == "text") {
    check(id.type == ResourceType::text);
    auto input = table(path / "strings.tsv");
    check(row(input) == std::vector<std::string>({"index", "text"}));
    std::vector<std::string> strings;
    while (input.peek() != std::char_traits<char>::eof()) {
      auto f = row(input);
      check(f.size() == 2 && f[0] == std::to_string(strings.size()));
      strings.push_back(unescape(f[1]));
    }
    return write_sci0_text(strings);
  }
  if (representation == "vocab0") {
    check(id.type == ResourceType::vocab && id.number == 0);
    auto input = table(path / "words.tsv");
    check(row(input) == std::vector<std::string>({"word", "class", "group"}));
    std::vector<Word> words;
    while (input.peek() != std::char_traits<char>::eof()) {
      auto f = row(input);
      check(f.size() == 3);
      words.push_back({unescape(f[0]), std::uint16_t(integer(f[1], 4095)),
                       std::uint16_t(integer(f[2], 4095))});
    }
    if (words == read_sci0_vocabulary(original))
      return original;
    return write_sci0_vocabulary(words);
  }
  if (representation == "view") {
    check(id.type == ResourceType::view);
    return import_sci0_view(path);
  }
  if (representation == "picture") {
    check(id.type == ResourceType::pic);
    return import_sci0_picture(path);
  }
  throw std::runtime_error("asset manifest: unsupported representation");
}
} // namespace
AssetExportSummary export_sci0_asset(ResourceId id, const Bytes &payload,
                                     const std::filesystem::path &destination) {
  check(unsigned(id.type) <= 9 && id.number <= 2047 && payload.size() <= 65535,
        "asset: invalid SCI0 identity/payload size");
  Files files;
  std::string representation, diagnostic;
  AssetExportSummary summary;
  try {
    if (id.type == ResourceType::view) {
      read_sci0_view(payload);
      representation = "view";
    } else if (id.type == ResourceType::pic) {
      read_sci0_picture(payload);
      representation = "picture";
    } else
      representation = prepare(id, payload, files);
  } catch (const std::runtime_error &error) {
    representation = "raw";
    files.clear();
    files["payload.bin"] = payload;
    diagnostic = error.what();
    summary.conversion_error = true;
  }
  summary.editable = representation != "raw";
  if (representation == "view")
    export_sci0_view(payload, destination);
  else if (representation == "picture")
    export_sci0_picture(payload, destination);
  else {
    detail::create_destination(destination);
    detail::write_file(destination / "original.bin", payload);
    for (const auto &file : files)
      detail::write_file(destination / file.first, file.second);
  }
  if (!diagnostic.empty())
    detail::write_file(
        destination / "conversion.error.txt",
        bytes("Editable conversion unavailable; edit payload.bin instead.\n" +
              diagnostic + "\n"));
  detail::write_file(destination / "asset.tsv",
                     bytes(descriptor(id, representation)));
  return summary;
}
Bytes import_sci0_asset(const std::filesystem::path &directory,
                        ResourceId *id) {
  auto input = table(directory / "asset.tsv");
  check(row(input) == std::vector<std::string>({"SCIMATE-SCI0-ASSET", "1"}));
  check(row(input) ==
        std::vector<std::string>({"type", "number", "representation"}));
  auto f = row(input);
  check(f.size() == 3);
  end(input);
  ResourceId resource{ResourceType(integer(f[0], 9)),
                      std::uint16_t(integer(f[1], 2047))};
  auto original = detail::read_file(directory / "original.bin", 65535);
  std::string expected;
  try {
    if (resource.type == ResourceType::view) {
      read_sci0_view(original);
      expected = "view";
    } else if (resource.type == ResourceType::pic) {
      read_sci0_picture(original);
      expected = "picture";
    } else if (resource.type == ResourceType::sound) {
      expected = read_sci0_sound(original).sample_header.empty() ? "midi" : "midi-pcm";
    } else if (resource.type == ResourceType::font) {
      read_sci0_font(original);
      expected = "font";
    } else if (resource.type == ResourceType::cursor) {
      read_sci0_cursor(original);
      expected = "cursor";
    } else if (resource.type == ResourceType::text) {
      read_sci0_text(original);
      expected = "text";
    } else if (resource.type == ResourceType::vocab && resource.number == 0) {
      read_sci0_vocabulary(original);
      expected = "vocab0";
    } else {
      expected = "raw";
    }
  } catch (const std::runtime_error &) {
    expected = "raw";
  }
  check(f[2] == expected,
        "asset manifest: representation differs from original resource");
  auto edited = load(resource, f[2], original, directory);
  check(edited.size() <= 65535, "asset: payload exceeds 65535-byte limit");
  if (id)
    *id = resource;
  return edited;
}
void export_sci0_asset_file(ResourceId id, const std::filesystem::path &payload,
                            const std::filesystem::path &destination) {
  export_sci0_asset(id, detail::read_file(payload, 65535), destination);
}
void import_sci0_asset_file(const std::filesystem::path &directory,
                            const std::filesystem::path &payload) {
  auto bytes = import_sci0_asset(directory);
  if (std::filesystem::exists(std::filesystem::symlink_status(payload)))
    throw std::runtime_error("destination already exists: " + payload.string());
  detail::write_file(payload, bytes);
  if (detail::read_file(payload, 65535) != bytes)
    throw std::runtime_error("saved asset verification failed");
}
} // namespace scimate
