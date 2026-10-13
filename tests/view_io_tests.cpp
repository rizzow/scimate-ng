// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_fixtures.hpp"
#include "view_fixture.hpp"
#include "scimate/game_io.hpp"
#include "scimate/view_io.hpp"
#include <png.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
using namespace fixture;
namespace fs = std::filesystem;
void save(const fs::path& path, const Bytes& bytes) {
    std::ofstream out(path,std::ios::binary);
    for (auto byte : bytes) out.put(static_cast<char>(byte));
    out.close(); require(static_cast<bool>(out),"test write failed");
}
Bytes read(const fs::path& path) {
    std::ifstream in(path,std::ios::binary); require(static_cast<bool>(in),"test read failed");
    return Bytes(std::istreambuf_iterator<char>(in),{});
}
Bytes rgba_png(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    png_image image{}; image.version = PNG_IMAGE_VERSION;
    image.width=1; image.height=1; image.format=PNG_FORMAT_RGBA;
    const unsigned char rgba[]{r,g,b,a};
    png_alloc_size_t size=0;
    require(png_image_write_to_memory(&image,nullptr,&size,0,rgba,0,nullptr)!=0,"test RGBA PNG size");
    Bytes result(size);
    require(png_image_write_to_memory(&image,result.data(),&size,0,rgba,0,nullptr)!=0,"test RGBA PNG");
    result.resize(size); png_image_free(&image); return result;
}
void create_game(const fs::path& dir) {
    require(fs::create_directory(dir),"fixture directory exists");
    GameFiles files;
    record(files,1,1,0,static_cast<unsigned>(view_bytes.size()),view_bytes);
    record(files,1,2,0,static_cast<unsigned>(view_bytes.size()),view_bytes); // Duplicate ID.
    record(files,2,1,0,1,{0}); // Unsupported view gets an error sidecar.
    files.map.insert(files.map.end(),6,0xff);
    save(dir/"resource.map",files.map);
    for (const auto& v : files.volumes) save(dir/volume_filename(v.first),v.second);
}
void tests(const fs::path& root) {
    const auto view=read_sci0_view(view_bytes);
    const auto png=encode_cel_png(view.loops[0].cels[0]);
    require(png[25]==3,"export PNG is not indexed");
    auto decoded=decode_cel_png(png,0); decoded.x=-2; decoded.y=3;
    require(decoded==view.loops[0].cels[0],"PNG pixel/alpha/orientation roundtrip");
    for (std::uint8_t transparent=0; transparent<16; ++transparent) {
        ViewCel all; all.width=16; all.height=1; all.transparent=transparent;
        for (std::uint8_t color=0; color<16; ++color) all.pixels.push_back(color);
        require(decode_cel_png(encode_cel_png(all),transparent)==all,"full EGA palette/transparency mapping");
    }
    require(decode_cel_png(rgba_png(0,170,0,255),0).pixels==Bytes{2},"RGBA exact EGA mapping");
    require(decode_cel_png(rgba_png(123,45,67,0),0).pixels==Bytes{0},"transparent RGB ignored");
    fails([&] { decode_cel_png(rgba_png(0,170,0,128),0); },"partial alpha");
    fails([&] { decode_cel_png(rgba_png(1,2,3,255),0); },"outside the EGA");
    fails([&] { decode_cel_png(rgba_png(0,0,0,255),0); },"opaque pixel");
    fails([&] { decode_cel_png({1,2,3},0); },"PNG");
    for (std::size_t cut : {0u,8u,20u,40u})
        fails([&] { decode_cel_png(Bytes(png.begin(),png.begin()+static_cast<std::ptrdiff_t>(cut)),0); },"PNG");

    const auto bundle=root/"view";
    export_sci0_view(view_bytes,bundle);
    require(import_sci0_view(bundle)==view_bytes,"unchanged editable view not exact");
    auto edit=view.loops[0].cels[0]; edit.width=2; edit.height=1; edit.pixels={2,0};
    save(bundle/"loop-0-cel-0.png",encode_cel_png(edit));
    auto manifest=read(bundle/"view.tsv");
    std::string text(manifest.begin(),manifest.end());
    const auto at=text.find("\t-2\t3\t0\t"); require(at!=std::string::npos,"metadata fixture");
    text.replace(at,8,"\t-5\t7\t0\t");
    save(bundle/"view.tsv",Bytes(text.begin(),text.end()));
    const auto edited=read_sci0_view(import_sci0_view(bundle));
    require(edited.loops[0].cels[0].pixels==edit.pixels && edited.loops[0].cels[0].width==2 &&
            edited.loops[0].cels[0].x==-5 && edited.loops[0].cels[0].y==7,"edited pixels/dimensions/placement");
    require(edited.loops[1].mirror_of==0,"mirror lost");
    auto invalid=text; invalid.replace(invalid.find("loop-0-cel-0.png"),16,"../../bad.png");
    save(bundle/"view.tsv",Bytes(invalid.begin(),invalid.end()));
    fails([&] { import_sci0_view(bundle); },"manifest");
    save(bundle/"view.tsv",Bytes(text.begin(),text.end()));
    fs::remove(bundle/"loop-0-cel-0.png");
    fails([&] { import_sci0_view(bundle); },"");

    create_game(root/"game");
    const auto summary=export_sci0_game(root/"game",root/"export");
    require(summary.editable_views==2 && summary.view_errors==1,"view export counts");
    import_sci0_game(root/"export",root/"unchanged");
    require(read(root/"game/resource.map")==read(root/"unchanged/resource.map") &&
            read(root/"game/resource.001")==read(root/"unchanged/resource.001"),"game exact PNG roundtrip");
    save(root/"export/views/0000000/loop-0-cel-0.png",encode_cel_png(edit));
    import_sci0_game(root/"export",root/"edited");
    const auto game=open_sci0_game(root/"edited");
    require(read_sci0_view(game.resources[0].payload).loops[0].cels[0].pixels==edit.pixels,"game view edit missing");
    require(game.resources[1].payload==view_bytes,"duplicate view changed");
    save(root/"export/payloads/0000000.bin",{1,2,3});
    fails([&] { import_sci0_game(root/"export",root/"conflict"); },"conflicting");
    require(!fs::exists(root/"conflict"),"conflict created output");
    save(root/"export/payloads/0000000.bin",view_bytes);
    save(root/"export/views/0000000/original.bin",{1,2,3});
    fails([&] { import_sci0_game(root/"export",root/"snapshot"); },"snapshot differs");
    require(!fs::exists(root/"snapshot"),"snapshot mismatch created output");
}
} // namespace
int main(int argc,char** argv) {
    try {
        if (argc==3 && std::string(argv[1])=="--write-fixture") { create_game(argv[2]); return 0; }
        if (argc==3 && std::string(argv[1])=="--edit-png") { save(argv[2],rgba_png(0,170,0,255)); return 0; }
        require(argc==2,"expected fixture parent");
        fs::create_directories(argv[1]);
        const auto root=fs::path(argv[1])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        require(fs::create_directory(root),"test directory exists");
        tests(root); fs::remove_all(root);
        std::cout<<"View PNG workflow tests passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
