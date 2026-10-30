#include "address_wire.h"
#include <iostream>
#include <map>
#include <stdexcept>
using namespace redbaron::eos;
using redbaron::net::Bytes;
void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
uint16_t Read(const Bytes& b,size_t at){return (uint16_t(b[at])<<8)|uint16_t(b[at+1]);}
void Put(Bytes& b,size_t at,uint16_t value){b[at]=std::byte(value>>8);b[at+1]=std::byte(value);}
struct FakeWire: redbaron::net::Wire {
  uint16_t local;Bytes sent;
  explicit FakeWire(uint16_t id):local(id){}
  bool Send(uint16_t,const Bytes& b,bool) override{sent=b;return true;}
  bool Ready(uint16_t id) const override{return id<8 && id!=local;}
  size_t Buffered(uint16_t) const override{return 0;}
  uint16_t id() const override{return local;}
};
int main() {
  try {
    Roster roster{"host","client1","client2","client3","client4","client5","client6","client7"};
    for(uint16_t origin=0;origin<8;++origin)for(uint16_t destination=0;destination<8;++destination)if(origin!=destination) {
      FakeWire source(origin),target(destination);AddressWire a(source,roster),b(target,roster);
      for(uint8_t kind:{1,2,3,4,5,6}) {
        Bytes message(40,std::byte{0x7b});message[8]=std::byte(kind);
        Put(message,4,a.id());Put(message,6,b.id());Put(message,18,a.id());
        Check(a.Send(b.id(),message,kind!=1),"Sending a valid mapped frame");
        Check(Read(source.sent,4)==origin && Read(source.sent,6)==destination,"Routing uses assigned EOS slots");
        bool received=false;b.packet=[&](uint16_t peer,const Bytes& bytes,bool reliable){
          received=true;Check(peer==a.id() && bytes==message && reliable==(kind!=1),"Roundtrip preserves game payload, addresses and TCP stream origin");
        };
        b.Receive(origin,source.sent,kind!=1);Check(received,"Frame delivered");
        Put(message,4,b.id());Check(!a.Send(b.id(),message,true),"Spoofed game sender rejected");
      }
    }
    // Same authenticated user keeps its network address in a different room.
    FakeWire first(1),second(7);AddressWire a(first,roster);
    std::swap(roster[1],roster[7]);AddressWire b(second,roster);Check(a.id()==b.id(),"Address stable across room assignments");
    std::map<uint16_t,std::string> seen;bool collision=false;
    for(int i=0;i<100000 && !collision;++i) {
      const auto name="identity"+std::to_string(i),id=std::to_string(NetworkId(name));
      auto [it,added]=seen.emplace(NetworkId(name),name);
      if(!added){Roster bad{it->second,name};FakeWire owner(0);try{AddressWire rejected(owner,bad);}catch(const std::invalid_argument&){collision=true;}}
    }
    Check(collision,"Network identity collision fails closed");
    {
      Roster before{"host","departing","remaining"};FakeWire sender(1),receiver(2);
      AddressWire from(sender,before),to(receiver,before);Bytes frame(20);frame[8]=std::byte{2};
      Put(frame,4,from.id());Put(frame,6,to.id());Put(frame,18,from.id());Check(from.Send(to.id(),frame,true),"Queue old member frame");
      before[1]="replacement";Check(to.Update(before),"Replace departed member");
      bool stale=false;to.packet=[&](uint16_t,const Bytes&,bool){stale=true;};to.Receive(1,sender.sent,true);
      Check(!stale,"Queued packets cannot impersonate a replacement member");
    }
    std::cout<<"Eight-player address and stream routing passed\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
