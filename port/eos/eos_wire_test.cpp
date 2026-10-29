#include "eos_wire.h"
#include <eos_init.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace redbaron::eos;
using redbaron::net::Bytes;
struct Message { EOS_ProductUserId from,to; EOS_P2P_SocketId socket; uint8_t channel; Packet data; };
static std::deque<Message> queue;
static void Check(bool value) { if (!value) throw std::runtime_error("EOS wire check failed"); }
static EOS_EResult EOS_CALL Accept(EOS_HP2P,const EOS_P2P_AcceptConnectionOptions*) {return EOS_EResult::EOS_Success;}
static EOS_EResult EOS_CALL Close(EOS_HP2P,const EOS_P2P_CloseConnectionOptions*) {return EOS_EResult::EOS_Success;}
static EOS_EResult EOS_CALL QueueInfo(EOS_HP2P,const EOS_P2P_GetPacketQueueInfoOptions*,EOS_P2P_PacketQueueInfo* out) {
  *out={};return EOS_EResult::EOS_Success;
}
static EOS_EResult EOS_CALL Send(EOS_HP2P,const EOS_P2P_SendPacketOptions* o) {
  Check(o->DataLengthBytes<=kPacketLimit && o->bDisableAutoAcceptConnection==EOS_TRUE);
  const auto* p=static_cast<const uint8_t*>(o->Data);
  queue.push_back({o->LocalUserId,o->RemoteUserId,*o->SocketId,o->Channel,Packet(p,p+o->DataLengthBytes)});
  return EOS_EResult::EOS_Success;
}
static auto Find(EOS_ProductUserId user,const uint8_t* channel) {
  return std::find_if(queue.begin(),queue.end(),[&](const Message& m){return m.to==user && (!channel || m.channel==*channel);});
}
static EOS_EResult EOS_CALL Next(EOS_HP2P,const EOS_P2P_GetNextReceivedPacketSizeOptions* o,uint32_t* out) {
  auto it=Find(o->LocalUserId,o->RequestedChannel);if (it==queue.end()) return EOS_EResult::EOS_NotFound;
  *out=uint32_t(it->data.size());return EOS_EResult::EOS_Success;
}
static EOS_EResult EOS_CALL Receive(EOS_HP2P,const EOS_P2P_ReceivePacketOptions* o,EOS_ProductUserId* from,
                                   EOS_P2P_SocketId* socket,uint8_t* channel,void* data,uint32_t* size) {
  auto it=Find(o->LocalUserId,o->RequestedChannel);if (it==queue.end()) return EOS_EResult::EOS_NotFound;
  Check(it->data.size()<=o->MaxDataSizeBytes);*from=it->from;*socket=it->socket;*channel=it->channel;
  *size=uint32_t(it->data.size());std::memcpy(data,it->data.data(),*size);queue.erase(it);
  return EOS_EResult::EOS_Success;
}
static Bytes Frame(uint16_t from,uint16_t to,size_t size) {
  Bytes b(size,std::byte{0x5a});b[0]=std::byte{'R'};b[1]=std::byte{'B'};b[2]=std::byte{'N'};b[3]=std::byte{1};
  b[4]=std::byte(from>>8);b[5]=std::byte(from);b[6]=std::byte(to>>8);b[7]=std::byte(to);return b;
}
int main() {
  try {
    EOS_InitializeOptions init{};init.ApiVersion=EOS_INITIALIZE_API_LATEST;init.ProductName="RedBaronTest";init.ProductVersion="1";
    Check(EOS_Initialize(&init)==EOS_EResult::EOS_Success);
    const auto a=EOS_ProductUserId_FromString("00000000000000000000000000000001");
    const auto b=EOS_ProductUserId_FromString("00000000000000000000000000000002");
    const auto c=EOS_ProductUserId_FromString("00000000000000000000000000000003");
    const auto unknown=EOS_ProductUserId_FromString("00000000000000000000000000000004");
    P2PApi api{Accept,Close,QueueInfo,Send,Next,Receive};
    auto handle=reinterpret_cast<EOS_HP2P>(1);
    {
      EosWire host(handle,a,0,"TestSocket",api),one(handle,b,1,"TestSocket",api),two(handle,c,2,"TestSocket",api);
      std::array<EOS_ProductUserId,8> roster{a,b,c};
      auto invalid=roster;invalid[2]=b;Check(!host.SetRoster(invalid));
      Check(host.SetRoster(roster) && one.SetRoster(roster) && two.SetRoster(roster));
      Check(!host.SetMember(3,b) && !one.SetMember(3,b));
      auto data=Frame(1,2,32768);unsigned deliveries=0;
      two.packet=[&](uint16_t from,const Bytes& received,bool reliable){Check(from==1 && received==data && reliable);++deliveries;};
      Check(one.Send(2,data,true));Check(queue.size()>1);
      Check(host.Poll());Check(two.Poll());Check(deliveries==1 && queue.empty());
      data=Frame(1,2,1800);
      two.packet=[&](uint16_t from,const Bytes& received,bool reliable){Check(from==1 && received==data && !reliable);++deliveries;};
      Check(one.Send(2,data,false));Check(host.Poll());Check(two.Poll());Check(deliveries==2 && queue.empty());
      Check(!one.Send(2,Frame(0,2,30),true));
      auto bad=Fragment(2,Packet(20,0x5a));EOS_P2P_SocketId socket{};
      socket.ApiVersion=EOS_P2P_SOCKETID_API_LATEST;std::memcpy(socket.SocketName,"TestSocket",sizeof("TestSocket"));
      queue.push_back({unknown,a,socket,0,bad.front()});
      Check(host.Poll() && queue.empty());
      auto spoof=Frame(2,1,32);Packet spoof_bytes(spoof.size());
      std::transform(spoof.begin(),spoof.end(),spoof_bytes.begin(),[](std::byte value){return std::to_integer<uint8_t>(value);});
      for (const auto& fragment : Fragment(3,spoof_bytes)) queue.push_back({b,a,socket,0,fragment});
      Check(host.Poll() && queue.empty());
      host.RemoveMember(1);Check(!host.Ready(1));
    }
    Check(EOS_Shutdown()==EOS_EResult::EOS_Success);
    std::cout << "PASS: EOS adapter relays fragmented messages and rejects unknown peers\n";
    return 0;
  } catch(const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
