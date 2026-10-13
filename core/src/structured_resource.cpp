// SPDX-License-Identifier: GPL-2.0-or-later
// SCI layout knowledge: SCI Companion Vocab99x.cpp, existing script parser;
// independent codecs and schema-driven editing. See docs/provenance.md.
#include "scimate/structured_resource.hpp"
#include "asset_binary.hpp"
#include "json.hpp"
#include "resource_charset.hpp"
#include "script_internal.hpp"
#include <algorithm>
namespace scimate {
namespace {
using namespace json_detail;
using asset_detail::require;
using asset_detail::Reader;
using asset_detail::put;
using asset_detail::word_at;
using A=Json::Array;
using O=Json::Object;
struct Binding {
 std::size_t record,offset,width;
 std::string field;
 enum Kind {integer,signed_integer,boolean,inverted_boolean,text,fixed_text,opcode,target,relative_target,big_integer,said_operator} kind=integer;
 unsigned mask=65535;
};
struct Document {Json root;std::vector<Binding> bindings;bool semantic=false;};
std::string label(std::size_t offset) {return "address_"+script_detail::hex(offset);}
const std::map<unsigned,std::string> said_names{{0xf0,","},{0xf1,"&"},{0xf2,"/"},{0xf3,"("},{0xf4,")"},{0xf5,"["},{0xf6,"]"},{0xf7,"#"},{0xf8,"<"},{0xf9,">"},{0xff,"end"}};
Document base(ResourceId id,const std::string& layout) {
 return {O{{"$schema","resource.schema.json"},{"format","scimate-resource"},{"version",1},{"encoding","cp437"},{"resource",O{{"type",std::string(resource_type_name(id.type))},{"number",unsigned(id.number)}}},{"layout",layout},{"records",A{}}},{},false};
}
void add(Document& d,Json record) {d.root.at("records").array().push_back(std::move(record));}
void field(Document& d,const Bytes& bytes,std::size_t offset,unsigned width,const std::string& name,Binding::Kind kind=Binding::integer,unsigned mask=65535) {
 require(offset<=bytes.size() && width<=bytes.size()-offset,"resource JSON: field outside payload");
 auto& records=d.root.at("records").array();require(!records.empty(),"resource JSON: missing record");
 Reader r(bytes,offset,offset+width);unsigned value=kind!=Binding::text && kind!=Binding::fixed_text && width<=2?(kind==Binding::big_integer?r.be(width):r.le(width)):0;Json item;
 if(kind==Binding::text || kind==Binding::fixed_text)item=byte_text(r.take(width));
 else if(kind==Binding::opcode)item=script_detail::opcode_name(value>>1);
 else if(kind==Binding::said_operator) {auto found=said_names.find(value);require(found!=said_names.end(),"script JSON: unknown said operator");item=found->second;}
 else if(kind==Binding::target)item=label(value);
 else if(kind==Binding::boolean || kind==Binding::inverted_boolean)item=kind==Binding::boolean?value!=0:value==0;
 else if(kind==Binding::signed_integer)item=script_detail::signed_value(value,width);
 else item=value&mask;
 records.back().object()[name]=item;d.bindings.push_back({records.size()-1,offset,width,name,kind,mask});
}
void cells(Document& d,const Bytes& b,std::size_t begin,std::size_t end,const std::string& kind,unsigned width=1) {
 require(begin<=end && end<=b.size(),"resource JSON: invalid range");
 for(auto pos=begin;pos<end;) {
  auto size=std::min<std::size_t>(width,end-pos);add(d,O{{"id",label(pos)},{"kind",kind},{"offset",pos}});field(d,b,pos,size,"value");pos+=size;
 }
}
Document script(ResourceId id,const Bytes& b) {
 auto parsed=script_detail::parse(b,false,false);auto d=base(id,"sci0-script");d.semantic=true;
 for(const auto& section:parsed.sections) {
  add(d,O{{"id","section_"+script_detail::hex(section.start)},{"kind","section"},{"offset",section.start},{"type",section.type},{"size",section.end-section.start}});
  if(section.type==2) {
   for(const auto& ins:parsed.instructions) if(ins.start>=section.start+4 && ins.end<=section.end) {
    add(d,O{{"id",label(ins.start)},{"kind","instruction"},{"offset",ins.start},{"operand_width",ins.width}});
    field(d,b,ins.start,1,"mnemonic",Binding::opcode);
    auto widths=script_detail::operand_widths(ins.opcode,ins.width);std::size_t pos=ins.start+1;
    for(std::size_t n=0;n<widths.size();++n) {
     auto name="operand_"+std::to_string(n);bool relative=n==0 && ((ins.opcode>=23 && ins.opcode<=25) || ins.opcode==32 || ins.opcode==57 || ins.opcode==58);
     if(relative) {
      d.root.at("records").array().back().object()[name]=label(script_detail::target(ins));
      d.bindings.push_back({d.root.at("records").array().size()-1,pos,widths[n],name,Binding::relative_target,unsigned(ins.end)});
     } else field(d,b,pos,widths[n],name,(ins.opcode==26 || ins.opcode==28)?Binding::signed_integer:Binding::integer);
     pos+=widths[n];
    }
   }
  } else if(section.type==5) {
   Reader r(b,section.start+4,section.end);
   while(r.remaining()) {
    auto offset=r.pos;Bytes text;while(r.remaining() && b[r.pos])text.push_back(r.byte());require(r.remaining()>0,"script JSON: unterminated string");r.byte();
    add(d,O{{"id",label(offset)},{"kind","string"},{"offset",offset},{"capacity",text.size()}});field(d,b,offset,text.size(),"text",Binding::text);
   }
  } else if(section.type==1 || section.type==6) {
   for(const auto& obj:parsed.objects) if(obj.pointer>=section.start && obj.pointer<section.end) {
    add(d,O{{"id",label(obj.pointer)},{"kind",obj.is_class?"class":"instance"},{"offset",obj.pointer}});
    field(d,b,section.start+6,2,"reserved_header_word");field(d,b,section.start+8,2,"method_area_word");
    auto pos=obj.pointer;
    for(std::size_t i=0;i<obj.values.size();++i,pos+=2) {
     add(d,O{{"id",label(pos)},{"kind","property"},{"object",label(obj.pointer)},{"index",i},{"offset",pos}});
     field(d,b,pos,2,i==0?"species":i==1?"superclass":i==2?"info":i==3?"name_pointer":"value",i==3?Binding::target:Binding::integer);
    }
    if(obj.is_class)for(std::size_t i=0;i<obj.selectors.size();++i,pos+=2) {add(d,O{{"id",label(pos)},{"kind","property-selector"},{"index",i},{"offset",pos}});field(d,b,pos,2,"selector");}
    pos+=2;auto ids=pos;auto addresses=pos+obj.methods.size()*2+2;
    for(std::size_t i=0;i<obj.methods.size();++i) {add(d,O{{"id",label(ids+i*2)},{"kind","method"},{"object",label(obj.pointer)},{"index",i},{"offset",ids+i*2}});field(d,b,ids+i*2,2,"selector");field(d,b,addresses+i*2,2,"entry",Binding::target);}
   }
  } else if(section.type==7 || section.type==8) {
   for(auto pos=section.start+6;pos<section.end;pos+=2) {add(d,O{{"id",label(pos)},{"kind",section.type==7?"export":"relocation"},{"offset",pos},{"index",(pos-section.start-6)/2}});field(d,b,pos,2,"address",Binding::target);}
  } else if(section.type==10)cells(d,b,section.start+4,section.end,"local",2);
  else if(section.type==3 && (section.end-section.start-4)%4==0) {
   for(auto pos=section.start+4;pos<section.end;pos+=4) {add(d,O{{"id",label(pos)},{"kind","synonym"},{"offset",pos}});field(d,b,pos,2,"source_group");field(d,b,pos+2,2,"replacement_group");}
  } else if(section.type==4) {
   for(auto pos=section.start+4;pos<section.end;) {
    add(d,O{{"id",label(pos)},{"kind",b[pos]>=0xf0?"said-operator":"said-word-group"},{"offset",pos}});
    if(b[pos]>=0xf0) {field(d,b,pos,1,"operator",Binding::said_operator);++pos;}
    else {require(section.end-pos>=2,"script JSON: truncated said group");field(d,b,pos,2,"word_group",Binding::big_integer);pos+=2;}
   }
  } else cells(d,b,section.start+4,section.end,"uninterpreted-section-byte");
 }
 return d;
}
Document names(ResourceId id,const Bytes& b) {
 Reader r(b);auto header=r.le(2);auto count=std::size_t(header)+1;
 // Selectors use maximum ID; kernel/opcode/debug tables have version-specific
 // count conventions. Require a complete layout, trying the shorter convention.
 auto valid=[&](std::size_t n) {if(n>(b.size()-2)/2)return false;for(std::size_t i=0;i<n;++i) {auto offset=script_detail::word(b,2+i*2,b.size());if(!offset)continue;if(offset<2+n*2 || offset+2>b.size())return false;auto length=script_detail::word(b,offset,b.size());if(length>b.size()-offset-2)return false;}return true;};
 if(!valid(count) && id.number!=997)count=header;
 require(valid(count),"vocabulary JSON: invalid name offset table");auto d=base(id,"vocabulary-names");d.semantic=true;d.root.object()["count_convention"]=count==std::size_t(header)+1?"maximum-id":"count";
 for(std::size_t i=0;i<count;++i) {
  auto offset=script_detail::word(b,2+i*2,b.size());Json value=nullptr;
  if(offset) {auto length=script_detail::word(b,offset,b.size());value=byte_text(Bytes(b.begin()+offset+2,b.begin()+offset+2+length));}
  add(d,O{{"id",i},{"kind","name"},{"name",value}});
 }
 return d;
}
Document vocabulary(ResourceId id,const Bytes& b) {
 if(id.number==995 || id.number==997 || id.number==998 || id.number==999)return names(id,b);
 if(id.number==996) {
  require(b.size()%4==0,"vocabulary JSON: invalid class table length");auto d=base(id,"vocabulary-classes");d.semantic=true;
  for(std::size_t pos=0;pos<b.size();pos+=4) {add(d,O{{"id",pos/4},{"kind","class-location"},{"offset",pos}});field(d,b,pos,2,"reserved");field(d,b,pos+2,2,"script");}return d;
 }
 if(id.number==900 || id.number==910) {
  require(b.size()%20==0,"vocabulary JSON: invalid grammar length");auto d=base(id,"vocabulary-grammar");d.semantic=true;
  for(std::size_t pos=0;pos<b.size();pos+=20) {add(d,O{{"id",pos/20},{"kind","grammar-rule"},{"offset",pos}});field(d,b,pos,2,"rule_id");for(int i=0;i<9;++i)field(d,b,pos+2+i*2,2,"symbol_"+std::to_string(i));}return d;
 }
 if(id.number==994) {auto d=base(id,"vocabulary-property-offsets");d.semantic=true;require(b.size()%2==0,"vocabulary JSON: odd property offset table");cells(d,b,0,b.size(),"property-offset",2);return d;}
 if(id.number==901 || id.number==911) {
  auto d=base(id,"vocabulary-suffixes");d.semantic=true;Reader r(b);require(r.remaining()>0,"vocabulary JSON: empty suffix table");r.byte();
  auto string_field=[&](const char* name) {auto pos=r.pos;while(r.remaining() && b[r.pos])r.byte();require(r.remaining()>0,"vocabulary JSON: unterminated suffix");field(d,b,pos,r.pos-pos,name,Binding::text);r.byte();};
  std::size_t index=0;
  while(r.remaining()>1 && b[r.pos+1]!=255) {
   add(d,O{{"id",index++},{"kind","suffix-rule"},{"offset",r.pos}});string_field("alternative_suffix");auto pos=r.pos;r.le(2);field(d,b,pos,2,"result_class",Binding::big_integer);r.byte();string_field("word_suffix");pos=r.pos;r.le(2);field(d,b,pos,2,"class_mask",Binding::big_integer);if(r.remaining())r.byte();
  }
  return d;
 }
 if(id.number==913) {
  auto d=base(id,"vocabulary-alternate-inputs");d.semantic=true;Reader r(b);std::size_t index=0;
  while(r.remaining() && b[r.pos]) {
   add(d,O{{"id",index++},{"kind","alternate-input"},{"offset",r.pos}});
   for(auto name:{"input","replacement"}) {auto pos=r.pos;while(r.remaining() && b[r.pos])r.byte();require(r.remaining()>0,"vocabulary JSON: unterminated alternate input");field(d,b,pos,r.pos-pos,name,Binding::text);r.byte();}
  }
  return d;
 }
 return base(id,"opaque-bytes");
}
Document adlib(ResourceId id,const Bytes& b) {
 require(b.size()==1344 || b.size()==2690,"patch JSON: unsupported AdLib bank size");auto d=base(id,"adlib-bank");d.semantic=true;
 unsigned count=b.size()==1344?48:96;
 const char* names[]={"keyboard_scale_level","frequency_multiplier","reserved","attack_rate","sustain_level","envelope_type","decay_rate","release_rate","total_level","amplitude_modulation","vibrato","keyboard_scale_rate","reserved_algorithm"};
 const unsigned masks[]={3,15,255,15,15,255,15,15,63,255,255,255,255};
 for(unsigned instrument=0;instrument<count;++instrument) {
  auto start=instrument*28+(b.size()==2690 && instrument>=48?2:0);
  for(unsigned op=0;op<2;++op) {
   add(d,O{{"id","instrument_"+std::to_string(instrument)+"_operator_"+std::to_string(op)},{"kind","adlib-operator"},{"instrument",instrument},{"role",op==0?"modulator":"carrier"},{"offset",start+op*13}});
   for(unsigned i=0;i<13;++i) {
    std::string name=names[i];auto kind=Binding::integer;unsigned mask=masks[i];
    if(i==5 || i==9 || i==10 || i==11)kind=Binding::boolean;
    if(op==0 && i==2) {name="feedback";mask=7;}
    if(op==0 && i==12) {name="additive_algorithm";kind=Binding::inverted_boolean;}
    field(d,b,start+op*13+i,1,name,kind,mask);
   }
   field(d,b,start+26+op,1,"waveform",Binding::integer,3);
  }
 }
 return d;
}
Document mt32(ResourceId id,const Bytes& b) {
 require(b.size()>=492,"patch JSON: truncated MT-32 header");auto count=b[491];require(count<=64 && 492+std::size_t(count)*246<=b.size(),"patch JSON: invalid MT-32 timbre count");
 auto d=base(id,"mt32-bank");d.semantic=true;
 add(d,O{{"id","display"},{"kind","mt32-display"},{"offset",0}});
 field(d,b,0,20,"after_message",Binding::fixed_text);field(d,b,20,20,"before_message",Binding::fixed_text);field(d,b,40,20,"goodbye_message",Binding::fixed_text);field(d,b,60,2,"volume");field(d,b,62,1,"reverb");
 cells(d,b,63,107,"mt32-reverb-byte");
 auto patches=[&](std::size_t begin,unsigned first) {
  for(unsigned i=0;i<48;++i) {auto pos=begin+i*8;add(d,O{{"id","program_"+std::to_string(first+i)},{"kind","mt32-program"},{"offset",pos}});field(d,b,pos,1,"timbre_group");field(d,b,pos+1,1,"timbre_number");field(d,b,pos+2,1,"key_shift");field(d,b,pos+3,1,"fine_tune");field(d,b,pos+4,1,"bend_range");field(d,b,pos+5,1,"assign_mode");field(d,b,pos+6,1,"reverb_switch");field(d,b,pos+7,1,"reserved");}
 };
 patches(107,0);
 for(unsigned timbre=0;timbre<count;++timbre) {
  auto pos=492+timbre*246;add(d,O{{"id","timbre_"+std::to_string(timbre)},{"kind","mt32-timbre"},{"offset",pos}});field(d,b,pos,10,"name",Binding::fixed_text);field(d,b,pos+10,1,"structure_1_2");field(d,b,pos+11,1,"structure_3_4");field(d,b,pos+12,1,"partial_mute");field(d,b,pos+13,1,"no_sustain");
  for(unsigned partial=0;partial<4;++partial) {add(d,O{{"id","timbre_"+std::to_string(timbre)+"_partial_"+std::to_string(partial)},{"kind","mt32-partial"},{"offset",pos+14+partial*58}});for(unsigned parameter=0;parameter<58;++parameter)field(d,b,pos+14+partial*58+parameter,1,"parameter_"+std::to_string(parameter));}
 }
 auto pos=492+std::size_t(count)*246;
 if(pos<b.size()) {Reader r(b,pos,b.size());require(r.le(2)==0xabcd,"patch JSON: unknown MT-32 extension");pos+=2;require(b.size()-pos>=384,"patch JSON: truncated MT-32 program bank");patches(pos,48);pos+=384;
  if(pos<b.size()) {Reader rhythm(b,pos,b.size());require(rhythm.le(2)==0xdcba && rhythm.remaining()>=265,"patch JSON: invalid MT-32 rhythm extension");pos+=2;cells(d,b,pos,pos+256,"mt32-rhythm-byte");pos+=256;cells(d,b,pos,pos+9,"mt32-partial-reserve");pos+=9;}
 }
 require(pos==b.size(),"patch JSON: unknown trailing MT-32 data");return d;
}
Document describe(ResourceId id,const Bytes& b) {
 require(b.size()<=65535 && unsigned(id.type)<=9 && id.number<=2047,"resource JSON: invalid SCI0 payload/identity");
 try {
  if(id.type==ResourceType::script)return script(id,b);
  if(id.type==ResourceType::vocab) {auto d=vocabulary(id,b);if(d.semantic)return d;}
  if(id.type==ResourceType::patch && id.number==3)return adlib(id,b);
  if(id.type==ResourceType::patch && (id.number==0 || id.number==1))return mt32(id,b);
 }catch(const std::runtime_error& error) {
  auto d=base(id,"opaque-bytes");d.root.object()["diagnostic"]=error.what();cells(d,b,0,b.size(),"uninterpreted-byte");return d;
 }
 auto d=base(id,id.type==ResourceType::memory?"memory-cells":"opaque-bytes");cells(d,b,0,b.size(),id.type==ResourceType::memory?"memory-byte":"uninterpreted-byte");return d;
}
Bytes read_names(ResourceId id,const Bytes& original,const Json& edited,const Document& reference) {
 auto normalized=reference.root;auto& rows=normalized.at("records").array();const auto& input=edited.at("records").array();require(rows.size()==input.size(),"vocabulary JSON: name IDs/count are fixed");
 for(std::size_t i=0;i<rows.size();++i) {rows[i].at("name")=input[i].at("name");if(!std::holds_alternative<std::nullptr_t>(rows[i].at("name").value))text_bytes(rows[i].at("name").string());}
 require(normalized==edited,"vocabulary JSON: changed identity/layout/unknown fields");if(normalized==reference.root)return original;
 Bytes out(2+rows.size()*2,0);word_at(out,0,script_detail::word(original,0,original.size()));
 std::map<Bytes,unsigned> shared;
 for(std::size_t i=0;i<rows.size();++i) {
  const auto& name=rows[i].at("name");if(std::holds_alternative<std::nullptr_t>(name.value))continue;auto bytes=text_bytes(name.string());
  auto found=shared.find(bytes);unsigned offset;
  if(found!=shared.end())offset=found->second;else {offset=out.size();shared.emplace(bytes,offset);put(out,bytes.size(),2);out.insert(out.end(),bytes.begin(),bytes.end());}
  word_at(out,2+i*2,offset);asset_detail::payload_limit(out);
 }
 require(names(id,out).root==edited,"vocabulary JSON: reconstruction differs");return out;
}
Bytes edit(Document reference,const Bytes& original,const Json& input) {
 auto normalized=reference.root;auto& records=normalized.at("records").array();const auto& edits=input.at("records").array();require(records.size()==edits.size(),"resource JSON: fixed record count/IDs cannot change");Bytes out=original;
 for(const auto& binding:reference.bindings) {
  const auto& value=edits[binding.record].at(binding.field);const auto& previous=records[binding.record].at(binding.field);
  if(value==previous)continue;
  unsigned number=0;
  if(binding.kind==Binding::text || binding.kind==Binding::fixed_text) {
   auto bytes=text_bytes(value.string());require(bytes.size()==binding.width && (binding.kind==Binding::fixed_text || std::find(bytes.begin(),bytes.end(),0)==bytes.end()),"resource JSON: text must retain encoded length; NUL is only allowed in fixed text fields");std::copy(bytes.begin(),bytes.end(),out.begin()+binding.offset);
  } else {
   if(binding.kind==Binding::boolean || binding.kind==Binding::inverted_boolean) {auto v=std::get_if<bool>(&value.value);require(v!=nullptr,"patch JSON: expected boolean");number=binding.kind==Binding::boolean?unsigned(*v):unsigned(!*v);}
   else if(binding.kind==Binding::opcode) {
    bool found=false;
    for(unsigned op=0;op<128;++op) {
     if(script_detail::opcode_name(op).empty() || script_detail::opcode_name(op)!=value.string()) continue;
     auto old_op=original[binding.offset]>>1;auto width=(original[binding.offset]&1)?1u:2u;
     require(script_detail::operand_widths(op,width)==script_detail::operand_widths(old_op,width),"script JSON: opcode must retain operand layout");
     auto relative=[](unsigned n) {return (n>=23 && n<=25) || n==32 || n==57 || n==58;};
     require(relative(op)==relative(old_op) && ((op==26 || op==28)==(old_op==26 || old_op==28)),"script JSON: opcode must retain operand interpretation");number=(op<<1)|(original[binding.offset]&1);found=true;break;
    }
    require(found,"script JSON: unknown mnemonic");
   } else if(binding.kind==Binding::said_operator) {
    bool found=false;for(const auto& op:said_names)if(op.second==value.string()) {number=op.first;found=true;break;}require(found,"script JSON: unknown said operator");
   } else if(binding.kind==Binding::target || binding.kind==Binding::relative_target) {
    require(value.string().size()==12 && value.string().substr(0,8)=="address_","script JSON: invalid address label");auto suffix=value.string().substr(8);require(std::all_of(suffix.begin(),suffix.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}),"script JSON: labels use four lowercase hex digits");std::size_t consumed=0;number=std::stoul(suffix,&consumed,16);require(consumed==4 && number<=65535,"script JSON: invalid address");
    if(binding.kind==Binding::relative_target) {int delta=int(number)-int(binding.mask);if(delta>32767)delta-=65536;if(delta< -32768)delta+=65536;require(binding.width==2 || (delta>=-128 && delta<=127),"script JSON: relative target exceeds operand width");number=unsigned(delta)&(binding.width==1?255:65535);}
   } else if(binding.kind==Binding::signed_integer)number=unsigned(value.number(binding.width==1?-128:-32768,binding.width==1?127:32767))&(binding.width==1?255:65535);
   else number=value.number(0,std::min<unsigned>(binding.mask,binding.width==1?255:65535));
   if(binding.width==1) {if(binding.kind==Binding::integer)out[binding.offset]=(original[binding.offset]&~binding.mask)|(number&binding.mask);else out[binding.offset]=number;}
   else if(binding.kind==Binding::big_integer) {out[binding.offset]=number>>8;out[binding.offset+1]=number&255;}
   else word_at(out,binding.offset,number);
  }
  records[binding.record].at(binding.field)=value;
 }
 require(normalized==input,"resource JSON: changed immutable identity/layout or unknown fields");return out;
}
}
std::string export_resource_json(ResourceId id,const Bytes& payload) {return json_detail::dump_json(describe(id,payload).root);}
std::string resource_json_schema(ResourceId id,const Bytes& payload) {
 auto document=describe(id,payload);const auto& records=document.root.at("records").array();
 std::map<std::pair<std::size_t,std::string>,Binding> bindings;
 for(const auto& binding:document.bindings)bindings.emplace(std::make_pair(binding.record,binding.field),binding);
 Json::Object shapes;
 for(std::size_t i=0;i<records.size();++i) {
  O properties;A required;
  for(const auto& field:records[i].object()) {
   required.emplace_back(field.first);Json schema;
   auto binding=bindings.find({i,field.first});
   if(binding!=bindings.end()) {
    const auto& b=binding->second;
    if(b.kind==Binding::integer || b.kind==Binding::big_integer || b.kind==Binding::signed_integer) {
     auto min=b.kind==Binding::signed_integer?(b.width==1?-128:-32768):0;
     auto max=b.kind==Binding::signed_integer?(b.width==1?127:32767):int(std::min<unsigned>(b.mask,b.width==1?255:65535));
     schema=O{{"type","integer"},{"minimum",min},{"maximum",max}};
    } else if(b.kind==Binding::boolean || b.kind==Binding::inverted_boolean)schema=O{{"type","boolean"}};
    else if(b.kind==Binding::text || b.kind==Binding::fixed_text)schema=O{{"type","string"},{"minLength",b.width},{"maxLength",b.width},{"description","CP437 text; encoded length and addresses stay fixed."}};
    else if(b.kind==Binding::opcode) {
     A choices;auto old=payload[b.offset]>>1;auto width=(payload[b.offset]&1)?1u:2u;
     auto relative=[](unsigned n) {return (n>=23 && n<=25) || n==32 || n==57 || n==58;};
     for(unsigned op=0;op<128;++op)if(!script_detail::opcode_name(op).empty() && script_detail::operand_widths(op,width)==script_detail::operand_widths(old,width) && relative(op)==relative(old) && ((op==26 || op==28)==(old==26 || old==28)))choices.emplace_back(script_detail::opcode_name(op));
     schema=O{{"type","string"},{"enum",choices}};
    } else if(b.kind==Binding::said_operator) {A ops;for(const auto& op:said_names)ops.emplace_back(op.second);schema=O{{"type","string"},{"enum",ops}};}
    else schema=O{{"type","string"},{"pattern","^address_[0-9a-f]{4}$"}};
   } else if(field.first=="name" && document.root.at("layout").string()=="vocabulary-names")schema=O{{"type",A{"string","null"}},{"description","Editable CP437 name. Null means no name at this ID."}};
   else if(field.first=="id" || field.first=="offset" || field.first=="index" || field.first=="object" || field.first=="instrument")schema=std::holds_alternative<std::string>(field.second.value)?Json(O{{"type","string"}}):Json(O{{"type","integer"},{"minimum",0},{"maximum",65535}});
   else schema=O{{"const",field.second}};
   properties.emplace(field.first,std::move(schema));
  }
  Json shape=O{{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};
  shapes.emplace(dump_json(shape),std::move(shape));
 }
 A choices;for(auto& shape:shapes)choices.push_back(std::move(shape.second));O properties;A required;
 for(const auto& field:document.root.object()) {required.emplace_back(field.first);properties[field.first]=field.first=="records"?Json(O{{"type","array"},{"minItems",records.size()},{"maxItems",records.size()},{"items",choices.empty()?Json(false):Json(O{{"anyOf",choices}})}}):Json(O{{"const",field.second}});}
 return dump_json(O{{"$schema","https://json-schema.org/draft/2020-12/schema"},{"title","SCIMate "+document.root.at("layout").string()+" resource"},{"description","Generated for this resource. Import additionally checks immutable IDs, field interpretations, encoded text and script targets."},{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}});
}
bool resource_json_is_semantic(ResourceId id,const Bytes& payload) {return describe(id,payload).semantic;}
Bytes import_resource_json(ResourceId id,const Bytes& original,const std::string& text) {
 auto reference=describe(id,original);auto input=json_detail::parse_json(text);
 if(reference.root.at("layout").string()=="vocabulary-names")return read_names(id,original,input,reference);
 auto out=edit(reference,original,input);
 if(out==original)return original;
 if(reference.root.at("layout").string()=="sci0-script")script_detail::parse(out);
 if(reference.semantic) {auto reopened=describe(id,out);require(reopened.root==input,"resource JSON: reconstructed semantic content differs");}return out;
}
}
