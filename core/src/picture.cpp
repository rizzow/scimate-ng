// SPDX-License-Identifier: GPL-2.0-or-later
// Pattern tables and line rasterization adapted from SCI Companion PicCommands.cpp.
// Copyright (c) 2015 Philip Fortier
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later version.
// Distributed without any warranty; see COPYING.
// Based on SCI Studio by Brian Provinciano.
#include "scimate/picture.hpp"
#include "scimate/format_constants.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
namespace scimate {
namespace {
constexpr int width = format::picture_width, height = format::picture_height, pixels = width*height;
const std::uint8_t default_palette[40] = {
 0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0x88,
 0x88,0x01,0x02,0x03,0x04,0x05,0x06,0x88,0x88,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff,
 0x08,0x19,0x2a,0x3b,0x4c,0x5d,0x6e,0x88};
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
const std::uint8_t circles[][30] = { /* bitmaps for circle patterns */
{0x80},
{0x4e, 0x40},
{0x73, 0xef, 0xbe, 0x70},
{0x38, 0x7c, 0xfe, 0xfe, 0xfe, 0x7c, 0x38, 0x00},
{0x1c, 0x1f, 0xcf, 0xfb, 0xfe, 0xff, 0xbf, 0xef, 0xf9, 0xfc, 0x1c},
{0x0e, 0x03, 0xf8, 0x7f, 0xc7, 0xfc, 0xff, 0xef, 0xfe, 0xff, 0xe7,
    0xfc, 0x7f, 0xc3, 0xf8, 0x1f, 0x00},
{0x0f, 0x80, 0xff, 0x87, 0xff, 0x1f, 0xfc, 0xff, 0xfb, 0xff, 0xef,
    0xff, 0xbf, 0xfe, 0xff, 0xf9, 0xff, 0xc7, 0xff, 0x0f, 0xf8,
    0x0f, 0x80},
{0x07, 0xc0, 0x1f, 0xf0, 0x3f, 0xf8, 0x7f, 0xfc, 0x7f, 0xfc, 0xff,
    0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0x7f,
    0xfc, 0x7f, 0xfc, 0x3f, 0xf8, 0x1f, 0xf0, 0x07, 0xc0}};
const std::uint8_t junq[32] = { /* random-looking fill pattern */
0x20, 0x94, 0x02, 0x24, 0x90, 0x82, 0xa4, 0xa2, 0x82, 0x09, 0x0a, 0x22,
0x12, 0x10, 0x42, 0x14, 0x91, 0x4a, 0x91, 0x11, 0x08, 0x12, 0x25, 0x10,
0x22, 0xa8, 0x14, 0x24, 0x00, 0x50, 0x24, 0x04};
const std::uint8_t junqindex[128] = { /* starting points for junq fill */
0x00, 0x18, 0x30, 0xc4, 0xdc, 0x65, 0xeb, 0x48,
0x60, 0xbd, 0x89, 0x05, 0x0a, 0xf4, 0x7d, 0x7d,
0x85, 0xb0, 0x8e, 0x95, 0x1f, 0x22, 0x0d, 0xdf,
0x2a, 0x78, 0xd5, 0x73, 0x1c, 0xb4, 0x40, 0xa1,
0xb9, 0x3c, 0xca, 0x58, 0x92, 0x34, 0xcc, 0xce,
0xd7, 0x42, 0x90, 0x0f, 0x8b, 0x7f, 0x32, 0xed,
0x5c, 0x9d, 0xc8, 0x99, 0xad, 0x4e, 0x56, 0xa6,
0xf7, 0x68, 0xb7, 0x25, 0x82, 0x37, 0x3a, 0x51,
0x69, 0x26, 0x38, 0x52, 0x9e, 0x9a, 0x4f, 0xa7,
0x43, 0x10, 0x80, 0xee, 0x3d, 0x59, 0x35, 0xcf,
0x79, 0x74, 0xb5, 0xa2, 0xb1, 0x96, 0x23, 0xe0,
0xbe, 0x05, 0xf5, 0x6e, 0x19, 0xc5, 0x66, 0x49,
0xf0, 0xd1, 0x54, 0xa9, 0x70, 0x4b, 0xa4, 0xe2,
0xe6, 0xe5, 0xab, 0xe4, 0xd2, 0xaa, 0x4c, 0xe3,
0x06, 0x6f, 0xc6, 0x4a, 0xa4, 0x75, 0x97, 0xe1
};


struct Renderer {
 const Bytes& data; std::size_t pos = 0;
 Picture pic;
 int color = -1, priority = -1, control = -1, pattern = 0;
 std::array<std::uint8_t,160> palette{};
 std::size_t work = 0;
 explicit Renderer(const Bytes& bytes): data(bytes) {
  pic.planes = {Bytes(pixels,15),Bytes(pixels,0),Bytes(pixels,0)};
  for (auto& plane : pic.written) plane.assign(pixels,0);
  for (int i=0;i<160;++i) palette[i]=default_palette[i%40];
 }
 int get() { check(pos<data.size(), "picture: truncated command"); return data[pos++]; }
 bool more() { check(pos<data.size(), "picture: missing terminator"); return data[pos]<0xf0; }
 void coords(int& x,int& y,int mode=0) {
  int b=get();
  if (!mode) { x=((b&0xf0)<<4)+get(); y=((b&15)<<8)+get(); }
  else if (mode==1) { x+=(b&128)?-((b>>4)&7):(b>>4); y+=(b&8)?-(b&7):(b&7); }
  else { y+=(b&128)?-(b&127):b; b=get(); x+=b>=128?b-256:b; }
  check(x>=-4096 && x<=4095 && y>=-4096 && y<=4095,"picture: coordinate limit exceeded");
 }
 void plot(int x,int y) {
  if (x<0 || x>=width || y<0 || y>=height) return;
  check(++work<=64*1024*1024,"picture: rendering work limit exceeded");
  int i=y*width+x;
  if (color>=0) { pic.planes[0][i]=((x^y)&1)?color>>4:color&15; pic.written[0][i]=1; }
  if (priority>=0) { pic.planes[1][i]=priority; pic.written[1][i]=1; }
  if (control>=0) { pic.planes[2][i]=control; pic.written[2][i]=1; }
 }
 void line(int x,int y,int endx,int endy) {
  int dx=std::abs(endx-x),dy=std::abs(endy-y),sx=endx<x?-1:1,sy=endy<y?-1:1;
  int major=std::max(dx,dy),minor=std::min(dx,dy),err=major-1;
  for (int n=0;n<=major;++n) {
   plot(x,y);
   if (dx>dy) x+=sx; else y+=sy;
   err-=2*minor;
   if (err<0) { err+=2*major; if(dx>dy) y+=sy; else x+=sx; }
  }
 }
 void fill(int x,int y) {
  pic.contains_fill = true;
  if(x<0 || x>=width || y<0 || y>=height) return;
  int plane=color>=0?0:priority>=0?1:control>=0?2:-1;
  if (plane<0 || (plane==0 && color==255) || (plane==1 && priority==0) || (plane==2 && control==0)) return;
  auto& mask=pic.planes[plane]; int empty=plane==0?15:0;
  if(mask[y*width+x]!=empty) return;
  Bytes visited(pixels,0); std::vector<int> pending{y*width+x}; visited[pending[0]]=1;
  while(!pending.empty()) {
   int i=pending.back(); pending.pop_back();
   if(mask[i]!=empty) continue;
   int xx=i%width,yy=i/width; plot(xx,yy);
   for(int next : {xx>0?i-1:-1,xx<width-1?i+1:-1,yy>0?i-width:-1,yy<height-1?i+width:-1})
    if(next>=0 && !visited[next]) {visited[next]=1;pending.push_back(next);}
  }
 }
 void pen(int x,int y,int texture) {
  int size=pattern&7;
  x=std::clamp(x,size,width-1-size); y=std::clamp(y,size,height-1-size);
  int bit=0,tex=junqindex[texture];
  for(int yy=y-size;yy<=y+size;++yy) for(int xx=x-size;xx<=x+size+1;++xx,++bit) {
   if(!(pattern&16) && !(circles[size][bit/8] & (128>>(bit%8)))) continue;
   if(!(pattern&32) || (junq[tex/8] & (128>>(tex%8)))) plot(xx,yy);
   if(pattern&32) { if(++tex==255) tex=0; }
  }
 }
 Picture run() {
  check(!data.empty() && data.size()<=65535,"picture: invalid payload size");
  for (;;) {
   int op=get(),x=0,y=0;
   switch(op) {
    case 0xf0: {int index=get();check(index<40,"picture: invalid palette index");color=palette[index];break;}
    case 0xf1: color=-1;break;
    case 0xf2: priority=get();check(priority<16,"picture: invalid priority");break;
    case 0xf3: priority=-1;break;
    case 0xfb: control=get();check(control<16,"picture: invalid control");break;
    case 0xfc: control=-1;break;
    case 0xf9: pattern=get();check((pattern&~0x37)==0,"picture: invalid pattern");break;
    case 0xf5: case 0xf6: case 0xf7:
     coords(x,y); while(more()) {int a=x,b=y;coords(x,y,op==0xf6?0:op==0xf7?1:2);line(a,b,x,y);} break;
    case 0xf8: while(more()) {coords(x,y);fill(x,y);} break;
    case 0xf4: case 0xfa: case 0xfd: {
     bool first=true;
     while(more()) {
      int tex=(pattern&32)?(get()>>1)&127:0;
      coords(x,y,first || op==0xfa?0:op==0xf4?1:2); pen(x,y,tex);first=false;
     } break;
    }
    case 0xfe: {
     int sub=get();
     if(sub==0) {while(more()) {int index=get();check(index<160,"picture: invalid palette entry");palette[index]=get();}}
     else if(sub==1) {int index=get();check(index<4,"picture: invalid palette");for(int i=0;i<40;++i)palette[index*40+i]=get();}
     else if(sub>=2 && sub<=6) {int count=sub==2?41:(sub==3 || sub==5)?1:0;while(count--)get();}
     else check(false,"picture: unsupported extended command");
     break;
    }
    case 0xff: pic.original=data;pic.terminator=pos-1;return pic;
    default: check(false,"picture: invalid opcode");
   }
  }
 }
};
void coordinate(Bytes& bytes,int x,int y) {
 bytes.push_back(((x>>8)<<4)|(y>>8));bytes.push_back(x&255);bytes.push_back(y&255);
}
}
Picture read_sci0_picture(const Bytes& payload) { return Renderer(payload).run(); }
Bytes write_sci0_picture(const Picture& picture) {
 const auto original=read_sci0_picture(picture.original);
 for(const auto& plane:picture.planes) {
  check(plane.size()==pixels,"picture: planes must be 320x190");
  for(auto value:plane) check(value<16,"picture: invalid plane index");
 }
 if(picture.planes==original.planes) return picture.original;
 Bytes result(picture.original.begin(),picture.original.begin()+original.terminator);
 // Explicit palettes make the appended colors independent of the DrawPic palette.
 for(int palette=0;palette<4;++palette) {
  result.insert(result.end(),{0xfe,1,static_cast<std::uint8_t>(palette)});
  for(int i=0;i<40;++i) result.push_back(i<16?i*17:default_palette[i]);
 }
 for(int plane=0;plane<3;++plane) {
  result.insert(result.end(),{0xf1,0xf3,0xfc});
  for(int value=0;value<16;++value) {
   bool selected=false;
   for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
    int i=y*width+x;
    if(picture.planes[plane][i]!=value || picture.planes[plane][i]==original.planes[plane][i]) continue;
    int end=x;
    while(end+1<width && picture.planes[plane][y*width+end+1]==value && picture.planes[plane][y*width+end+1]!=original.planes[plane][y*width+end+1]) ++end;
    if(!selected) {result.push_back(plane==0?0xf0:plane==1?0xf2:0xfb);result.push_back(value);selected=true;}
    result.push_back(0xf6);coordinate(result,x,y);coordinate(result,end,y);x=end;
    check(result.size()<65535,"picture: edits exceed SCI0 payload limit");
   }
  }
 }
 result.push_back(0xff);
 result.insert(result.end(),picture.original.begin()+original.terminator+1,picture.original.end());
 check(result.size()<=65535,"picture: edits exceed SCI0 payload limit");
 check(read_sci0_picture(result).planes==picture.planes,"picture: edited plane verification failed");
 return result;
}

