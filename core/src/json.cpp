// SPDX-License-Identifier: GPL-2.0-or-later
#include "json.hpp"
#include <charconv>
#include <stdexcept>
namespace scimate::json_detail {
namespace {
[[noreturn]] void error(const std::string& s) {throw std::runtime_error("resource JSON: "+s);}
template<class T> const T& get(const Json& j) {if(auto v=std::get_if<T>(&j.value))return *v;error("unexpected value type");}
struct Parser {
 const std::string& text;std::size_t pos=0,nodes=0;
 void space() {while(pos<text.size() && (text[pos]==' ' || text[pos]=='\t' || text[pos]=='\r' || text[pos]=='\n'))++pos;}
 char take() {if(pos==text.size())error("truncated document at byte "+std::to_string(pos));return text[pos++];}
 bool accept(char c) {space();if(pos<text.size() && text[pos]==c) {++pos;return true;}return false;}
 void expect(char c) {if(!accept(c))error("expected '"+std::string(1,c)+"' at byte "+std::to_string(pos));}
 unsigned hex4() {unsigned v=0;for(int i=0;i<4;++i) {auto c=take();unsigned n;if(c>='0'&&c<='9')n=c-'0';else if(c>='a'&&c<='f')n=c-'a'+10;else if(c>='A'&&c<='F')n=c-'A'+10;else error("invalid Unicode escape");v=(v<<4)|n;}return v;}
 std::string string() {
  expect('"');std::string out;
  for(;;) {
   auto c=static_cast<unsigned char>(take());if(c=='"')break;
   if(c<32)error("unescaped control character");
   if(c!='\\') {out+=char(c);continue;}
   switch(take()) {
    case '"':out+='"';break;case '\\':out+='\\';break;case '/':out+='/';break;
    case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
    case 'u': {unsigned cp=hex4();if(cp>=0xd800 && cp<=0xdbff) {if(take()!='\\' || take()!='u')error("unpaired Unicode surrogate");auto low=hex4();if(low<0xdc00 || low>0xdfff)error("unpaired Unicode surrogate");cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);}out+=utf8(cp);break;}
    default:error("unknown string escape");
   }
  }
  scalars(out);return out;
 }
 Json value(unsigned depth=0) {
  if(depth>64 || ++nodes>2000000)error("document nesting/node limit exceeded");space();if(pos==text.size())error("missing value");auto c=text[pos];
  if(c=='"')return string();
  if(c=='[') {++pos;Json::Array out;if(accept(']'))return out;do {out.push_back(value(depth+1));}while(accept(','));expect(']');return out;}
  if(c=='{') {++pos;Json::Object out;if(accept('}'))return out;do {auto key=string();expect(':');if(!out.emplace(key,value(depth+1)).second)error("duplicate key: "+key);}while(accept(','));expect('}');return out;}
  for(auto token: {"true","false","null"}) {
   const std::string literal=token;if(text.compare(pos,literal.size(),literal)==0) {pos+=literal.size();if(literal=="null")return nullptr;return literal=="true";}
  }
  const auto begin=pos;if(c=='-')++pos;
  if(pos==text.size() || text[pos]<'0' || text[pos]>'9')error("expected integer");
  if(text[pos]=='0')++pos;else while(pos<text.size() && text[pos]>='0' && text[pos]<='9')++pos;
  std::int64_t number=0;auto result=std::from_chars(text.data()+begin,text.data()+pos,number);
  if(result.ec!=std::errc{})error("integer overflow");
  Json j;j.value=number;return j;
 }
};
std::string quote(const std::string& s) {
 scalars(s);std::string out="\"";const char* hex="0123456789abcdef";
 for(unsigned char c:s) {
  if(c=='"' || c=='\\') {out+='\\';out+=char(c);}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";
  else if(c<32) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);
 }
 return out+'"';
}
std::string dump(const Json& j,unsigned depth) {
 if(depth>64)error("document nesting limit exceeded");
 if(std::holds_alternative<std::nullptr_t>(j.value))return "null";
 if(auto v=std::get_if<bool>(&j.value))return *v?"true":"false";
 if(auto v=std::get_if<std::int64_t>(&j.value))return std::to_string(*v);
 if(auto v=std::get_if<std::string>(&j.value))return quote(*v);
 std::string out;const std::string indent(depth*2,' '),child((depth+1)*2,' ');
 if(auto a=std::get_if<Json::Array>(&j.value)) {
  if(a->empty())return "[]";
  bool compact=true;for(const auto& item:*a)if(std::holds_alternative<Json::Object>(item.value) || std::holds_alternative<Json::Array>(item.value))compact=false;
  out=compact?"[":"[\n";
  for(std::size_t i=0;i<a->size();++i) {if(!compact)out+=child;out+=dump((*a)[i],depth+1);if(i+1<a->size())out+=compact?", ":",\n";}
  return out+(compact?"]":"\n"+indent+"]");
 }
 const auto& obj=j.object();if(obj.empty())return "{}";out="{\n";std::size_t i=0;
 for(const auto& entry:obj) {out+=child+quote(entry.first)+": "+dump(entry.second,depth+1);if(++i<obj.size())out+=',';out+='\n';}
 return out+indent+'}';
}
}
const Json::Object& Json::object() const {return get<Object>(*this);}
Json::Object& Json::object() {return const_cast<Object&>(get<Object>(*this));}
const Json::Array& Json::array() const {return get<Array>(*this);}
Json::Array& Json::array() {return const_cast<Array&>(get<Array>(*this));}
const std::string& Json::string() const {return get<std::string>(*this);}
std::int64_t Json::number(std::int64_t min,std::int64_t max) const {auto n=get<std::int64_t>(*this);if(n<min || n>max)error("integer outside "+std::to_string(min)+".."+std::to_string(max));return n;}
const Json& Json::at(const std::string& key) const {auto found=object().find(key);if(found==object().end())error("missing field: "+key);return found->second;}
Json& Json::at(const std::string& key) {return const_cast<Json&>(static_cast<const Json&>(*this).at(key));}
void keys(const Json& j,std::initializer_list<const char*> names) {if(j.object().size()!=names.size())error("unknown or missing fields");for(auto name:names)j.at(name);}
Json parse_json(const std::string& text) {
 if(text.size()>16*1024*1024)error("document exceeds 16 MiB");Parser p{text};auto result=p.value();p.space();if(p.pos!=text.size())error("trailing content at byte "+std::to_string(p.pos));return result;
}
std::string dump_json(const Json& j) {return dump(j,0)+'\n';}
std::string utf8(unsigned cp) {
 if(cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))error("invalid Unicode scalar");std::string out;
 if(cp<128)out+=char(cp);
 else if(cp<0x800) {out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}
 else if(cp<0x10000) {out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
 else {out+=char(0xf0|(cp>>18));out+=char(0x80|((cp>>12)&63));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}return out;
}
std::vector<unsigned> scalars(const std::string& s) {
 std::vector<unsigned> out;
 for(std::size_t i=0;i<s.size();) {
  unsigned b=static_cast<unsigned char>(s[i++]),cp;unsigned n,min;
  if(b<128) {out.push_back(b);continue;}
  if(b>=0xc2 && b<=0xdf) {cp=b&31;n=1;min=0x80;}else if(b>=0xe0 && b<=0xef) {cp=b&15;n=2;min=0x800;}else if(b>=0xf0 && b<=0xf4) {cp=b&7;n=3;min=0x10000;}else error("invalid UTF-8");
  while(n--) {if(i==s.size())error("truncated UTF-8");b=static_cast<unsigned char>(s[i++]);if((b&0xc0)!=0x80)error("invalid UTF-8 continuation");cp=(cp<<6)|(b&63);}
  if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))error("invalid UTF-8 scalar");out.push_back(cp);
 }return out;
}
}
