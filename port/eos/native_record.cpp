#include "native_record.h"
#include "../network/session_record.h"
#include <algorithm>
namespace redbaron::eos {
std::string EncodeNativeRecord(const rex::system::xam::LanRecord& r) {
  std::vector<uint8_t> b{'R','B','L','A','N','0','1',2,0,0,0,0,0,0,0,0};
  const auto put=[&](uint32_t n){for(int shift=24;shift>=0;shift-=8)b.push_back(uint8_t(n>>shift));};
  put(r.title);b.insert(b.end(),r.info.begin(),r.info.end());
  for(auto n:{r.open_public,r.open_private,r.filled_public,r.filled_private,uint32_t(r.contexts.size()),uint32_t(r.properties.size())})put(n);
  for(auto [key,value]:r.contexts){put(key);put(value);}
  for(const auto& [key,value]:r.properties){put(key);put(uint32_t(value.size()));b.insert(b.end(),value.begin(),value.end());}
  constexpr char hex[]="0123456789abcdef";std::string out;
  for(auto byte:b){out.push_back(hex[byte>>4]);out.push_back(hex[byte&15]);}
  return redbaron::net::ValidSessionRecord(out) ? out : std::string{};
}
std::optional<rex::system::xam::LanRecord> DecodeNativeRecord(std::string_view record) {
  if(!redbaron::net::ValidSessionRecord(record))return std::nullopt;
  std::vector<uint8_t> b;const auto digit=[](char c){return c<='9' ? c-'0' : c-'a'+10;};
  for(size_t i=0;i<record.size();i+=2)b.push_back(uint8_t(digit(record[i])*16+digit(record[i+1])));
  size_t at=16;
  const auto get=[&](){uint32_t n=0;for(int i=0;i<4;++i)n=(n<<8)|b[at++];return n;};
  rex::system::xam::LanRecord r;r.title=get();std::copy_n(b.begin()+at,60,r.info.begin());at+=60;
  r.open_public=get();r.open_private=get();r.filled_public=get();r.filled_private=get();
  const auto contexts=get(),properties=get();
  for(uint32_t i=0;i<contexts;++i){const auto key=get(),value=get();r.contexts.emplace(key,value);}
  for(uint32_t i=0;i<properties;++i){const auto key=get(),size=get();r.properties.emplace(key,std::vector<uint8_t>(b.begin()+at,b.begin()+at+size));at+=size;}
  return r;
}
}
