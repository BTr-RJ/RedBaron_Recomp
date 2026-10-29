#include "eos_wire.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace redbaron::eos {
using redbaron::net::Bytes;
namespace {
uint16_t Read16(const Bytes& b, size_t at) {
  return uint16_t((std::to_integer<unsigned>(b[at])<<8)|std::to_integer<unsigned>(b[at+1]));
}
bool ValidFrame(const Bytes& b) {
  return b.size()>=8 && b.size()<=kMessageLimit && b[0]==std::byte{'R'} &&
         b[1]==std::byte{'B'} && b[2]==std::byte{'N'} && b[3]==std::byte{1};
}
}
EosWire::EosWire(EOS_HP2P p2p, EOS_ProductUserId local, uint16_t slot, std::string_view name, P2PApi api)
    : p2p_(p2p), api_(api), local_(local), local_slot_(slot) {
  if (!p2p || !api.accept || !api.close || !api.queue || !api.send || !api.next || !api.receive ||
      !EOS_ProductUserId_IsValid(local) || slot>=8 || name.empty() || name.size()>32 ||
      !std::all_of(name.begin(),name.end(),[](unsigned char c) {
        return (c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') ||
               c=='-' || c=='_' || c==' ' || c=='+' || c=='=' || c=='.';
      })) throw std::invalid_argument("Invalid EOS P2P configuration");
  socket_.ApiVersion=EOS_P2P_SOCKETID_API_LATEST;
  std::memcpy(socket_.SocketName,name.data(),name.size());
  members_.emplace(slot,local);
}
EosWire::~EosWire() {
  for (const auto& [slot,user] : members_) {
    if (slot==local_slot_ || (local_slot_!=0 && slot!=0)) continue;
    EOS_P2P_CloseConnectionOptions options{};
    options.ApiVersion=EOS_P2P_CLOSECONNECTION_API_LATEST;
    options.LocalUserId=local_;options.RemoteUserId=user;options.SocketId=&socket_;
    api_.close(p2p_,&options);
  }
}
bool EosWire::SetMember(uint16_t slot, EOS_ProductUserId user) {
  if (slot>=8 || slot==local_slot_ || !EOS_ProductUserId_IsValid(user) || members_.contains(slot)) return false;
  for (const auto& [existing,id] : members_) if (id==user) return false;
  if (local_slot_==0 || slot==0) {
    EOS_P2P_AcceptConnectionOptions options{};
    options.ApiVersion=EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    options.LocalUserId=local_;options.RemoteUserId=user;options.SocketId=&socket_;
    if (api_.accept(p2p_,&options)!=EOS_EResult::EOS_Success) return false;
  }
  members_.emplace(slot,user);return true;
}
void EosWire::RemoveMember(uint16_t slot) {
  auto it=members_.find(slot);
  if (it==members_.end() || slot==local_slot_) return;
  if (local_slot_==0 || slot==0) {
    EOS_P2P_CloseConnectionOptions options{};
    options.ApiVersion=EOS_P2P_CLOSECONNECTION_API_LATEST;
    options.LocalUserId=local_;options.RemoteUserId=it->second;options.SocketId=&socket_;
    api_.close(p2p_,&options);
  }
  reliable_.RemovePeer(slot);unreliable_.RemovePeer(slot);members_.erase(it);
}
bool EosWire::Ready(uint16_t to) const {
  return to!=local_slot_ && members_.contains(to) && (local_slot_==0 || members_.contains(0));
}
size_t EosWire::Buffered(uint16_t to) const {
  if (!Ready(to)) return 1024*1024;
  EOS_P2P_GetPacketQueueInfoOptions options{};
  options.ApiVersion=EOS_P2P_GETPACKETQUEUEINFO_API_LATEST;
  EOS_P2P_PacketQueueInfo info{};
  if (api_.queue(p2p_,&options,&info)!=EOS_EResult::EOS_Success) return 1024*1024;
  return static_cast<size_t>(info.OutgoingPacketQueueCurrentSizeBytes);
}
bool EosWire::Send(uint16_t to,const Bytes& bytes,bool reliable) {
  if (!Ready(to) || !ValidFrame(bytes) || Read16(bytes,6)!=to || Buffered(to)>=512*1024) return false;
  const uint16_t from=Read16(bytes,4);
  if (!members_.contains(from) || from==to || (local_slot_!=0 && from!=local_slot_)) return false;
  const uint16_t physical=local_slot_==0 ? to : 0;
  auto it=members_.find(physical);
  if (it==members_.end()) return false;
  Packet message(bytes.size());
  std::transform(bytes.begin(),bytes.end(),message.begin(),[](std::byte b){return std::to_integer<uint8_t>(b);});
  const uint32_t id=next_message_++;
  if (!next_message_) next_message_=1;
  for (const auto& fragment : Fragment(id,message)) {
    EOS_P2P_SendPacketOptions options{};
    options.ApiVersion=EOS_P2P_SENDPACKET_API_LATEST;
    options.LocalUserId=local_;options.RemoteUserId=it->second;options.SocketId=&socket_;
    options.Channel=reliable ? 0 : 1;options.Data=fragment.data();
    options.DataLengthBytes=static_cast<uint32_t>(fragment.size());
    options.bAllowDelayedDelivery=EOS_TRUE;options.bDisableAutoAcceptConnection=EOS_TRUE;
    options.Reliability=reliable ? EOS_EPacketReliability::EOS_PR_ReliableOrdered : EOS_EPacketReliability::EOS_PR_UnreliableUnordered;
    if (api_.send(p2p_,&options)!=EOS_EResult::EOS_Success) return false;
  }
  return true;
}
bool EosWire::Poll() {
  for (uint8_t channel=0;channel<2;++channel) for (unsigned n=0;n<128;++n) {
    EOS_P2P_GetNextReceivedPacketSizeOptions size_options{};
    size_options.ApiVersion=EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    size_options.LocalUserId=local_;size_options.RequestedChannel=&channel;
    uint32_t size=0;
    const auto next=api_.next(p2p_,&size_options,&size);
    if (next==EOS_EResult::EOS_NotFound) break;
    if (next!=EOS_EResult::EOS_Success) return false;
    if (!size || size>kPacketLimit) return false;
    std::array<uint8_t,kPacketLimit> buffer{};
    EOS_P2P_ReceivePacketOptions options{};
    options.ApiVersion=EOS_P2P_RECEIVEPACKET_API_LATEST;
    options.LocalUserId=local_;options.MaxDataSizeBytes=static_cast<uint32_t>(buffer.size());options.RequestedChannel=&channel;
    EOS_ProductUserId remote{};EOS_P2P_SocketId socket{};socket.ApiVersion=EOS_P2P_SOCKETID_API_LATEST;
    uint8_t received_channel=0;uint32_t count=0;
    const auto received=api_.receive(p2p_,&options,&remote,&socket,&received_channel,buffer.data(),&count);
    if (received==EOS_EResult::EOS_NotFound) break;
    if (received!=EOS_EResult::EOS_Success) return false;
    if (count>buffer.size() || received_channel!=channel || std::strncmp(socket.SocketName,socket_.SocketName,sizeof(socket.SocketName))) continue;
    auto peer=std::find_if(members_.begin(),members_.end(),[remote](const auto& member){return member.second==remote;});
    if (peer==members_.end() || peer->first==local_slot_ || (local_slot_!=0 && peer->first!=0)) continue;
    Packet fragment(buffer.begin(),buffer.begin()+count);
    auto complete=(channel==0 ? reliable_ : unreliable_).Accept(peer->first,fragment,Reassembler::Clock::now());
    if (!complete) continue;
    Bytes bytes(complete->size());
    std::transform(complete->begin(),complete->end(),bytes.begin(),[](uint8_t b){return std::byte(b);});
    if (!ValidFrame(bytes)) continue;
    const auto from=Read16(bytes,4),to=Read16(bytes,6);
    if (!members_.contains(from) || !members_.contains(to) || from==to || (local_slot_==0 && from!=peer->first)) continue;
    if (to==local_slot_) {if (packet) packet(from,bytes,channel==0);}
    else if (local_slot_==0 && !Send(to,bytes,channel==0) && channel==0) return false;
  }
  return true;
}
}
