// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <type_traits>
#include <vector>
namespace scimate::json_detail {
struct Json {
 using Array=std::vector<Json>;
 using Object=std::map<std::string,Json>;
 std::variant<std::nullptr_t,bool,std::int64_t,std::string,Array,Object> value=nullptr;
 Json()=default;
 Json(std::nullptr_t):value(nullptr) {}
 Json(bool v):value(v) {}
 template<class N, std::enable_if_t<std::is_integral_v<N> && !std::is_same_v<N,bool>,int> = 0>
 Json(N v):value(std::int64_t(v)) {}
 Json(const char* v):value(std::string(v)) {}
 Json(std::string v):value(std::move(v)) {}
 Json(Array v):value(std::move(v)) {}
 Json(Object v):value(std::move(v)) {}
 const Object& object() const;
 Object& object();
 const Array& array() const;
 Array& array();
 const std::string& string() const;
 std::int64_t number(std::int64_t min,std::int64_t max) const;
 const Json& at(const std::string& key) const;
 Json& at(const std::string& key);
 bool operator==(const Json& other) const {return value==other.value;}
 bool operator!=(const Json& other) const {return !(*this==other);}
};
Json parse_json(const std::string& text);
std::string dump_json(const Json& value);
void keys(const Json& value,std::initializer_list<const char*> names);
std::string utf8(unsigned scalar);
std::vector<unsigned> scalars(const std::string& utf8_text);
}
