#include "session_record.h"
#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>
namespace redbaron::net {
bool ValidSessionRecord(std::string_view hex) {
  if (hex.size()<208 || hex.size()>16384 || hex.size()%2) return false;
  auto nibble=[](char c)->int {if(c>='0' && c<='9')return c-'0';if(c>='a' && c<='f')return c-'a'+10;return -1;};
  std::vector<unsigned char> bytes;bytes.reserve(hex.size()/2);
  for(size_t i=0;i<hex.size();i+=2) {
    const int a=nibble(hex[i]),b=nibble(hex[i+1]);if(a<0 || b<0)return false;
    bytes.push_back(static_cast<unsigned char>((a<<4)|b));
  }
  constexpr unsigned char prefix[]={'R','B','L','A','N','0','1',2};
  if(!std::equal(std::begin(prefix),std::end(prefix),bytes.begin()) ||
     bytes[16]!=0x58 || bytes[17]!=0x41 || bytes[18]!=0x09 || bytes[19]!=0xB5)return false;
  auto u32=[&](size_t i) {return (uint32_t(bytes[i])<<24)|(uint32_t(bytes[i+1])<<16)|
                                (uint32_t(bytes[i+2])<<8)|uint32_t(bytes[i+3]);};
  const uint64_t slots=uint64_t(u32(80))+u32(84)+u32(88)+u32(92);
  const auto contexts=u32(96),properties=u32(100);
  if(!u32(80) || slots>32 || contexts>32 || properties>32)return false;
  size_t pos=104;std::set<uint32_t> keys;
  for(uint32_t i=0;i<contexts;++i) {
    if(pos+8>bytes.size() || !keys.insert(u32(pos)).second)return false;
    pos+=8;
  }
  keys.clear();
  for(uint32_t i=0;i<properties;++i) {
    if(pos+8>bytes.size() || !keys.insert(u32(pos)).second)return false;
    const auto length=u32(pos+4);pos+=8;
    if(length>1024 || length>bytes.size()-pos)return false;
    pos+=length;
  }
  return pos==bytes.size();
}
}
