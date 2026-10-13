// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/view_io.hpp"
#include "files.hpp"
#include <png.h>
#include <array>
#include <charconv>
#include <sstream>

namespace scimate {
namespace {
constexpr std::array<std::array<std::uint8_t, 3>,16> ega{{
    {{0,0,0}},{{0,0,170}},{{0,170,0}},{{0,170,170}},
    {{170,0,0}},{{170,0,170}},{{170,85,0}},{{170,170,170}},
    {{85,85,85}},{{85,85,255}},{{85,255,85}},{{85,255,255}},
    {{255,85,85}},{{255,85,255}},{{255,255,85}},{{255,255,255}}
}};
struct Png {
    png_image image{};
    Png() { image.version = PNG_IMAGE_VERSION; }
    ~Png() { png_image_free(&image); }
    Png(const Png&) = delete;
    Png& operator=(const Png&) = delete;
};
[[noreturn]] void png_error(const Png& p) { throw std::runtime_error(std::string("PNG: ") + p.image.message); }
std::string filename(std::size_t loop, std::size_t cel) {
    return "loop-" + std::to_string(loop) + "-cel-" + std::to_string(cel) + ".png";
}
std::string header = "SCIMATE-SCI0-VIEW\t1";
std::string columns = "kind\tloop\tcel_or_source\tx\ty\ttransparent\tfile";
std::vector<std::string> fields(std::istream& input) {
    std::string line;
    if (!std::getline(input,line)) throw std::runtime_error("view manifest: missing row");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> result;
    std::size_t begin = 0;
    for (;;) {
        auto end = line.find('\t',begin);
        result.push_back(line.substr(begin,end == std::string::npos ? end : end-begin));
        if (end == std::string::npos) break;
        begin = end+1;
    }
    return result;
}
void expect(bool condition) { if (!condition) throw std::runtime_error("view manifest: invalid structure or metadata"); }
int integer(const std::string& s, int min, int max) {
    int value = 0;
    auto result = std::from_chars(s.data(),s.data()+s.size(),value);
    if (result.ec != std::errc{} || result.ptr != s.data()+s.size() || value < min || value > max)
        throw std::runtime_error("view manifest: integer outside allowed range");
    return value;
}
} // namespace

static Bytes encode_png(const ViewCel& cel, bool opaque) {
    if (!cel.width || !cel.height || cel.width > 320 || cel.height > 200 || (!opaque && cel.transparent > 15) ||
        cel.pixels.size() != static_cast<std::size_t>(cel.width)*cel.height)
        throw std::runtime_error("PNG: invalid cel dimensions/palette");
    for (auto pixel : cel.pixels) if (pixel > 15) throw std::runtime_error("PNG: invalid EGA pixel");
    Png p;
    p.image.width = cel.width; p.image.height = cel.height;
    p.image.format = PNG_FORMAT_RGBA_COLORMAP; p.image.colormap_entries = 16;
    std::array<std::uint8_t,64> palette{};
    for (std::size_t i = 0; i < 16; ++i) {
        for (std::size_t c = 0; c < 3; ++c) palette[i*4+c] = ega[i][c];
        palette[i*4+3] = !opaque && i == cel.transparent ? 0 : 255;
    }
    png_alloc_size_t size = 0;
    if (!png_image_write_to_memory(&p.image,nullptr,&size,0,cel.pixels.data(),0,palette.data())) png_error(p);
    Bytes result(size);
    if (!png_image_write_to_memory(&p.image,result.data(),&size,0,cel.pixels.data(),0,palette.data())) png_error(p);
    result.resize(size);
    return result;
}

static ViewCel decode_png(const Bytes& bytes, std::uint8_t transparent, bool opaque) {
    if (bytes.size() > 16*1024*1024 || (!opaque && transparent > 15)) throw std::runtime_error("PNG: input/palette limit exceeded");
    Png p;
    if (!png_image_begin_read_from_memory(&p.image,bytes.data(),bytes.size())) png_error(p);
    if (!p.image.width || !p.image.height || p.image.width > 320 || p.image.height > 200)
        throw std::runtime_error("PNG: dimensions must be 1..320 by 1..200");
    if (bytes.size() < 25 || bytes[24] > 8) throw std::runtime_error("PNG: 16-bit channels are unsupported");
    p.image.format = PNG_FORMAT_RGBA;
    Bytes rgba(PNG_IMAGE_SIZE(p.image));
    if (!png_image_finish_read(&p.image,nullptr,rgba.data(),0,nullptr)) png_error(p);
    ViewCel cel;
    cel.width = static_cast<std::uint16_t>(p.image.width); cel.height = static_cast<std::uint16_t>(p.image.height);
    cel.transparent = transparent;
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        if (!opaque && rgba[i+3] == 0) { cel.pixels.push_back(transparent); continue; }
        if (rgba[i+3] != 255) throw std::runtime_error("PNG: partial alpha is not representable in SCI0");
        int index = -1;
        for (int c = 0; c < 16; ++c)
            if (rgba[i] == ega[static_cast<std::size_t>(c)][0] && rgba[i+1] == ega[static_cast<std::size_t>(c)][1] && rgba[i+2] == ega[static_cast<std::size_t>(c)][2]) { index = c; break; }
        if (index < 0) throw std::runtime_error("PNG: color is outside the EGA palette (no automatic quantization)");
        if (!opaque && index == transparent) throw std::runtime_error("PNG: opaque pixel uses the designated transparent color");
        cel.pixels.push_back(static_cast<std::uint8_t>(index));
    }
    return cel;
}

