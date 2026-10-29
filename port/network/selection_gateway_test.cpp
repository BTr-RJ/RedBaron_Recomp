#include <WinSock2.h>
#include "selection_gateway.h"
#include "selection_admission.h"
#include "socket_bridge.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace redbaron::net;
void Check(bool ok){if(!ok)throw std::runtime_error("Selection gateway test failed");}
struct TestWire : Wire {
  std::vector<Bytes> sent;
  bool ready=false;
  bool Send(uint16_t peer,const Bytes& b,bool reliable) override {Check(peer==0 && reliable);sent.push_back(b);return true;}
  bool Ready(uint16_t peer) const override{return peer==0 && ready;}
  size_t Buffered(uint16_t) const override{return 0;}
  uint16_t id() const override{return 1;}
};
int main(){
  try {
    SelectionGateway gateway(194,1);TestWire wire;SocketBridge bridge(wire,194);
    Check(gateway.Add(256));unsigned selected=0;
    SelectionAdmission admission(gateway,bridge,wire,[&](uint16_t room){Check(room==256);++selected;return true;});
    SOCKET game=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);Check(game!=INVALID_SOCKET);
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(0x7fc20001u);
    Check(bind(game,reinterpret_cast<sockaddr*>(&local),sizeof(local))==0);
    sockaddr_in room{};room.sin_family=AF_INET;room.sin_addr.s_addr=htonl(0x7fc20100u);room.sin_port=htons(21000);
    SOCKET unexpected=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);Check(unexpected!=INVALID_SOCKET);
    sockaddr_in other{};other.sin_family=AF_INET;other.sin_addr.s_addr=htonl(0x7fc20002u);
    Check(bind(unexpected,reinterpret_cast<sockaddr*>(&other),sizeof(other))==0);
    Check(connect(unexpected,reinterpret_cast<sockaddr*>(&room),sizeof(room))==0);
    admission.Poll();Check(selected==0);closesocket(unexpected);
    Check(connect(game,reinterpret_cast<sockaddr*>(&room),sizeof(room))==0);
    Check(send(game,"test",4,0)==4);admission.Poll();Check(selected==1 && admission.state()==SelectionAdmission::State::Waiting);
    Check(admission.Complete(true));admission.Poll();Check(wire.sent.empty());
    wire.ready=true;admission.Poll();Check(admission.state()==SelectionAdmission::State::Connected);
    Check(wire.sent.size()==1 && wire.sent[0][8]==std::byte{2});
    auto reply=wire.sent[0];reply[4]=std::byte{0};reply[5]=std::byte{0};reply[6]=std::byte{0};reply[7]=std::byte{1};reply[8]=std::byte{6};
    bridge.Packet(0,reply,true);bridge.Poll();
    Check(wire.sent.size()==2 && wire.sent[1].size()==24 && wire.sent[1][8]==std::byte{3});
    Check(wire.sent[1][20]==std::byte{'t'} && wire.sent[1][23]==std::byte{'t'});
    gateway.Cancel();closesocket(game);std::cout<<"PASS: selected TCP connection survives asynchronous room admission\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
