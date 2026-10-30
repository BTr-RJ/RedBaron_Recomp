#include "native_record.h"
#include <rex/cvar.h>
#include <iostream>
#include <stdexcept>
using namespace rex::system::xam;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Provider:LanOnlineProvider {
  bool connected=false,left=false;
  std::vector<LanRecord> Search(uint32_t title) override{LanRecord record;record.title=title;record.open_public=7;return {record};}
  std::vector<LanQosResult> Probe(const std::vector<LanQosTarget>& targets,uint32_t) override {
    std::vector<LanQosResult> out(targets.size());for(auto& r:out){r.contacted=true;r.data={1,2,3};}return out;
  }
  bool Connect(uint32_t address) override{connected=address==0x0acb1234;return connected;}
  bool CanHost() const override{return false;}
  void Leave(const LanSessionId&) override{left=true;}
};
int main() {
  try {
    auto provider=std::make_shared<Provider>();LanSetOnlineProvider(provider);
    auto results=LanSearch(0x584109b5);Check(results.size()==1 && results[0].title==0x584109b5,"Native search reaches installed provider");
    Check(!LanCanHostMatch(),"Provider controls host eligibility");
    Check(LanOnlinePlayerLimit()==8,"EOS caps native player slots at eight");
    Check(LanEnsureConnected(0x0acb1234) && provider->connected,"Native connect reaches provider");
    Check(!LanEnsureConnected(0x0acb5678),"Failed admission propagates to native network");
    auto qos=LanQosProbe({LanQosTarget{}},1);Check(qos.size()==1 && qos[0].contacted && qos[0].data.size()==3,"Native QoS reaches provider");
    LanLeaveOnline({});Check(provider->left,"Native departure reaches provider");
    LanSetOnlineProvider({});Check(LanEnsureConnected(0x0acb5678),"Offline/LAN path preserved");
    Check(LanOnlinePlayerLimit()==32,"Legacy session capacity preserved without provider");
    LanRecord original;original.title=0x584109b5;original.open_public=7;original.filled_public=1;
    original.contexts={{1,2},{3,4}};original.properties={{0x60000001,std::vector<uint8_t>(1024,0xb3)}};
    for(size_t i=0;i<original.info.size();++i)original.info[i]=uint8_t(i);
    const auto encoded=redbaron::eos::EncodeNativeRecord(original);auto decoded=redbaron::eos::DecodeNativeRecord(encoded);
    Check(decoded && decoded->info==original.info && decoded->properties==original.properties && decoded->contexts==original.contexts,"Native session data roundtrips without loss");
    Check(!redbaron::eos::DecodeNativeRecord(encoded.substr(0,encoded.size()-2)),"Truncated public record rejected");
    std::cout<<"Native online provider dispatch and record tests passed\n";return 0;
  }catch(const std::exception& e){LanSetOnlineProvider({});std::cerr<<e.what()<<'\n';return 1;}
}
