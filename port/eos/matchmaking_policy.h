#pragma once
#include <rex/system/xam/lan_lobby.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
namespace redbaron::eos {
struct MatchPing {
  rex::system::xam::LanSessionId session{};
  uint16_t milliseconds=UINT16_MAX;
  std::chrono::steady_clock::time_point expires{};
};
inline uint32_t MatchHost(const rex::system::xam::LanRecord& r) {
  return uint32_t(r.info[8])<<24|uint32_t(r.info[9])<<16|uint32_t(r.info[10])<<8|r.info[11];
}
inline std::optional<uint16_t> SelectBestMatch(std::vector<rex::system::xam::LanRecord>& records,
    const std::map<uint32_t,MatchPing>& pings,std::chrono::steady_clock::time_point now) {
  const auto ping=[&](const auto& record) -> uint16_t {
    const auto found=pings.find(MatchHost(record));if(found==pings.end() || found->second.expires<=now)return UINT16_MAX;
    if(!std::equal(found->second.session.begin(),found->second.session.end(),record.info.begin()))return UINT16_MAX;
    return found->second.milliseconds;
  };
  std::erase_if(records,[](const auto& r){return r.private_session || r.open_public==0;});
  std::stable_sort(records.begin(),records.end(),[&](const auto& a,const auto& b){
    const auto pa=ping(a),pb=ping(b);return pa!=pb?pa<pb:a.info<b.info;
  });
  if(records.empty() || ping(records.front())==UINT16_MAX)return std::nullopt;
  const auto result=ping(records.front());records.resize(1);return result;
}
}