Bytes encode_cel_png(const ViewCel& cel) { return encode_png(cel,false); }
ViewCel decode_cel_png(const Bytes& bytes, std::uint8_t transparent) { return decode_png(bytes,transparent,false); }
Bytes encode_picture_plane_png(const Bytes& pixels) {
    ViewCel cel; cel.width=320; cel.height=190; cel.pixels=pixels;
    return encode_png(cel,true);
}
Bytes decode_picture_plane_png(const Bytes& bytes) {
    auto cel=decode_png(bytes,0,true);
    if(cel.width!=320 || cel.height!=190) throw std::runtime_error("picture PNG: dimensions must be 320x190");
    return cel.pixels;
}

void export_sci0_view(const Bytes& payload, const std::filesystem::path& destination) {
    const auto view = read_sci0_view(payload);
    detail::create_destination(destination);
    detail::write_file(destination / "original.bin",payload);
    std::ostringstream text;
    text << header << '\n' << columns << '\n';
    for (std::size_t l = 0; l < view.loops.size(); ++l) {
        const auto& loop = view.loops[l];
        if (loop.mirror_of >= 0) { text << "mirror\t" << l << '\t' << loop.mirror_of << "\t-\t-\t-\t-\n"; continue; }
        text << "loop\t" << l << '\t' << loop.cels.size() << "\t-\t-\t-\t-\n";
        for (std::size_t c = 0; c < loop.cels.size(); ++c) {
            const auto& cel = loop.cels[c];
            text << "cel\t" << l << '\t' << c << '\t' << cel.x << '\t' << cel.y << '\t'
                 << static_cast<unsigned>(cel.transparent) << '\t' << filename(l,c) << '\n';
            detail::write_file(destination / filename(l,c),encode_cel_png(cel));
        }
    }
    const auto manifest = text.str();
    detail::write_file(destination / "view.tsv",Bytes(manifest.begin(),manifest.end()));
}

Bytes import_sci0_view(const std::filesystem::path& directory) {
    auto view = read_sci0_view(detail::read_file(directory / "original.bin",65535));
    const auto manifest = detail::read_file(directory / "view.tsv",1024*1024);
    std::istringstream input(std::string(manifest.begin(),manifest.end()));
    expect(fields(input) == std::vector<std::string>({"SCIMATE-SCI0-VIEW","1"}));
    expect(fields(input) == std::vector<std::string>({"kind","loop","cel_or_source","x","y","transparent","file"}));
    for (std::size_t l = 0; l < view.loops.size(); ++l) {
        auto& loop = view.loops[l];
        const auto f = fields(input);
        expect(f.size() == 7);
        expect(f[0] == (loop.mirror_of >= 0 ? "mirror" : "loop") && f[1] == std::to_string(l) &&
               f[2] == (loop.mirror_of >= 0 ? std::to_string(loop.mirror_of) : std::to_string(loop.cels.size())) &&
               f[3] == "-" && f[4] == "-" && f[5] == "-" && f[6] == "-");
        if (loop.mirror_of >= 0) continue;
        for (std::size_t c = 0; c < loop.cels.size(); ++c) {
            const auto row = fields(input); expect(row.size() == 7);
            expect(row[0] == "cel" && row[1] == std::to_string(l) && row[2] == std::to_string(c) && row[6] == filename(l,c));
            const auto x = integer(row[3],-128,127), y = integer(row[4],-128,127);
            const auto transparent = static_cast<std::uint8_t>(integer(row[5],0,15));
            auto cel = decode_cel_png(detail::read_file(directory / filename(l,c),16*1024*1024),transparent);
            cel.x = x; cel.y = y;
            loop.cels[c] = std::move(cel);
        }
    }
    std::string extra;
    if (std::getline(input,extra)) throw std::runtime_error("view manifest: unexpected trailing row");
    return write_sci0_view(view);
}

void export_sci0_view_file(const std::filesystem::path& payload, const std::filesystem::path& destination) {
    export_sci0_view(detail::read_file(payload,65535),destination);
}
void import_sci0_view_file(const std::filesystem::path& directory, const std::filesystem::path& payload) {
    const auto bytes = import_sci0_view(directory);
    if (std::filesystem::exists(std::filesystem::symlink_status(payload))) throw std::runtime_error("destination already exists: " + payload.string());
    detail::write_file(payload,bytes);
    if (detail::read_file(payload,65535) != bytes) throw std::runtime_error("saved view verification failed");
}
} // namespace scimate