namespace vga_picture {
constexpr std::uint8_t extended = 0xfe, palette = 2, bitmap = 1, terminate = 0xff;
constexpr std::uint8_t disable_visual = 0xf1, disable_priority = 0xf3, disable_control = 0xfc;
constexpr std::uint8_t set_priority = 0xf2, set_control = 0xfb, absolute_line = 0xf6;
constexpr std::uint8_t set_visual = 0xf0;
constexpr std::uint8_t repeat_run = 0x80, clear_color = 255;
constexpr std::size_t max_literal_run = 127, max_repeat_run = 63, cel_header_size = 8;
void word(Bytes& bytes, std::size_t value) {
 check(value <= 65535,"VGA picture: word exceeds format limit");
 bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
Bytes encode_bitmap(const Bytes& visual) {
 Bytes bytes;
 auto run = [&](std::size_t start) {
  std::size_t count = 1;
  while (start+count < visual.size() && count < max_repeat_run && visual[start+count] == visual[start]) ++count;
  return count;
 };
 for (std::size_t cursor = 0; cursor < visual.size();) {
  const auto repeated = run(cursor);
  if (repeated >= 3) {
   bytes.push_back(static_cast<std::uint8_t>(repeat_run | repeated)); bytes.push_back(visual[cursor]); cursor += repeated;
  } else {
   const auto begin = cursor++;
   while (cursor < visual.size() && cursor-begin < max_literal_run && run(cursor) < 3) ++cursor;
   bytes.push_back(static_cast<std::uint8_t>(cursor-begin));
   bytes.insert(bytes.end(),visual.begin()+begin,visual.begin()+cursor);
  }
 }
 return bytes;
}
void append_vector_plane(Bytes& bytes, const Bytes& plane, std::uint8_t set_color,
                         unsigned colors, const Bytes* written = nullptr) {
 const auto draws = [&](std::size_t offset, unsigned value) {
  return plane[offset] == value && (written ? (*written)[offset] != 0 : value != 0);
 };
 std::array<bool,256> active{};
 for (std::size_t i = 0; i < plane.size(); ++i)
  if (draws(i,plane[i])) active[plane[i]] = true;
 Bytes best;
 // All four directions rasterize exact straight pixel runs. Diagonals compact
 // EGA checkerboard dithering without painting intervening background pixels.
 for (const auto direction : {std::pair{1,0},std::pair{0,1},std::pair{1,1},std::pair{1,-1}}) {
 Bytes candidate{disable_visual,disable_priority,disable_control};
 for (unsigned value = 0; value < colors; ++value) {
  if (!active[value]) continue;
  bool selected = false;
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
   if (!draws(y*width+x,value)) continue;
   const auto inside = [](int px, int py) { return px >= 0 && px < width && py >= 0 && py < height; };
   const int previous_x = x-direction.first, previous_y = y-direction.second;
   if (inside(previous_x,previous_y) && draws(previous_y*width+previous_x,value)) continue;
   int end_x = x, end_y = y;
   while (inside(end_x+direction.first,end_y+direction.second) &&
          draws((end_y+direction.second)*width+end_x+direction.first,value)) {
    end_x += direction.first; end_y += direction.second;
   }
   if (!selected) { candidate.push_back(set_color); candidate.push_back(static_cast<std::uint8_t>(value)); selected = true; }
   candidate.push_back(absolute_line); coordinate(candidate,x,y); coordinate(candidate,end_x,end_y);
  }
 }
 if (best.empty() || candidate.size() < best.size()) best = std::move(candidate);
 }
 bytes.insert(bytes.end(),best.begin(),best.end());
}
}

