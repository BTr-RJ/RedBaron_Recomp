#include "native_record.h"
#include "matchmaking_policy.h"
#include "invitation_record.h"
#include <rex/cvar.h>
#include <iostream>
#include <stdexcept>
using namespace rex::system::xam;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Provider:LanOnlineProvider {
  bool connected=false,left=false;unsigned friends_shown=0;
  std::vector<LanRecord> Search(uint32_t title) override{LanRecord record;record.title=title;record.open_public=7;return {record};}
  std::vector<LanQosResult> Probe(const std::vector<LanQosTarget>& targets,uint32_t) override {
    std::vector<LanQosResult> out(targets.size());for(auto& r:out){r.contacted=true;r.data={1,2,3};}return out;
  }
  bool Connect(uint32_t address) override{connected=address==0x0acb1234;return connected;}
  bool CanHost() const override{return false;}
  void Leave(const LanSessionId&) override{left=true;}
  bool ShowFriends(uint32_t user) override{++friends_shown;return user==0;}
};
int main() {
  try {
    auto provider=std::make_shared<Provider>();LanSetOnlineProvider(provider);
    LanSessionId menu_session{};menu_session[0]=0x42;
    Check(!LanHandleLobbyInvite(0,true),"RT gameplay input passes through outside a lobby");
    LanHandleLobbyInvite(0,false);LanSetGameLobby(menu_session,0,true);
    Check(LanHandleLobbyInvite(0,true) && provider->friends_shown==1,"RT opens the EOS friends overlay in the native lobby");
    Check(LanHandleLobbyInvite(0,true) && provider->friends_shown==1,"Holding RT cannot repeatedly reopen the overlay");
    Check(!LanHandleLobbyInvite(0,false),"RT release does not generate an invitation action");
    Check(LanHandleLobbyInvite(0,true) && provider->friends_shown==2,"A second press can reopen EOS friends");
    LanSetGameLobby(menu_session,0,false);
    Check(!LanHandleLobbyInvite(0,true),"Starting gameplay restores the right trigger weapon binding");
    LanSessionId party_session{};party_session[0]=0x43;LanSetGameLobby(party_session,0,true,true);
    Check(!LanHandleLobbyInvite(0,true),"Persistent hangar party cannot steal RT during gameplay");
    LanRemoveGameLobby(menu_session);LanHandleLobbyInvite(0,false);
    Check(LanHandleLobbyInvite(0,true) && provider->friends_shown==3,"RT opens EOS friends in Player Match hangar");
    LanRemoveGameLobby(party_session);
    auto results=LanSearch(0x584109b5);Check(results.size()==1 && results[0].title==0x584109b5,"Native search reaches installed provider");
    Check(!LanCanHostMatch(),"Provider controls host eligibility");
    Check(LanOnlinePlayerLimit()==8,"EOS caps native player slots at eight");
    Check(LanEnsureConnected(0x0acb1234) && provider->connected,"Native connect reaches provider");
    Check(!LanEnsureConnected(0x0acb5678),"Failed admission propagates to native network");
    auto qos=LanQosProbe({LanQosTarget{}},1);Check(qos.size()==1 && qos[0].contacted && qos[0].data.size()==3,"Native QoS reaches provider");
    LanLeaveOnline({});Check(provider->left,"Native departure reaches provider");
    LanSetOnlineProvider({});Check(LanEnsureConnected(0x0acb5678),"Offline/LAN path preserved");
    Check(LanOnlinePlayerLimit()==32,"Legacy session capacity preserved without provider");
    LanRecord original;original.title=0x584109b5;original.open_public=7;original.filled_public=1;original.host_xuid=0x0009000012345600ull;
    original.contexts={{1,2},{3,4}};original.properties={{0x60000001,std::vector<uint8_t>(1024,0xb3)}};
    for(size_t i=0;i<original.info.size();++i)original.info[i]=uint8_t(i);
    const auto encoded=redbaron::eos::EncodeNativeRecord(original);auto decoded=redbaron::eos::DecodeNativeRecord(encoded);
    Check(decoded && decoded->info==original.info && decoded->properties==original.properties && decoded->contexts==original.contexts,"Native session data roundtrips without loss");
    Check(decoded->host_xuid==original.host_xuid,"Native inviter XUID survives the EOS session record");
    Check(!redbaron::eos::DecodeNativeRecord(encoded.substr(0,encoded.size()-2)),"Truncated public record rejected");
    original.open_public=0;original.filled_public=0;original.open_private=7;original.filled_private=1;
    const auto private_encoded=redbaron::eos::EncodeNativeRecord(original);
    auto private_decoded=redbaron::eos::DecodeNativeRecord(private_encoded);
    Check(private_decoded && private_decoded->open_private==7 && !private_decoded->open_public,"Invite-only sessions preserve native private slots");
    original.open_private=0;
    Check(redbaron::eos::EncodeNativeRecord(original).empty(),"A session with no free slots is not offered for a new invitation");
    {
      using namespace redbaron::eos;const auto now=std::chrono::steady_clock::now();
      LanRecord a,b,c;a.open_public=b.open_public=c.open_public=7;
      a.info[0]=1;a.info[11]=1;b.info[0]=2;b.info[11]=2;c.info[0]=3;c.info[11]=3;
      a.contexts[0x800B]=1;b.contexts[0x800B]=2;c.contexts[0x800B]=3;
      LanSessionId aid{},bid{},cid{};aid[0]=1;bid[0]=2;cid[0]=3;
      std::map<uint32_t,MatchPing> pings{{1,{aid,80,now+std::chrono::seconds(10)}},{2,{bid,15,now+std::chrono::seconds(10)}},{3,{cid,40,now+std::chrono::seconds(10)}}};
      std::vector<LanRecord> choices{a,b,c};
      Check(SelectBestMatch(choices,pings,now)==15 && choices.size()==1 && choices[0].info==b.info,"Quick matchmaking chooses the lowest measured ping across modes");
      b.private_session=true;choices={a,b,c};Check(SelectBestMatch(choices,pings,now)==40,"Private sessions cannot enter public matchmaking");
      b.private_session=false;b.open_public=0;choices={a,b,c};Check(SelectBestMatch(choices,pings,now)==40,"Full hosts are excluded");
      pings[3].expires=now;choices={a,c};Check(SelectBestMatch(choices,pings,now)==80,"Expired measurements cannot beat a live host");
      pings[1].session[0]=9;choices={a};Check(!SelectBestMatch(choices,pings,now),"A reused host address cannot reuse the old session's ping");
    }
    {
      using namespace redbaron::eos;LanRecord record;record.title=0x584109B5;
      record.info[0]=0x80;record.info[8]=10;record.info[9]=203;record.info[10]=0x12;record.info[11]=0x34;
      record.host_xuid=0x00090000AABBCC00ull;
      const auto packed=PackNativeInvitation(record,0x0009000000001234ull);
      Check(packed && packed->size()==84 && (*packed)[6]==0x12 && (*packed)[7]==0x34,"Native invitation preserves the signed-in invitee with big endian ABI");
      Check((*packed)[12]==0xAA && (*packed)[13]==0xBB && (*packed)[14]==0xCC,"Invitation uses the host's actual native XUID");
      Check((*packed)[16]==0x58 && (*packed)[17]==0x41 && (*packed)[18]==9 && (*packed)[19]==0xB5 && (*packed)[83]==1,"Native invitation has title and game-invite flag at the observed offsets");
      Check(std::equal(record.info.begin(),record.info.end(),packed->begin()+20),"Private invite carries the exact native session key/address record");
      record.title=1;Check(!PackNativeInvitation(record,0x0009000000001234ull),"Invitation from a different title is rejected");
      record.title=0x584109B5;record.info[9]=0;Check(!PackNativeInvitation(record,0x0009000000001234ull),"Non-EOS invitation address is rejected");
    }
    std::cout<<"Native online provider dispatch and record tests passed\n";return 0;
  }catch(const std::exception& e){LanSetOnlineProvider({});std::cerr<<e.what()<<'\n';return 1;}
}
