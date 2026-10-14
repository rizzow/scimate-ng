// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/view_io.hpp"
#include "files.hpp"
#include "scimate/workspace.hpp"
#include "scimate/output.hpp"
#include "scimate/format_constants.hpp"
#include "scimate/picture_io.hpp"
#include "tables.hpp"
#include <png.h>
#include <array>
#include <charconv>
#include <sstream>

namespace scimate {
namespace {
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
std::vector<std::string> fields(std::istream& input) { return table_detail::row(input, "view manifest"); }
void expect(bool condition) { scimate::require(condition, "view manifest: invalid structure or metadata"); }
int integer(const std::string& s, int min, int max) { return table_detail::integer(s, min, max, "view manifest"); }
} // namespace

static Bytes encode_png(const ViewCel& cel, bool opaque) {
    if (!cel.width || !cel.height || cel.width > format::picture_width || cel.height > format::max_cel_height || (!opaque && cel.transparent > 15) ||
        cel.pixels.size() != static_cast<std::size_t>(cel.width)*cel.height)
        throw std::runtime_error("PNG: invalid cel dimensions/palette");
    for (auto pixel : cel.pixels) if (pixel > 15) throw std::runtime_error("PNG: invalid EGA pixel");
    Png p;
    p.image.width = cel.width; p.image.height = cel.height;
    p.image.format = PNG_FORMAT_RGBA_COLORMAP; p.image.colormap_entries = 16;
    std::array<std::uint8_t,64> palette{};
    for (std::size_t i = 0; i < 16; ++i) {
        for (std::size_t c = 0; c < 3; ++c) palette[i*4+c] = (format::ega_rgb[i] >> (8 * (2-c))) & 255;
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
    if (bytes.size() > format::max_interchange_bytes || (!opaque && transparent > 15)) throw std::runtime_error("PNG: input/palette limit exceeded");
    Png p;
    if (!png_image_begin_read_from_memory(&p.image,bytes.data(),bytes.size())) png_error(p);
    if (!p.image.width || !p.image.height || p.image.width > format::picture_width || p.image.height > format::max_cel_height)
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
            if (rgba[i] == ((format::ega_rgb[static_cast<std::size_t>(c)] >> 16) & 255) && rgba[i+1] == ((format::ega_rgb[static_cast<std::size_t>(c)] >> 8) & 255) && rgba[i+2] == (format::ega_rgb[static_cast<std::size_t>(c)] & 255)) { index = c; break; }
        if (index < 0) throw std::runtime_error("PNG: color is outside the EGA palette (no automatic quantization)");
        if (!opaque && index == transparent) throw std::runtime_error("PNG: opaque pixel uses the designated transparent color");
        cel.pixels.push_back(static_cast<std::uint8_t>(index));
    }
    return cel;
}

Bytes encode_cel_png(const ViewCel& cel) { return encode_png(cel,false); }
ViewCel decode_cel_png(const Bytes& bytes, std::uint8_t transparent) { return decode_png(bytes,transparent,false); }
Bytes encode_ega_png(const ViewCel& image) { return encode_png(image,true); }
ViewCel decode_ega_png(const Bytes& bytes) { return decode_png(bytes,15,true); }
Bytes encode_picture_plane_png(const Bytes& pixels) {
    ViewCel cel; cel.width=format::picture_width; cel.height=format::picture_height; cel.pixels=pixels;
    return encode_png(cel,true);
}
Bytes decode_picture_plane_png(const Bytes& bytes) {
    auto cel=decode_png(bytes,0,true);
    if(cel.width!=format::picture_width || cel.height!=format::picture_height) throw std::runtime_error("picture PNG: dimensions must be 320x190");
    return cel.pixels;
}
VgaPicture decode_vga_picture_visual_png(const Bytes& bytes) {
    if (bytes.size() > format::max_interchange_bytes || bytes.size() < 25 || bytes[24] > 8)
        throw std::runtime_error("VGA picture PNG: input/channel limit exceeded");
    Png p;
    if (!png_image_begin_read_from_memory(&p.image,bytes.data(),bytes.size())) png_error(p);
    if (p.image.width != format::picture_width || p.image.height != format::picture_height)
        throw std::runtime_error("VGA picture PNG: dimensions must be 320x190");
    p.image.format = PNG_FORMAT_RGBA;
    Bytes rgba(PNG_IMAGE_SIZE(p.image));
    if (!png_image_finish_read(&p.image,nullptr,rgba.data(),0,nullptr)) png_error(p);
    VgaPicture result; result.palette = ega_picture_palette();
    unsigned colors = 16;
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        if (rgba[i+3] != 255) throw std::runtime_error("VGA picture PNG: visual plane must be opaque");
        const std::array<std::uint8_t,3> rgb{rgba[i],rgba[i+1],rgba[i+2]};
        unsigned index = 0;
        while (index < colors && result.palette[index] != rgb) ++index;
        if (index == colors) {
            if (colors == 255) throw std::runtime_error("VGA picture PNG: more than 239 additional colours; no automatic quantization");
            result.palette[colors++] = rgb;
        }
        result.picture.planes[0].push_back(static_cast<std::uint8_t>(index));
    }
    return result;
}

