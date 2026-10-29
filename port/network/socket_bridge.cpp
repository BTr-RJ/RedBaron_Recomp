#include <WinSock2.h>
#include <WS2tcpip.h>
#include "transport.h"
#include "socket_bridge.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <map>
#include <utility>

namespace redbaron::net {
namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t kHeader = 20, kMaxPending = 1024*1024;
enum Kind : uint8_t { Datagram=1, Open=2, Data=3, Finish=4, Reset=5, Accepted=6 };
uint16_t U16(const Bytes& b, size_t p) { return (uint16_t(b[p]) << 8) | uint16_t(b[p+1]); }
uint32_t U32(const Bytes& b, size_t p) { return (uint32_t(U16(b,p)) << 16) | U16(b,p+2); }
void Put16(Bytes& b, size_t p, uint16_t n) { b[p]=std::byte(n>>8); b[p+1]=std::byte(n); }
void Put32(Bytes& b, size_t p, uint32_t n) { Put16(b,p,uint16_t(n>>16)); Put16(b,p+2,uint16_t(n)); }
bool GamePort(uint16_t p) { return p >= 21000 && p <= 21002; }
bool UdpPort(uint16_t p) { return GamePort(p) || p == 43821; }
bool WouldBlock() { const auto e=WSAGetLastError(); return e==WSAEWOULDBLOCK || e==WSAEINPROGRESS; }
void Nonblocking(SOCKET s) { u_long yes=1; if (ioctlsocket(s,FIONBIO,&yes)) throw std::runtime_error("Could not configure loopback socket"); }
sockaddr_in Address(uint8_t ns, uint16_t id, uint16_t port) {
  sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(0x7F000000u | (uint32_t(ns)<<16) | id); a.sin_port=htons(port); return a;
}
SOCKET Bound(uint8_t ns, uint16_t id, uint16_t port, bool tcp) {
  auto s=socket(AF_INET,tcp?SOCK_STREAM:SOCK_DGRAM,tcp?IPPROTO_TCP:IPPROTO_UDP);
  if (s==INVALID_SOCKET) throw std::runtime_error("Could not create loopback socket");
  auto address=Address(ns,id,port); int exclusive=1;
  setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<char*>(&exclusive),sizeof(exclusive));
  if (bind(s,reinterpret_cast<sockaddr*>(&address),sizeof(address))) {
    closesocket(s); throw std::runtime_error("Loopback port is in use. Close the previous online session");
  }
  if (tcp) { int one=1; setsockopt(s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char*>(&one),sizeof(one)); }
  Nonblocking(s); return s;
}
struct Socket {
  SOCKET s=INVALID_SOCKET;
  Socket()=default;
  explicit Socket(SOCKET value):s(value){}
  Socket(const Socket&)=delete;
  Socket& operator=(const Socket&)=delete;
  Socket(Socket&& other) noexcept :s(std::exchange(other.s,INVALID_SOCKET)){}
  Socket& operator=(Socket&& other) noexcept {
    if(s!=INVALID_SOCKET) closesocket(s); s=std::exchange(other.s,INVALID_SOCKET); return *this;
  }
  ~Socket(){if(s!=INVALID_SOCKET) closesocket(s);}
};
struct Endpoint { Socket socket; uint16_t peer, port; bool tcp; Clock::time_point used=Clock::now(); };
struct Stream {
  Socket socket; uint16_t peer, origin; uint32_t id;
  bool connecting=false, accepted=false, local_end=false, remote_end=false, shutdown=false;
  Bytes pending; size_t offset=0;
  Clock::time_point started=Clock::now();
};
}
struct SocketBridge::Impl {
  Wire& wire; uint8_t ns; uint32_t next_id=1; bool diagnostics;
  std::map<uint64_t,Endpoint> endpoints;
  std::map<uint64_t,Stream> streams;
  std::map<std::pair<uint16_t,uint16_t>,Clock::time_point> udp_returns;
  Impl(Wire& t,uint8_t n,bool log):wire(t),ns(n),diagnostics(log) {
    if(n==0 || n==255) throw std::runtime_error("Invalid local network namespace");
    WSADATA data{}; if(WSAStartup(MAKEWORD(2,2),&data)) throw std::runtime_error("Winsock initialization failed");
  }
  ~Impl(){ streams.clear(); endpoints.clear(); WSACleanup(); }
  Endpoint& EndpointFor(uint16_t peer,uint16_t port,bool tcp) {
    uint64_t key=(uint64_t(peer)<<17)|(uint64_t(port)<<1)|tcp;
    auto it=endpoints.find(key); if(it!=endpoints.end()) return it->second;
    if(endpoints.size()>=256) throw std::runtime_error("Too many UDP routes");
    Endpoint e{Socket(Bound(ns,peer,port,tcp)),peer,port,tcp};
    if(tcp && listen(e.socket.s,16)) throw std::runtime_error("Could not listen on loopback socket");
    return endpoints.emplace(key,std::move(e)).first->second;
  }
  Bytes Header(uint16_t to,Kind kind,uint16_t source=0,uint16_t destination=0,uint32_t id=0,uint16_t origin=0) {
    Bytes b(kHeader); b[0]=std::byte{'R'}; b[1]=std::byte{'B'}; b[2]=std::byte{'N'}; b[3]=std::byte{1};
    Put16(b,4,wire.id()); Put16(b,6,to); b[8]=std::byte(kind); Put16(b,10,source);
    Put32(b,12,id); Put16(b,16,destination); Put16(b,18,origin); return b;
  }
  bool Control(Stream& stream,Kind kind) {
    return wire.Send(stream.peer,Header(stream.peer,kind,0,0,stream.id,stream.origin),true);
  }
  void Erase(uint64_t key,bool notify=true) {
    auto it=streams.find(key); if(it==streams.end())return;
    if(diagnostics)std::cerr<<"TCP close peer="<<it->second.peer<<" key="<<key<<" accepted="<<it->second.accepted<<" ends="<<it->second.local_end<<it->second.remote_end<<" pending="<<(it->second.pending.size()-it->second.offset)<<"\n";
    if(notify)Control(it->second,Reset); streams.erase(it);
  }
  void Packet(uint16_t peer,const Bytes& bytes,bool reliable) {
    if(bytes.size()<kHeader) return;
    const auto kind=Kind(bytes[8]);
    const auto source=U16(bytes,10), destination=U16(bytes,16), origin=U16(bytes,18);
    const uint32_t id=U32(bytes,12); const auto key=(uint64_t(origin)<<32)|id;
    if(kind==Datagram) {
      if(reliable || bytes.size()>8192+kHeader || !source || !destination) return;
      if(!UdpPort(destination) && !udp_returns.contains({peer,destination})) return;
      auto& endpoint=EndpointFor(peer,source,false); endpoint.used=Clock::now();
      auto target=redbaron::net::Address(ns,wire.id(),destination);
      sendto(endpoint.socket.s,reinterpret_cast<const char*>(bytes.data()+kHeader),int(bytes.size()-kHeader),0,
             reinterpret_cast<sockaddr*>(&target),sizeof(target)); return;
    }
    if(!reliable) return;
    if(kind==Open) {
      if(diagnostics)std::cerr<<"TCP incoming peer="<<peer<<" port="<<destination<<" key="<<key<<"\n";
      if(bytes.size()!=kHeader || origin!=peer || !id || !GamePort(destination) || streams.contains(key) || streams.size()>=128) return;
      Stream s{Socket(Bound(ns,peer,0,true)),peer,origin,id};
      auto target=redbaron::net::Address(ns,wire.id(),destination);
      const int result=connect(s.socket.s,reinterpret_cast<sockaddr*>(&target),sizeof(target));
      if(result && !WouldBlock()) { if(diagnostics)std::cerr<<"TCP connect error="<<WSAGetLastError()<<"\n";Control(s,Reset);return; }
      s.connecting=result!=0; s.accepted=!s.connecting;
      if(s.accepted)Control(s,Accepted);
      streams.emplace(key,std::move(s)); return;
    }
    auto it=streams.find(key); if(it==streams.end() || it->second.peer!=peer) return;
    auto& s=it->second;
    if(kind==Reset) { Erase(key,false); return; }
    if(kind==Accepted && bytes.size()==kHeader) s.accepted=true;
    else if(kind==Finish && bytes.size()==kHeader) s.remote_end=true;
    else if(kind==Data && !s.remote_end && bytes.size()>kHeader) {
      if(s.pending.size()-s.offset + bytes.size()-kHeader > kMaxPending) { if(diagnostics)std::cerr<<"TCP pending overflow\n";Erase(key);return; }
      if(s.offset) {s.pending.erase(s.pending.begin(),s.pending.begin()+s.offset);s.offset=0;}
      s.pending.insert(s.pending.end(),bytes.begin()+kHeader,bytes.end());
    }
  }
  void Poll() {
    // Short, nonblocking polls: no sleeping while holding a game socket or callback.
    std::array<char,16384> buffer{};
    const auto own=redbaron::net::Address(ns,wire.id(),0).sin_addr.s_addr;
    for(auto& [key,e]:endpoints) {
      if(!wire.Ready(e.peer))continue;
      if(e.tcp) {
        for(int n=0;n<8;++n) {
          sockaddr_in remote{};int length=sizeof(remote);
          SOCKET accepted=accept(e.socket.s,reinterpret_cast<sockaddr*>(&remote),&length);
          if(accepted==INVALID_SOCKET)break;
          if(remote.sin_addr.s_addr!=own || streams.size()>=128 || next_id==0){closesocket(accepted);continue;}
          Nonblocking(accepted);int one=1;setsockopt(accepted,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char*>(&one),sizeof(one));
          Stream s{Socket(accepted),e.peer,wire.id(),next_id++};
          if(diagnostics)std::cerr<<"TCP outgoing peer="<<e.peer<<" port="<<e.port<<" key="<<((uint64_t(s.origin)<<32)|s.id)<<"\n";
          auto request=Header(e.peer,Open,0,e.port,s.id,s.origin);
          if(!wire.Send(e.peer,request,true))continue;
          streams.emplace((uint64_t(s.origin)<<32)|s.id,std::move(s));
        }
      } else {
        for(int n=0;n<64;++n) {
          sockaddr_in remote{};int length=sizeof(remote);
          int size=recvfrom(e.socket.s,buffer.data(),8192,0,reinterpret_cast<sockaddr*>(&remote),&length);
          if(size<0){if(diagnostics && !WouldBlock())std::cerr<<"UDP receive error="<<WSAGetLastError()<<" peer="<<e.peer<<" port="<<e.port<<"\n";break;}
          if(remote.sin_addr.s_addr!=own)continue;
          e.used=Clock::now();const auto port=ntohs(remote.sin_port);
          udp_returns[{e.peer,port}]=Clock::now();
          auto b=Header(e.peer,Datagram,port,e.port);
          b.insert(b.end(),reinterpret_cast<std::byte*>(buffer.data()),reinterpret_cast<std::byte*>(buffer.data()+size));
          wire.Send(e.peer,b,false);
        }
      }
    }
    std::vector<uint64_t> remove, finished;
    for(auto& [key,s]:streams) {
      if(!wire.Ready(s.peer)){remove.push_back(key);continue;}
      if(!s.accepted && Clock::now()-s.started>std::chrono::seconds(10)){if(diagnostics)std::cerr<<"TCP open timeout key="<<key<<"\n";remove.push_back(key);continue;}
      if(s.connecting) {
        WSAPOLLFD poll{s.socket.s,POLLWRNORM,0};
        if(WSAPoll(&poll,1,0)>0) {
          int error=0,length=sizeof(error);getsockopt(s.socket.s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length);
          if(error){if(diagnostics)std::cerr<<"TCP async connect error="<<error<<" key="<<key<<"\n";remove.push_back(key);continue;}
          s.connecting=false;s.accepted=true;
          if(!Control(s,Accepted)){remove.push_back(key);continue;}
        }
      }
      if(!s.accepted)continue;
      if(s.offset<s.pending.size()) {
        int written=send(s.socket.s,reinterpret_cast<const char*>(s.pending.data()+s.offset),int(s.pending.size()-s.offset),0);
        if(written>0)s.offset+=written;
        else if(written<0 && !WouldBlock()){remove.push_back(key);continue;}
        if(s.offset==s.pending.size()){s.pending.clear();s.offset=0;}
      }
      if(s.remote_end && s.pending.empty() && !s.shutdown){shutdown(s.socket.s,SD_SEND);s.shutdown=true;}
      if(!s.local_end && wire.Buffered(s.peer)<512*1024) {
        int size=recv(s.socket.s,buffer.data(),int(buffer.size()),0);
        if(size>0) {
          auto b=Header(s.peer,Data,0,0,s.id,s.origin);
          b.insert(b.end(),reinterpret_cast<std::byte*>(buffer.data()),reinterpret_cast<std::byte*>(buffer.data()+size));
          if(!wire.Send(s.peer,b,true)){remove.push_back(key);continue;}
        } else if(size==0) {s.local_end=true;if(!Control(s,Finish))remove.push_back(key);}
        else if(!WouldBlock())remove.push_back(key);
      }
      // FIN is ordered after the last data frame. A following RESET would erase
      // the receiver's still-buffered final bytes before its next socket poll.
      if(s.local_end && s.remote_end && s.pending.empty())finished.push_back(key);
    }
    for(auto key:remove)Erase(key);
    for(auto key:finished)Erase(key,false);
    const auto expiry=Clock::now()-std::chrono::seconds(60);
    std::erase_if(udp_returns,[&](const auto& item){return item.second<expiry;});
    std::erase_if(endpoints,[&](const auto& item){return !item.second.tcp && !UdpPort(item.second.port) && item.second.used<expiry;});
  }
};
SocketBridge::SocketBridge(Wire& t,uint8_t ns,bool diagnostics):impl_(std::make_unique<Impl>(t,ns,diagnostics)){}
SocketBridge::~SocketBridge()=default;
void SocketBridge::Member(uint16_t id,bool present) {
  auto& s=*impl_;
  if(present) {
    for(uint16_t port: {21000,21001,21002}) {s.EndpointFor(id,port,true);s.EndpointFor(id,port,false);}
    s.EndpointFor(id,43821,false);
  } else {
    std::erase_if(s.streams,[&](const auto& item){return item.second.peer==id;});
    std::erase_if(s.endpoints,[&](const auto& item){return item.second.peer==id;});
    std::erase_if(s.udp_returns,[&](const auto& item){return item.first.first==id;});
  }
}
void SocketBridge::Packet(uint16_t peer,const Bytes& b,bool reliable){impl_->Packet(peer,b,reliable);}
void SocketBridge::Poll(){impl_->Poll();}
std::string SocketBridge::Address(uint16_t peer) const {
  return "127."+std::to_string(impl_->ns)+"."+std::to_string(peer>>8)+"."+std::to_string(peer&255);
}
}
