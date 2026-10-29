#include "fragmentation.h"
#include <eos_p2p_types.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace redbaron::eos;
static_assert(kPacketLimit == EOS_P2P_MAX_PACKET_SIZE);
void Require(bool value) { if (!value) throw std::runtime_error("fragmentation test failed"); }
int main() {
  try {
    for (size_t size : {size_t(1),size_t(1158),size_t(1159),size_t(8192),size_t(32768)}) {
      Packet source(size); for (size_t i=0;i<size;++i) source[i]=uint8_t(i*17);
      auto fragments=Fragment(1,source); std::reverse(fragments.begin(),fragments.end());
      Reassembler receiver; auto now=Reassembler::Clock::now(); std::optional<Packet> result;
      for (auto& fragment:fragments) { Require(fragment.size()<=kPacketLimit); result=receiver.Accept(2,fragment,now); }
      Require(result && *result==source);
    }
    Packet source(2500,42); auto parts=Fragment(3,source); auto now=Reassembler::Clock::now();
    Reassembler receiver;
    Require(!receiver.Accept(1,parts[0],now));
    Require(!receiver.Accept(1,parts[0],now));
    auto altered=parts[0];altered.back()^=1;
    Require(!receiver.Accept(1,altered,now));
    Require(!receiver.Accept(1,parts[1],now));
    Require(!receiver.Accept(1,parts[2],now));
    auto result=receiver.Accept(1,parts[0],now);
    Require(result && *result==source);
    Require(!receiver.Accept(1,parts[0],now));
    Require(!receiver.Accept(1,parts[1],now+std::chrono::seconds(6)));
    Require(!receiver.Accept(1,parts[2],now+std::chrono::seconds(6)));
    result=receiver.Accept(1,parts[0],now+std::chrono::seconds(6));
    Require(result && *result==source);
    Require(!receiver.Accept(1,parts[0],now)); receiver.RemovePeer(1);
    Require(!receiver.Accept(1,parts[1],now));
    auto bad=parts[0];bad[9]=0; Require(!receiver.Accept(2,bad,now));
    Reassembler limited;
    for (uint32_t id=1;id<=8;++id) Require(!limited.Accept(9,Fragment(id,source)[0],now));
    auto blocked=Fragment(9,source);
    Require(!limited.Accept(9,blocked[0],now));
    auto other=limited.Accept(10,Fragment(1,Packet{7})[0],now);
    Require(other && *other==Packet{7});
    limited.RemovePeer(9);
    for (const auto& part:blocked) result=limited.Accept(9,part,now);
    Require(result && *result==source);
    bool rejected=false; try {Fragment(4,Packet(32769));} catch(const std::invalid_argument&) {rejected=true;} Require(rejected);
    std::cout << "PASS: EOS-sized fragments, reordering, duplicates, expiry and limits\n";
    return 0;
  } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
