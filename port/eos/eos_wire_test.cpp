#include <WinSock2.h>
#include "eos_wire.h"
#include "address_wire.h"
#include "../network/socket_bridge.h"
#include <eos_init.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <memory>

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
static void TestSocketBridges(const P2PApi& api,EOS_HP2P handle) {
  // Real loopback game sockets; only the EOS packet-delivery API is simulated.
  WSADATA data{};Check(WSAStartup(MAKEWORD(2,2),&data)==0);
  Roster names;std::array<EOS_ProductUserId,8> users{};
  std::array<std::unique_ptr<EosWire>,8> wires;
  std::array<std::unique_ptr<AddressWire>,8> addressed;
  std::array<std::unique_ptr<redbaron::net::SocketBridge>,8> bridges;
  for(int i=0;i<8;++i){names[i]="0000000000000000000000000000000"+std::to_string(i+1);users[i]=EOS_ProductUserId_FromString(names[i].c_str());}
  for(int i=0;i<8;++i) {
    wires[i]=std::make_unique<EosWire>(handle,users[i],uint16_t(i),"EightSocketTest",api);Check(wires[i]->SetRoster(users));
    addressed[i]=std::make_unique<AddressWire>(*wires[i],names);
    bridges[i]=std::make_unique<redbaron::net::SocketBridge>(*addressed[i],uint8_t(200+i));
    wires[i]->packet=[&,i](uint16_t peer,const Bytes& bytes,bool reliable){addressed[i]->Receive(peer,bytes,reliable);};
    addressed[i]->packet=[&,i](uint16_t peer,const Bytes& bytes,bool reliable){bridges[i]->Packet(peer,bytes,reliable);};
    for(int j=0;j<8;++j)if(i!=j)bridges[i]->Member(NetworkId(names[j]),true);
  }
  const auto address=[&](int instance,int peer,uint16_t port) {
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(0x7f000000u|(uint32_t(200+instance)<<16)|NetworkId(names[peer]));a.sin_port=htons(port);return a;
  };
  const auto bound=[&](int instance,uint16_t port,bool tcp) {
    SOCKET s=socket(AF_INET,tcp?SOCK_STREAM:SOCK_DGRAM,tcp?IPPROTO_TCP:IPPROTO_UDP);Check(s!=INVALID_SOCKET);
    const auto a=address(instance,instance,port);Check(bind(s,reinterpret_cast<const sockaddr*>(&a),sizeof(a))==0);return s;
  };
  SOCKET listener=bound(0,21000,true);Check(listen(listener,8)==0);u_long nonblocking=1;Check(ioctlsocket(listener,FIONBIO,&nonblocking)==0);
  std::array<SOCKET,8> clients{};clients.fill(INVALID_SOCKET);std::vector<SOCKET> accepted;
  for(int i=1;i<8;++i) {
    clients[i]=bound(i,0,true);const auto host=address(i,0,21000);Check(connect(clients[i],reinterpret_cast<const sockaddr*>(&host),sizeof(host))==0);
    Check(ioctlsocket(clients[i],FIONBIO,&nonblocking)==0);
    const char byte=char(i);Check(send(clients[i],&byte,1,0)==1);
  }
  std::array<bool,8> replied{};bool incoming[8]{};
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(std::chrono::steady_clock::now()<deadline && !std::all_of(replied.begin()+1,replied.end(),[](bool b){return b;})) {
    for(int i=0;i<8;++i){Check(wires[i]->Poll());bridges[i]->Poll();}
    SOCKET s;while((s=accept(listener,nullptr,nullptr))!=INVALID_SOCKET){Check(ioctlsocket(s,FIONBIO,&nonblocking)==0);accepted.push_back(s);}
    for(auto stream:accepted) {
      char byte=0;if(recv(stream,&byte,1,0)==1){Check(byte>=1 && byte<=7 && !incoming[int(byte)]);incoming[int(byte)]=true;Check(send(stream,&byte,1,0)==1);}
    }
    for(int i=1;i<8;++i){char byte=0;if(recv(clients[i],&byte,1,0)==1){Check(byte==i);replied[i]=true;}}
    Sleep(1);
  }
  Check(accepted.size()==7 && std::all_of(replied.begin()+1,replied.end(),[](bool b){return b;}));
  SOCKET first=bound(1,21001,false),last=bound(7,21001,false);Check(ioctlsocket(first,FIONBIO,&nonblocking)==0 && ioctlsocket(last,FIONBIO,&nonblocking)==0);
  auto destination=address(1,7,21001);std::array<char,1800> payload{};payload.fill('q');
  Check(sendto(first,payload.data(),int(payload.size()),0,reinterpret_cast<const sockaddr*>(&destination),sizeof(destination))==int(payload.size()));
  bool echoed=false;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(std::chrono::steady_clock::now()<deadline && !echoed) {
    for(int i=0;i<8;++i){Check(wires[i]->Poll());bridges[i]->Poll();}
    std::array<char,2048> buffer{};sockaddr_in from{};int length=sizeof(from);
    const int n=recvfrom(last,buffer.data(),int(buffer.size()),0,reinterpret_cast<sockaddr*>(&from),&length);
    if(n>=0){Check(n==int(payload.size()) && from.sin_addr.s_addr==address(7,1,21001).sin_addr.s_addr);Check(sendto(last,buffer.data(),n,0,reinterpret_cast<sockaddr*>(&from),length)==n);}
    const int response=recv(first,buffer.data(),int(buffer.size()),0);
    if(response>=0){Check(response==int(payload.size()) && std::equal(payload.begin(),payload.end(),buffer.begin()));echoed=true;}
    Sleep(1);
  }
  Check(echoed);closesocket(first);closesocket(last);closesocket(listener);
  for(auto s:accepted)closesocket(s);for(auto s:clients)if(s!=INVALID_SOCKET)closesocket(s);
  for(auto& b:bridges)b.reset();for(auto& a:addressed)a.reset();for(auto& w:wires)w.reset();queue.clear();WSACleanup();
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
    TestSocketBridges(api,handle);
    Check(EOS_Shutdown()==EOS_EResult::EOS_Success);
    std::cout << "PASS: EOS adapter relays fragmented messages and rejects unknown peers\n";
    return 0;
  } catch(const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
