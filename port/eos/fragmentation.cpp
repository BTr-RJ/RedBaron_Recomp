#include "fragmentation.h"
#include <algorithm>
#include <stdexcept>

namespace redbaron::eos {
namespace {
constexpr size_t kPayload = kPacketLimit-kHeader;
uint32_t Read32(const Packet& p, size_t i) {
  return (uint32_t(p[i])<<24)|(uint32_t(p[i+1])<<16)|(uint32_t(p[i+2])<<8)|p[i+3];
}
}
std::vector<Packet> Fragment(uint32_t id, const Packet& message) {
  if (!id || message.empty() || message.size()>kMessageLimit)
    throw std::invalid_argument("Invalid P2P message size or id");
  const auto count=(message.size()+kPayload-1)/kPayload;
  std::vector<Packet> output; output.reserve(count);
  for (size_t i=0; i<count; ++i) {
    const size_t start=i*kPayload, end=std::min(start+kPayload,message.size());
    Packet p{'R','B','E',1,uint8_t(id>>24),uint8_t(id>>16),uint8_t(id>>8),uint8_t(id),
             uint8_t(message.size()>>8),uint8_t(message.size()),uint8_t(i),uint8_t(count)};
    p.insert(p.end(),message.begin()+start,message.begin()+end);
    output.push_back(std::move(p));
  }
  return output;
}
std::optional<Packet> Reassembler::Accept(uint16_t peer, const Packet& p, Clock::time_point now) {
  for (auto it=pending_.begin(); it!=pending_.end();) {
    if (now-it->second.created>std::chrono::seconds(5)) it=pending_.erase(it); else ++it;
  }
  if (p.size()<=kHeader || p.size()>kPacketLimit || p[0]!='R' || p[1]!='B' || p[2]!='E' || p[3]!=1)
    return std::nullopt;
  const uint32_t id=Read32(p,4);
  const uint16_t total=uint16_t((uint16_t(p[8])<<8)|p[9]);
  const uint8_t index=p[10], count=p[11];
  if (!id || !total || total>kMessageLimit || !count || index>=count ||
      count!=(total+kPayload-1)/kPayload ||
      p.size()-kHeader!=std::min(kPayload,size_t(total)-size_t(index)*kPayload)) return std::nullopt;
  const auto key=std::make_pair(peer,id);
  auto it=pending_.find(key);
  if (it==pending_.end()) {
    const auto peer_entries=std::count_if(pending_.begin(),pending_.end(),
        [peer](const auto& entry) { return entry.first.first==peer; });
    if (pending_.size()>=64 || peer_entries>=8) return std::nullopt;
    it=pending_.emplace(key,Pending{total,count,now,std::vector<Packet>(count),0}).first;
  }
  auto& state=it->second;
  if (state.total!=total || state.count!=count) { pending_.erase(it); return std::nullopt; }
  Packet part(p.begin()+kHeader,p.end());
  auto& previous=state.parts[index];
  if (!previous.empty()) {
    if (previous!=part) pending_.erase(it);
    return std::nullopt;
  }
  previous=std::move(part); ++state.received;
  if (state.received!=count) return std::nullopt;
  Packet result; result.reserve(total);
  for (const auto& item:state.parts) result.insert(result.end(),item.begin(),item.end());
  pending_.erase(it);
  return result;
}
void Reassembler::RemovePeer(uint16_t peer) {
  for (auto it=pending_.begin(); it!=pending_.end();) {
    if (it->first.first==peer) it=pending_.erase(it); else ++it;
  }
}
}