void export_sci0_view(const Bytes& payload, const std::filesystem::path& destination) {
    const auto view = read_sci0_view(payload);
    detail::create_destination(destination);
    detail::write_file(destination / workspace_detail::original_file,payload);
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

static ViewCel decode_vga_cel_png(const Bytes& bytes, ViewPalette& palette, unsigned& colors) {
    if (bytes.size() > format::max_interchange_bytes || bytes.size() < 25 || bytes[24] > 8)
        throw std::runtime_error("VGA view PNG: input/channel limit exceeded");
    Png p;
    if (!png_image_begin_read_from_memory(&p.image,bytes.data(),bytes.size())) png_error(p);
    if (!p.image.width || !p.image.height || p.image.width > format::picture_width || p.image.height > format::max_cel_height)
        throw std::runtime_error("VGA view PNG: dimensions must be 1..320 by 1..200");
    p.image.format = PNG_FORMAT_RGBA;
    Bytes rgba(PNG_IMAGE_SIZE(p.image));
    if (!png_image_finish_read(&p.image,nullptr,rgba.data(),0,nullptr)) png_error(p);
    ViewCel cel;
    cel.width = static_cast<std::uint16_t>(p.image.width); cel.height = static_cast<std::uint16_t>(p.image.height);
    cel.transparent = 255;
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        if (!rgba[i+3]) { cel.pixels.push_back(255); continue; }
        if (rgba[i+3] != 255) throw std::runtime_error("VGA view PNG: partial alpha is not representable");
        const std::array<std::uint8_t,3> rgb{rgba[i],rgba[i+1],rgba[i+2]};
        unsigned index = 0;
        while (index < colors && palette[index] != rgb) ++index;
        if (index == colors) {
            if (colors == 255) throw std::runtime_error("VGA view PNG: more than 239 additional colours across cels; no automatic quantization");
            palette[colors++] = rgb;
        }
        cel.pixels.push_back(static_cast<std::uint8_t>(index));
    }
    return cel;
}

static View import_view(const std::filesystem::path& directory, bool vga) {
    auto view = read_sci0_view(detail::read_file(directory / workspace_detail::original_file,65535));
    const auto images = vga && std::filesystem::exists(directory / "vga" / "view.tsv") ? directory / "vga" : directory;
    const auto manifest = detail::read_file(images / "view.tsv",1024*1024);
    if (images != directory && manifest != detail::read_file(directory / "view.tsv",1024*1024))
        throw std::runtime_error("VGA view manifest must match the parent view metadata");
    if (vga) view.palette = ega_picture_palette();
    unsigned colors = 16;
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
            const auto path = images / filename(l,c);
            ViewCel cel;
            try {
                const auto png = detail::read_file(path,format::max_interchange_bytes);
                cel = vga ? decode_vga_cel_png(png,*view.palette,colors) : decode_cel_png(png,transparent);
            } catch (const std::exception& error) {
                throw std::runtime_error(path.string() + ": " + error.what());
            }
            cel.x = x; cel.y = y;
            loop.cels[c] = std::move(cel);
        }
    }
    table_detail::end(input, "view manifest");
    return view;
}

Bytes import_sci0_view(const std::filesystem::path& directory) { return write_sci0_view(import_view(directory,false)); }
View import_vga_view_edit(const std::filesystem::path& directory) { return import_view(directory,true); }

void export_sci0_view_file(const std::filesystem::path& payload, const std::filesystem::path& destination) {
    export_sci0_view(detail::read_file(payload,65535),destination);
}
void import_sci0_view_file(const std::filesystem::path& directory, const std::filesystem::path& payload) {
    const auto bytes = import_sci0_view(directory);
    write_new_file(payload, bytes);
}
} // namespace scimate
