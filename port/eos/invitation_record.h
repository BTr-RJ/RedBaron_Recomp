#pragma once
#include <rex/system/xam/lan_lobby.h>
#include <algorithm>
#include <optional>
namespace redbaron::eos {
// XINVITE_INFO as consumed by this title: invitee, inviter, title, 60-byte
// XSESSION_INFO and fFromGameInvite. This is data, never a guest code fragment.
inline std::optional<std::array<uint8_t,84>> PackNativeInvitation(
    const rex::system::xam::LanRecord& record,uint64_t invitee) {
  if(record.title!=0x584109B5 || (invitee>>48)!=9 ||
      record.info[8]!=10 || record.info[9]!=203 ||
      !(record.info[10] || record.info[11]))return std::nullopt;
  std::array<uint8_t,84> out{};
  const auto put=[&](size_t at,uint64_t value,size_t bytes) {
    for(size_t i=0;i<bytes;++i)out[at+i]=uint8_t(value>>(8*(bytes-1-i)));
  };
  const uint16_t peer=uint16_t(record.info[10])<<8|record.info[11];
  const uint64_t inviter=record.host_xuid?record.host_xuid:(0x0009000000000000ull|(uint64_t(peer)<<8));
  if((inviter>>48)!=9 || inviter==invitee)return std::nullopt;
  put(0,invitee,8);put(8,inviter,8);put(16,record.title,4);
  std::copy(record.info.begin(),record.info.end(),out.begin()+20);put(80,1,4);return out;
}
}