PicturePalette ega_picture_palette() {
 PicturePalette palette{};
 for (unsigned value = 0; value < 16; ++value) {
  const auto bright = value & 8 ? 85u : 0u;
  palette[value] = {static_cast<std::uint8_t>((value & 4 ? 170 : 0)+bright),
                    static_cast<std::uint8_t>((value & 2 ? 170 : 0)+bright),
                    static_cast<std::uint8_t>((value & 1 ? 170 : 0)+bright)};
 }
 palette[6] = {170,85,0};
 palette[vga_picture::clear_color] = {255,255,255};
 return palette;
}

static Bytes encode_vga_picture(const Picture& picture, const PicturePalette& palette, VgaPictureMode mode) {
 using namespace vga_picture;
 for (const auto& plane : picture.planes) check(plane.size() == pixels,"VGA picture: planes must be 320x190");
 for (const auto pixel : picture.planes[0]) check(pixel != clear_color,"VGA picture: visual index 255 is reserved for transparency");
 for (std::size_t plane = 1; plane < picture.planes.size(); ++plane)
  for (const auto pixel : picture.planes[plane]) check(pixel < 16,"VGA picture: invalid priority/control index");
 if (mode == VgaPictureMode::overlay) {
  check(!picture.contains_fill,"VGA picture: overlay has background-dependent flood fills");
  for (const auto& plane : picture.written) {
   check(plane.size() == pixels,"VGA picture: overlay requires explicit drawing masks");
   for (const auto pixel : plane) check(pixel <= 1,"VGA picture: invalid drawing mask");
  }
 }
 Bytes result{extended,vga_picture::palette};
 for (unsigned index = 0; index < 256; ++index) result.push_back(static_cast<std::uint8_t>(index));
 result.insert(result.end(),4,0); // Palette timestamp.
 for (const auto& color : palette) { result.push_back(1); result.insert(result.end(),color.begin(),color.end()); }
 if (mode == VgaPictureMode::background) {
  const auto encoded = encode_bitmap(picture.planes[0]);
  result.insert(result.end(),{extended,bitmap}); coordinate(result,0,0);
  vga_picture::word(result,encoded.size()+cel_header_size);
  vga_picture::word(result,width); vga_picture::word(result,height);
  result.insert(result.end(),{0,0,clear_color,0});
  result.insert(result.end(),encoded.begin(),encoded.end());
  append_vector_plane(result,picture.planes[1],set_priority,16);
  append_vector_plane(result,picture.planes[2],set_control,16);
 } else {
  append_vector_plane(result,picture.planes[0],set_visual,clear_color,&picture.written[0]);
  append_vector_plane(result,picture.planes[1],set_priority,16,&picture.written[1]);
  append_vector_plane(result,picture.planes[2],set_control,16,&picture.written[2]);
 }
 result.push_back(terminate);
 return result;
}
Bytes write_sci1_vga_picture(const Picture& picture, const PicturePalette& palette, VgaPictureMode mode) {
 auto result = encode_vga_picture(picture,palette,mode);
 if (result.size() > 65531)
  throw std::runtime_error("VGA picture: encoded payload is "+std::to_string(result.size())+
      " bytes; SCI1 resource limit is 65531 bytes");
 const auto decoded = read_sci1_vga_picture(result);
 if (decoded.palette != palette) throw std::runtime_error("VGA picture: palette verification failed");
 for (unsigned plane = 0; plane < 3; ++plane) for (int i = 0; i < pixels; ++i) {
  if (mode == VgaPictureMode::overlay && !picture.written[plane][i]) {
   if (decoded.picture.written[plane][i]) throw std::runtime_error("VGA picture: overlay writes an untouched pixel");
  } else if (decoded.picture.planes[plane][i] != picture.planes[plane][i] ||
             (mode == VgaPictureMode::overlay && !decoded.picture.written[plane][i]))
   throw std::runtime_error("VGA picture: decoded pixel verification failed");
 }
 return result;
}
std::vector<Bytes> write_sci1_vga_picture_parts(const Picture& picture, const PicturePalette& palette, VgaPictureMode mode) {
 auto whole = encode_vga_picture(picture,palette,mode);
 constexpr std::size_t payload_limit = 65531;
 if (whole.size() <= payload_limit) return {write_sci1_vga_picture(picture,palette,mode)};
 std::vector<Bytes> parts;
 auto overlay = picture;
 overlay.contains_fill = false;
 if (mode == VgaPictureMode::background) {
  auto background = picture;
  background.planes[1].assign(pixels,0); background.planes[2].assign(pixels,0);
  parts.push_back(write_sci1_vga_picture(background,palette,VgaPictureMode::background));
  overlay.written[0].assign(pixels,0);
  for (unsigned plane = 1; plane < 3; ++plane) {
   overlay.written[plane].resize(pixels);
   for (int i = 0; i < pixels; ++i) overlay.written[plane][i] = picture.planes[plane][i] != 0;
  }
 }
 // Eight rows bound even the worst case of three independently scattered
 // planes: 8*320*3 seven-byte lines plus palette and opcode overhead.
 constexpr int rows_per_part = 8;
 for (int first = 0; first < height; first += rows_per_part) {
  auto band = overlay;
  bool draws = false;
  for (auto& mask : band.written) for (int i = 0; i < pixels; ++i) {
   if (i/width < first || i/width >= first+rows_per_part) mask[i] = 0;
   draws |= mask[i] != 0;
  }
  if (draws) parts.push_back(write_sci1_vga_picture(band,palette,VgaPictureMode::overlay));
 }
 return parts;
}
}
