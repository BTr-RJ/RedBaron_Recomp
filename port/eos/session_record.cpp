#include "session_record.h"
#include <algorithm>
#include <stdexcept>

namespace redbaron::eos {
namespace {
constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
int Hex(char c) {if(c>='0' && c<='9')return c-'0';if(c>='a' && c<='f')return c-'a'+10;return -1;}
int Base64(char c) {
  const auto p=std::find(alphabet,alphabet+64,c);
  return p==alphabet+64 ? -1 : int(p-alphabet);
}
}
std::vector<std::string> EncodeSessionRecord(std::string_view hex) {
  if (hex.empty() || hex.size()%2 || hex.size()>2*kSessionMaxBytes) throw std::invalid_argument("Invalid session record length");
  std::string output;output.reserve((hex.size()*2+2)/3);
  unsigned bits=0, value=0;
  for(size_t i=0;i<hex.size();i+=2) {
    const int a=Hex(hex[i]),b=Hex(hex[i+1]);
    if(a<0 || b<0) throw std::invalid_argument("Invalid session record encoding");
    value=(value<<8)|unsigned((a<<4)|b);bits+=8;
    while(bits>=6) {bits-=6;output.push_back(alphabet[(value>>bits)&63]);}
  }
  if(bits) output.push_back(alphabet[(value<<(6-bits))&63]);
  std::vector<std::string> chunks;
  for(size_t i=0;i<output.size();i+=kSessionChunkSize) chunks.push_back(output.substr(i,kSessionChunkSize));
  return chunks;
}
std::optional<std::string> DecodeSessionRecord(const std::vector<std::string>& chunks) {
  constexpr size_t max_chunks=((kSessionMaxBytes*8+5)/6+kSessionChunkSize-1)/kSessionChunkSize;
  if(chunks.empty() || chunks.size()>max_chunks) return std::nullopt;
  size_t length=0;
  for(size_t i=0;i<chunks.size();++i) {
    if(chunks[i].empty() || chunks[i].size()>kSessionChunkSize ||
       (i+1<chunks.size() && chunks[i].size()!=kSessionChunkSize)) return std::nullopt;
    length+=chunks[i].size();
  }
  if(length%4==1 || length>(kSessionMaxBytes*8+5)/6) return std::nullopt;
  static constexpr char hex[]="0123456789abcdef";
  std::string output;output.reserve(length*3/2);
  unsigned bits=0,value=0;
  for(const auto& chunk:chunks) for(char c:chunk) {
    const int n=Base64(c);if(n<0)return std::nullopt;
    value=(value<<6)|unsigned(n);bits+=6;
    if(bits>=8) {bits-=8;const unsigned byte=(value>>bits)&255;output.push_back(hex[byte>>4]);output.push_back(hex[byte&15]);}
  }
  if(bits && (value&((1u<<bits)-1))) return std::nullopt;
  if(output.empty() || output.size()>2*kSessionMaxBytes)return std::nullopt;
  return output;
}
}
