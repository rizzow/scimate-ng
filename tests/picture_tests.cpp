// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_fixtures.hpp"
#include "scimate/picture_io.hpp"
#include "scimate/game_io.hpp"
#include "scimate/view_io.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
namespace {
using namespace fixture;
namespace fs=std::filesystem;
void write(const fs::path& p,const Bytes& b) {std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());require(bool(f),"write failed");}
const Bytes source{0xf0,4,0xf2,6,0xfb,3,0xf6,0,10,10,0,20,10,0,20,20,0,10,20,0,10,10,0xf8,0,15,15,0xff};
void tests(const fs::path& root) {
 auto picture=read_sci0_picture(source);
 require(picture.planes[0][15*320+15]==4 && picture.planes[1][15*320+15]==6 && picture.planes[2][15*320+15]==3,"enclosed three-plane fill");
 require(picture.planes[0][0]==15 && picture.planes[1][0]==0,"initial planes");
 require(picture.written[0][0] == 0 && picture.written[0][15*320+15] == 1 &&
         picture.written[1][15*320+15] == 1 && picture.contains_fill,"source drawing masks lost fill writes");
 const auto zero_overlay = read_sci0_picture({0xf0,39,0xf2,0,0xfb,0,0xf6,0,0,0,0,2,0,0xff});
 require(zero_overlay.written[0][0] == 1 && zero_overlay.written[1][0] == 1 &&
         zero_overlay.written[2][0] == 1 && zero_overlay.written[0][3] == 0,
         "Explicit white/zero writes were mistaken for untouched pixels");
 const auto vga_overlay = write_sci1_vga_picture(zero_overlay,ega_picture_palette(),VgaPictureMode::overlay);
 const Bytes overlay_commands{0xf1,0xf3,0xfc,0xf0,15,0xf6,0,0,0,0,2,0,
     0xf1,0xf3,0xfc,0xf2,0,0xf6,0,0,0,0,2,0,
     0xf1,0xf3,0xfc,0xfb,0,0xf6,0,0,0,0,2,0,0xff};
 constexpr std::size_t vga_palette_bytes = 2+256+4+256*4;
 auto checkerboard = zero_overlay;
 for (unsigned plane = 0; plane < 3; ++plane) {
  checkerboard.planes[plane].assign(320*190,plane == 0 ? 15 : 0);
  checkerboard.written[plane].assign(320*190,0);
 }
 for (unsigned y = 0; y < 190; ++y) for (unsigned x = 0; x < 320; ++x) {
  checkerboard.planes[1][y*320+x] = (x+y)%2;
  checkerboard.written[1][y*320+x] = 1;
 }
 const auto compact_checkerboard = write_sci1_vga_picture(checkerboard,ega_picture_palette(),VgaPictureMode::overlay);
 require(compact_checkerboard.size() < 10000,"Checkerboard mask expanded into individual pixel lines");
 const auto decoded_checkerboard = read_sci0_picture(Bytes(compact_checkerboard.begin()+vga_palette_bytes,compact_checkerboard.end()));
 require(decoded_checkerboard.planes == checkerboard.planes && decoded_checkerboard.written == checkerboard.written,
         "Compact diagonal runs changed mask pixels or explicit zero writes");
 require(Bytes(vga_overlay.begin()+vga_palette_bytes,vga_overlay.end()) == overlay_commands,
         "VGA overlay did not preserve untouched pixels and explicit zero mask writes");
 fails([&] { write_sci1_vga_picture(picture,ega_picture_palette(),VgaPictureMode::overlay); },"background-dependent");
 require(write_sci0_picture(picture)==source,"unchanged vector preservation");
 // Diagonal rounding follows Sierra's line algorithm, including reversed lines.
 auto diagonal=read_sci0_picture({0xf0,1,0xf6,0,0,0,0,4,2,0xff});
 require(diagonal.planes[0][320+1]==1 && diagonal.planes[0][0*320+1]==15,"line rounding");
 auto dither=read_sci0_picture({0xf0,25,0xf6,0,0,0,0,3,0,0xff});
 require(dither.planes[0][0]==9 && dither.planes[0][1]==15,"dither palette");
 auto pen=read_sci0_picture({0xf0,2,0xf9,0,0xfa,0,5,5,0xff});
 require(pen.planes[0][5*320+5]==2 && pen.planes[0][5*320+6]==15,"size zero circle");
 fails([] {read_sci0_picture({0xf6,0,10});},"truncated");
 fails([] {read_sci0_picture({0xfe,7,0xff});},"unsupported");
 fails([] {read_sci0_picture({0xf0,40,0xff});},"palette index");
 for(std::size_t size=0;size<source.size();++size) fails([&] {read_sci0_picture(Bytes(source.begin(),source.begin()+size));},"picture:");
 const auto bundle=root/"bundle";
 export_sci0_picture(source,bundle);
 require(import_sci0_picture(bundle)==source,"PNG unchanged roundtrip");
 // Plane PNGs require fixed dimensions, exact EGA colors and opaque pixels.
 ViewCel alpha;alpha.width=320;alpha.height=190;alpha.transparent=0;alpha.pixels=Bytes(320*190,0);
 fails([&] {decode_picture_plane_png(encode_cel_png(alpha));},"alpha");
 alpha.width=1;alpha.height=1;alpha.pixels={1};
 fails([&] {decode_picture_plane_png(encode_cel_png(alpha));},"dimensions");
 auto invalid=picture.planes[0];invalid[0]=16;
 fails([&] {encode_picture_plane_png(invalid);},"EGA pixel");
 auto too_large=picture;
 for(int y=0;y<190;++y) for(int x=0;x<320;++x) too_large.planes[0][y*320+x]=(x+y)%2;
 fails([&] {write_sci0_picture(too_large);},"payload limit");

 for(int plane=0;plane<3;++plane) picture.planes[plane][0]=plane+1;
 write(bundle/"visual.png",encode_picture_plane_png(picture.planes[0]));
 write(bundle/"priority.png",encode_picture_plane_png(picture.planes[1]));
 write(bundle/"control.png",encode_picture_plane_png(picture.planes[2]));
 auto edited=import_sci0_picture(bundle);
 require(read_sci0_picture(edited).planes==picture.planes,"all-plane edit roundtrip");
 require(std::equal(source.begin(),source.end()-1,edited.begin()),"original vector prefix retained");
 // Pure white and zero mask values must be writable too.
 picture.planes[0][15*320+15]=15;picture.planes[1][15*320+15]=0;picture.planes[2][15*320+15]=0;
 require(read_sci0_picture(write_sci0_picture(picture)).planes==picture.planes,"erase pixels and masks");
 fails([&] {export_sci0_picture(source,bundle);},"already exists");
 import_sci0_picture_file(bundle,root/"edited.bin");
 fails([&] {import_sci0_picture_file(bundle,root/"edited.bin");},"already exists");
 write(bundle/"picture.tsv",{});fails([&] {import_sci0_picture(bundle);},"manifest");
 auto files=GameFiles{};record(files,0x0801,1,0,source.size(),source);record(files,0x0801,1,0,1,{0});files.map.insert(files.map.end(),6,255);
 fs::create_directory(root/"game");write(root/"game/resource.map",files.map);write(root/"game/resource.001",files.volumes[1]);
 auto summary=export_sci0_game(root/"game",root/"export");
 require(summary.editable_pictures==1 && summary.picture_errors==1,"picture diagnostics");
 import_sci0_game(root/"export",root/"unchanged");
 require(open_sci0_game(root/"unchanged").original.volumes==files.volumes,"unchanged game bytes");
 write(root/"export/pictures/0000000/priority.png",encode_picture_plane_png(picture.planes[1]));
 import_sci0_game(root/"export",root/"rebuilt");
 require(read_sci0_picture(open_sci0_game(root/"rebuilt").resources[0].payload).planes[1]==picture.planes[1],"game mask edits");
 write(root/"export/payloads/0000000.bin",{0xff});
 fails([&] {import_sci0_game(root/"export",root/"conflict");},"conflicting");
 require(!fs::exists(root/"conflict"),"conflict created destination");
 write(root/"export/pictures/0000000/original.bin",{0xff});
 fails([&] {import_sci0_game(root/"export",root/"snapshot");},"snapshot");
}
}
int main(int argc,char** argv) {
 try {
  if(argc==3 && std::string(argv[1])=="--write-fixture") { write(argv[2],source);return 0; }
  require(argc==2,"expected fixture directory");
  const fs::path root=fs::path(argv[1])/("run-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root);tests(root);fs::remove_all(root);
  std::cout<<"SCI0 picture tests passed\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
