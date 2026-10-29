#include <WinSock2.h>
#include <WS2tcpip.h>
#include "directory_responder.h"
#include "session_record.h"
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>
#include <vector>

namespace redbaron::net {
namespace {
struct Socket {
  SOCKET value=INVALID_SOCKET;
  Socket()=default;
  explicit Socket(SOCKET s):value(s){}
  ~Socket(){if(value!=INVALID_SOCKET)closesocket(value);}
  Socket(const Socket&)=delete;
  Socket& operator=(const Socket&)=delete;
  Socket(Socket&& o) noexcept:value(o.value){o.value=INVALID_SOCKET;}
  Socket& operator=(Socket&& o) noexcept {
    if(this!=&o) {if(value!=INVALID_SOCKET)closesocket(value);value=o.value;o.value=INVALID_SOCKET;}
    return *this;
  }
};
sockaddr_in Address(uint8_t ns,uint16_t id) {
  sockaddr_in out{};out.sin_family=AF_INET;out.sin_addr.s_addr=htonl(0x7f000000u|(uint32_t(ns)<<16)|id);
  out.sin_port=htons(43821);return out;
}
Socket Bind(uint8_t ns,uint16_t id) {
  Socket s(socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP));
  if(s.value==INVALID_SOCKET)throw std::runtime_error("Could not create discovery socket");
  int exclusive=1;setsockopt(s.value,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<char*>(&exclusive),sizeof(exclusive));
  const auto address=Address(ns,id);u_long nonblocking=1;
  if(bind(s.value,reinterpret_cast<const sockaddr*>(&address),sizeof(address)) || ioctlsocket(s.value,FIONBIO,&nonblocking))
    throw std::runtime_error("Could not bind discovery socket");
  return s;
}
}
struct DirectoryResponder::Impl {
  struct Entry {Socket socket;std::vector<uint8_t> record;};
  uint8_t ns;Socket listener;std::map<uint16_t,Entry> records;
  explicit Impl(uint8_t n):ns(n) {
    if(!n || n==255)throw std::invalid_argument("Invalid network namespace");
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Winsock initialization failed");
    try {listener=Bind(ns,0);} catch(...) {WSACleanup();throw;}
  }
  ~Impl(){records.clear();if(listener.value!=INVALID_SOCKET){closesocket(listener.value);listener.value=INVALID_SOCKET;}WSACleanup();}
};
DirectoryResponder::DirectoryResponder(uint8_t ns):impl_(std::make_unique<Impl>(ns)){}
DirectoryResponder::~DirectoryResponder()=default;
bool DirectoryResponder::Set(uint16_t id,std::string_view record) {
  if(id<256 || id>=288 || !ValidSessionRecord(record))return false;
  auto nibble=[](char c){return c<='9' ? c-'0' : c-'a'+10;};
  std::vector<uint8_t> data;data.reserve(record.size()/2);
  for(size_t i=0;i<record.size();i+=2)data.push_back(uint8_t(nibble(record[i])*16+nibble(record[i+1])));
  auto& entries=impl_->records;auto it=entries.find(id);
  if(it!=entries.end()){it->second.record=std::move(data);return true;}
  entries.emplace(id,Impl::Entry{Bind(impl_->ns,id),std::move(data)});return true;
}
void DirectoryResponder::Remove(uint16_t id){impl_->records.erase(id);}
void DirectoryResponder::Poll() {
  for(unsigned n=0;n<64;++n) {
    std::array<uint8_t,20> query{};sockaddr_in remote{};int length=sizeof(remote);
    const int count=recvfrom(impl_->listener.value,reinterpret_cast<char*>(query.data()),int(query.size()),0,
                             reinterpret_cast<sockaddr*>(&remote),&length);
    if(count<0)break;
    const auto ip=ntohl(remote.sin_addr.s_addr);
    if(count!=20 || (ip&0xffff0000u)!=(0x7f000000u|(uint32_t(impl_->ns)<<16)) ||
       !std::equal(query.begin(),query.begin()+8,reinterpret_cast<const uint8_t*>("RBLAN01\x01")) ||
       query[16]!=0x58 || query[17]!=0x41 || query[18]!=0x09 || query[19]!=0xb5)continue;
    for(const auto& [id,entry]:impl_->records) {
      auto packet=entry.record;std::copy(query.begin()+8,query.begin()+16,packet.begin()+8);
      sendto(entry.socket.value,reinterpret_cast<const char*>(packet.data()),int(packet.size()),0,
             reinterpret_cast<const sockaddr*>(&remote),length);
    }
  }
}
}
