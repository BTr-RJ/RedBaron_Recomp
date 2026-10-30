#include "address_wire.h"
#include <algorithm>
#include <stdexcept>

namespace redbaron::eos {
namespace {
uint16_t Read(const redbaron::net::Bytes& b,size_t at) {
  return (uint16_t(b[at])<<8)|uint16_t(b[at+1]);
}
void Put(redbaron::net::Bytes& b,size_t at,uint16_t value) {
  b[at]=std::byte(value>>8);b[at+1]=std::byte(value);
}
uint64_t Identity(std::string_view id) {
  if(id.empty())return 0;
  uint64_t hash=14695981039346656037ull;
  for(unsigned char c:id){hash^=c;hash*=1099511628211ull;}return hash;
}
uint64_t Read64(const redbaron::net::Bytes& b,size_t at) {
  uint64_t value=0;for(size_t i=0;i<8;++i)value=(value<<8)|uint8_t(b[at+i]);return value;
}
void Put64(redbaron::net::Bytes& b,size_t at,uint64_t value) {
  for(size_t i=0;i<8;++i)b[at+i]=std::byte(value>>(56-8*i));
}
}
uint16_t NetworkId(std::string_view id) {
  if(id.empty())return 0;
  uint32_t hash=2166136261u;
  for(unsigned char c:id){hash^=c;hash*=16777619u;}
  return uint16_t(1+(hash%65534u));
}
AddressWire::AddressWire(redbaron::net::Wire& wire,const Roster& roster):wire_(wire) {
  if(!Update(roster))throw std::invalid_argument("Conflicting online network identities");
}
bool AddressWire::Update(const Roster& roster) {
  std::array<uint16_t,8> next{};
  if(wire_.id()>=8 || roster[wire_.id()].empty() || roster[0].empty())return false;
  for(size_t i=0;i<next.size();++i) {
    next[i]=NetworkId(roster[i]);
    if(next[i] && std::find(next.begin(),next.begin()+i,next[i])!=next.begin()+i)return false;
  }
  if(addresses_[wire_.id()] && addresses_[wire_.id()]!=next[wire_.id()])return false;
  addresses_=next;for(size_t i=0;i<8;++i)identities_[i]=Identity(roster[i]);return true;
}
int AddressWire::Slot(uint16_t address) const {
  if(!address)return -1;
  const auto it=std::find(addresses_.begin(),addresses_.end(),address);
  return it==addresses_.end() ? -1 : int(it-addresses_.begin());
}
uint16_t AddressWire::id() const{return addresses_[wire_.id()];}
bool AddressWire::Ready(uint16_t peer) const {const int slot=Slot(peer);return slot>=0 && wire_.Ready(uint16_t(slot));}
size_t AddressWire::Buffered(uint16_t peer) const {const int slot=Slot(peer);return slot<0 ? 1024*1024 : wire_.Buffered(uint16_t(slot));}
bool AddressWire::Send(uint16_t peer,const redbaron::net::Bytes& data,bool reliable) {
  const int to=Slot(peer);
  if(to<0 || data.size()<20 || Read(data,4)!=id() || Read(data,6)!=peer)return false;
  // Bind queued frames to identities as well as slots. A late packet from a
  // departed player must not become a packet from a new user reusing its slot.
  auto copy=data;copy.insert(copy.begin()+20,24,std::byte{});
  Put64(copy,20,identities_[wire_.id()]);Put64(copy,28,identities_[to]);
  Put(copy,4,wire_.id());Put(copy,6,uint16_t(to));
  if(data[8]!=std::byte{1}) {
    const int origin=Slot(Read(data,18));if(origin<0)return false;Put(copy,18,uint16_t(origin));Put64(copy,36,identities_[origin]);
  }
  return wire_.Send(uint16_t(to),copy,reliable);
}
void AddressWire::Receive(uint16_t peer,const redbaron::net::Bytes& data,bool reliable) {
  if(peer>=8 || !addresses_[peer] || data.size()<44 || Read(data,4)!=peer || Read(data,6)!=wire_.id() ||
     Read64(data,20)!=identities_[peer] || Read64(data,28)!=identities_[wire_.id()])return;
  auto copy=data;Put(copy,4,addresses_[peer]);Put(copy,6,id());
  if(data[8]!=std::byte{1}) {
    const auto origin=Read(data,18);if(origin>=8 || !addresses_[origin] || Read64(data,36)!=identities_[origin])return;Put(copy,18,addresses_[origin]);
  }
  copy.erase(copy.begin()+20,copy.begin()+44);
  if(packet)packet(addresses_[peer],copy,reliable);
}
}
