#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>

namespace redbaron {
// Same order as launcher/src/Model.cs: digital buttons, triggers, stick halves.
constexpr size_t kPadActions = 24;
using PadMap = std::array<int, kPadActions>;
using PadValues = std::array<int, kPadActions>;
inline PadMap IdentityPadMap() {
  PadMap result{};
  for (size_t i=0; i<result.size(); ++i) result[i]=static_cast<int>(i);
  return result;
}
inline bool ParsePadMap(std::string_view text, PadMap& result) {
  auto parsed=IdentityPadMap();
  if (text.empty()) { result=parsed; return true; }
  for (size_t i=0; i<parsed.size(); ++i) {
    const auto end=text.find(',');
    const auto token=text.substr(0,end);
    int value=0;
    const auto conversion=std::from_chars(token.data(),token.data()+token.size(),value);
    if (conversion.ec!=std::errc{} || conversion.ptr!=token.data()+token.size() || value < -1 || value>=static_cast<int>(kPadActions)) return false;
    parsed[i]=value;
    if (i+1==parsed.size()) { if (end!=text.npos) return false; }
    else { if (end==text.npos) return false; text.remove_prefix(end+1); }
  }
  result=parsed;
  return true;
}
inline PadValues RemapPad(const PadValues& input,const PadMap& map) {
  PadValues output{};
  for(size_t i=0;i<output.size();++i)
    if(map[i]>=0 && map[i]<static_cast<int>(input.size())) output[i]=input[map[i]];
  return output;
}
} // namespace redbaron
