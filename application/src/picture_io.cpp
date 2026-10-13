// SPDX-License-Identifier: GPL-2.0-or-later
#include "scimate/picture_io.hpp"
#include "files.hpp"
namespace scimate {
namespace {
const char* names[] = {"visual.png","priority.png","control.png"};
const std::string manifest = "SCIMATE-SCI0-PICTURE\t1\nplane\twidth\theight\tfile\nvisual\t320\t190\tvisual.png\npriority\t320\t190\tpriority.png\ncontrol\t320\t190\tcontrol.png\n";
}
void export_sci0_picture(const Bytes& payload, const std::filesystem::path& destination) {
 const auto picture=read_sci0_picture(payload);
 detail::create_destination(destination);
 detail::write_file(destination/"original.bin",payload);
 for(int i=0;i<3;++i) detail::write_file(destination/names[i],encode_picture_plane_png(picture.planes[i]));
 detail::write_file(destination/"picture.tsv",Bytes(manifest.begin(),manifest.end()));
}
Bytes import_sci0_picture(const std::filesystem::path& directory) {
 auto picture=read_sci0_picture(detail::read_file(directory/"original.bin",65535));
 if(detail::read_file(directory/"picture.tsv",1024)!=Bytes(manifest.begin(),manifest.end()))
  throw std::runtime_error("picture manifest: invalid structure or metadata");
 for(int i=0;i<3;++i) picture.planes[i]=decode_picture_plane_png(detail::read_file(directory/names[i],16*1024*1024));
 return write_sci0_picture(picture);
}
void export_sci0_picture_file(const std::filesystem::path& payload,const std::filesystem::path& destination) {
 export_sci0_picture(detail::read_file(payload,65535),destination);
}
void import_sci0_picture_file(const std::filesystem::path& directory,const std::filesystem::path& payload) {
 const auto bytes=import_sci0_picture(directory);
 if(std::filesystem::exists(std::filesystem::symlink_status(payload))) throw std::runtime_error("destination already exists: "+payload.string());
 detail::write_file(payload,bytes);
 if(detail::read_file(payload,65535)!=bytes) throw std::runtime_error("saved picture verification failed");
}
}
