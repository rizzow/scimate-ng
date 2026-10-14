// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/picture_io.hpp"
#include "files.hpp"
#include "scimate/workspace.hpp"
#include "scimate/output.hpp"
#include <utility>
namespace scimate {
namespace {
const char* names[] = {"visual.png","priority.png","control.png"};
const std::string manifest = "SCIMATE-SCI0-PICTURE\t1\nplane\twidth\theight\tfile\nvisual\t320\t190\tvisual.png\npriority\t320\t190\tpriority.png\ncontrol\t320\t190\tcontrol.png\n";
}
void export_sci0_picture(const Bytes& payload, const std::filesystem::path& destination) {
 const auto picture=read_sci0_picture(payload);
 detail::create_destination(destination);
 detail::write_file(destination/workspace_detail::original_file,payload);
 for(int i=0;i<3;++i) detail::write_file(destination/names[i],encode_picture_plane_png(picture.planes[i]));
 detail::write_file(destination/"picture.tsv",Bytes(manifest.begin(),manifest.end()));
}
Bytes import_sci0_picture(const std::filesystem::path& directory) {
 auto picture=read_sci0_picture(detail::read_file(directory/workspace_detail::original_file,65535));
 if(detail::read_file(directory/"picture.tsv",1024)!=Bytes(manifest.begin(),manifest.end()))
  throw std::runtime_error("picture manifest: invalid structure or metadata");
 bool changed = false;
 for(int i=0;i<3;++i) {
  auto plane=decode_picture_plane_png(detail::read_file(directory/names[i],16*1024*1024));
  changed = changed || plane != picture.planes[i];
  picture.planes[i]=std::move(plane);
 }
 // The original has already been rendered and all PNGs validated. Avoid
 // rendering it again in the writer when none of the planes changed.
 if(!changed) return picture.original;
 return write_sci0_picture(picture);
}
VgaPicture import_vga_picture_edit(const std::filesystem::path& directory) {
 auto original = read_sci0_picture(detail::read_file(directory/workspace_detail::original_file,65535));
 if(detail::read_file(directory/"picture.tsv",1024)!=Bytes(manifest.begin(),manifest.end()))
  throw std::runtime_error("picture manifest: invalid structure or metadata");
 auto result = decode_vga_picture_visual_png(detail::read_file(directory/names[0],16*1024*1024));
 result.picture.original = original.original;
 result.picture.contains_fill = original.contains_fill;
 result.picture.written = original.written;
 for (unsigned plane = 1; plane < 3; ++plane)
  result.picture.planes[plane] = decode_picture_plane_png(detail::read_file(directory/names[plane],16*1024*1024));
 for (unsigned plane = 0; plane < 3; ++plane) for (std::size_t i = 0; i < original.planes[plane].size(); ++i)
  if (result.picture.planes[plane][i] != original.planes[plane][i]) result.picture.written[plane][i] = 1;
 return result;
}
void export_sci0_picture_file(const std::filesystem::path& payload,const std::filesystem::path& destination) {
 export_sci0_picture(detail::read_file(payload,65535),destination);
}
void import_sci0_picture_file(const std::filesystem::path& directory,const std::filesystem::path& payload) {
 const auto bytes=import_sci0_picture(directory);
 write_new_file(payload, bytes);
}
}
