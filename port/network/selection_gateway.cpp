#include <WinSock2.h>
#include "selection_gateway.h"
#include <chrono>
#include <deque>
#include <map>
#include <stdexcept>
#include <utility>

namespace redbaron::net {
namespace {
struct Socket {
  SOCKET value=INVALID_SOCKET;
  explicit Socket(SOCKET s=INVALID_SOCKET):value(s){}
  ~Socket(){if(value!=INVALID_SOCKET)closesocket(value);}
  Socket(const Socket&)=delete;Socket& operator=(const Socket&)=delete;
  Socket(Socket&& o) noexcept:value(std::exchange(o.value,INVALID_SOCKET)){}
  Socket& operator=(Socket&& o) noexcept {if(this!=&o){if(value!=INVALID_SOCKET)closesocket(value);value=std::exchange(o.value,INVALID_SOCKET);}return *this;}
};
Socket Listen(uint8_t ns,uint16_t room,uint16_t port) {
  Socket s(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));if(s.value==INVALID_SOCKET)throw std::runtime_error("Could not create selection listener");
  int one=1;setsockopt(s.value,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<char*>(&one),sizeof(one));
  sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(0x7f000000u|(uint32_t(ns)<<16)|room);a.sin_port=htons(port);
  u_long yes=1;if(bind(s.value,reinterpret_cast<sockaddr*>(&a),sizeof(a)) || listen(s.value,8) || ioctlsocket(s.value,FIONBIO,&yes))
    throw std::runtime_error("Could not bind selection listener");
  return s;
}
}
struct SelectionGateway::Impl {
  struct Listener {uint16_t room,port;Socket socket;};
  struct Pending {uint16_t room,port;Socket socket;std::chrono::steady_clock::time_point created;};
  uint8_t ns;uint16_t game;std::map<uint32_t,Listener> listeners;std::deque<Pending> pending;
  std::optional<uint16_t> selection;
  Impl(uint8_t n,uint16_t g):ns(n),game(g) {
    if(!n || n==255 || g>=256)throw std::invalid_argument("Invalid local game address");
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Winsock initialization failed");
  }
  ~Impl(){pending.clear();listeners.clear();WSACleanup();}
};
SelectionGateway::SelectionGateway(uint8_t n,uint16_t g):impl_(std::make_unique<Impl>(n,g)){}
SelectionGateway::~SelectionGateway()=default;
bool SelectionGateway::Add(uint16_t room) {
  if(room<256 || room>=288)return false;
  auto& s=*impl_;
  if(s.listeners.contains((uint32_t(room)<<16)|21000))return true;
  std::map<uint32_t,Impl::Listener> created;
  for(uint16_t port:{21000,21001,21002})created.emplace((uint32_t(room)<<16)|port,Impl::Listener{room,port,Listen(s.ns,room,port)});
  s.listeners.merge(created);return true;
}
void SelectionGateway::Remove(uint16_t room) {
  auto& s=*impl_;std::erase_if(s.listeners,[room](const auto& x){return x.second.room==room;});
  if(s.selection && *s.selection==room)Cancel();
}
void SelectionGateway::Cancel(){impl_->pending.clear();impl_->selection.reset();}
std::optional<PendingConnection> SelectionGateway::Take() {
  auto& q=impl_->pending;if(q.empty())return std::nullopt;
  auto p=std::move(q.front());q.pop_front();
  return PendingConnection{p.room,p.port,static_cast<uintptr_t>(std::exchange(p.socket.value,INVALID_SOCKET))};
}
void SelectionGateway::Poll() {
  auto& s=*impl_;std::optional<uint16_t> notify;
  const auto expected=htonl(0x7f000000u|(uint32_t(s.ns)<<16)|s.game);
  for(auto& [key,listener]:s.listeners) for(unsigned n=0;n<8;++n) {
    sockaddr_in remote{};int length=sizeof(remote);Socket socket(accept(listener.socket.value,reinterpret_cast<sockaddr*>(&remote),&length));
    if(socket.value==INVALID_SOCKET)break;
    if(remote.sin_addr.s_addr!=expected || s.pending.size()>=32 || (s.selection && *s.selection!=listener.room))continue;
    if(!s.selection){s.selection=listener.room;notify=listener.room;}
    s.pending.push_back({listener.room,listener.port,std::move(socket),std::chrono::steady_clock::now()});
  }
  const auto expiry=std::chrono::steady_clock::now()-std::chrono::seconds(30);
  if(!s.pending.empty() && s.pending.front().created<expiry) {
    const auto room=s.selection;s.pending.clear();s.selection.reset();
    if(room && expired)expired(*room);
    notify.reset();
  }
  if(notify && selected)selected(*notify);
}
}
