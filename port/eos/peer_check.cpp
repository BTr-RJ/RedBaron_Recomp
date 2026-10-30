// Opt-in live diagnostic with a separate OS/device identity. No game assets.
#include "platform.h"
#include "lobby_directory.h"
#include "lobby_join.h"
#include "lobby_roster.h"
#include "address_wire.h"
#include "eos_wire.h"
#include "../network/session_record.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
using namespace redbaron::eos;
using redbaron::net::Bytes;
using Clock=std::chrono::steady_clock;
struct Peer {
  Platform platform;
  EOS_HLobby lobby{};EOS_HP2P p2p{};EOS_HLobbySearch search{};
  LobbyDirectory directory;std::unique_ptr<LobbyJoin> join;
  std::unique_ptr<EosWire> wire;std::unique_ptr<AddressWire> addressed;
  EOS_NotificationId connection=EOS_INVALID_NOTIFICATIONID;
  bool found=false,relayed=false,accepted=false;EOS_EResult search_result{};
  Bytes tcp,udp;uint16_t host=0,local=0;
  explicit Peer(const char* config):platform(config,"RedBaron Peer Check"){}
  ~Peer(){
    addressed.reset();wire.reset();
    if(join && join->state()==LobbyJoin::State::Joined){join->Leave();auto end=Clock::now()+std::chrono::seconds(8);
      while(join->state()==LobbyJoin::State::Leaving && Clock::now()<end){platform.Tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
    if(connection!=EOS_INVALID_NOTIFICATIONID)EOS_P2P_RemoveNotifyPeerConnectionEstablished(p2p,connection);
    directory.Clear();if(search)EOS_LobbySearch_Release(search);
    platform.Release(); // Outstanding callbacks cannot outlive their contexts.
  }
  void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
  template<class F> void Wait(F ready,int seconds,const char* error){
    const auto end=Clock::now()+std::chrono::seconds(seconds);
    while(Clock::now()<end){if(ready())return;platform.Tick();if(wire)Check(wire->Poll(),"Peer transport failed");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    throw std::runtime_error(error);
  }
  Bytes Frame(uint8_t kind,uint16_t source=0,uint16_t destination=0){
    Bytes b(20);b[0]=std::byte{'R'};b[1]=std::byte{'B'};b[2]=std::byte{'N'};b[3]=std::byte{1};
    auto put=[&](size_t at,uint16_t n){b[at]=std::byte(n>>8);b[at+1]=std::byte(n);};
    put(4,local);put(6,host);b[8]=std::byte(kind);put(10,source);b[15]=std::byte{1};put(16,destination);put(18,kind==1?0:local);return b;
  }
  void Run(const char* bucket,bool force_relay){
    Wait([&]{return platform.LoginDone();},45,"Login timeout");Check(platform.LoggedIn(),"Second-device login failed");
    lobby=EOS_Platform_GetLobbyInterface(platform.handle());p2p=EOS_Platform_GetP2PInterface(platform.handle());
    if(force_relay){EOS_P2P_SetRelayControlOptions relay{};relay.ApiVersion=EOS_P2P_SETRELAYCONTROL_API_LATEST;relay.RelayControl=EOS_ERelayControl::EOS_RC_ForceRelays;
      Check(EOS_P2P_SetRelayControl(p2p,&relay)==EOS_EResult::EOS_Success,"Cannot force relay");}
    EOS_P2P_AddNotifyPeerConnectionEstablishedOptions notify{};notify.ApiVersion=EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST;notify.LocalUserId=platform.user();
    connection=EOS_P2P_AddNotifyPeerConnectionEstablished(p2p,&notify,this,[](const EOS_P2P_OnPeerConnectionEstablishedInfo* data){
      auto& p=*static_cast<Peer*>(data->ClientData);
      if(data->SocketId && std::strcmp(data->SocketId->SocketName,"RedBaronQosV1") && data->NetworkType==EOS_ENetworkConnectionType::EOS_NCT_RelayedConnection)p.relayed=true;
      std::cout<<"EOS connection established, network type="<<int(data->NetworkType)<<std::endl;});
    EOS_Lobby_CreateLobbySearchOptions create{};create.ApiVersion=EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;create.MaxResults=32;
    Check(EOS_Lobby_CreateLobbySearch(lobby,&create,&search)==EOS_EResult::EOS_Success,"Search allocation failed");
    EOS_Lobby_AttributeData attr{};attr.ApiVersion=EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;attr.Key=EOS_LOBBY_SEARCH_BUCKET_ID;attr.ValueType=EOS_ELobbyAttributeType::EOS_AT_STRING;attr.Value.AsUtf8=bucket;
    EOS_LobbySearch_SetParameterOptions set{};set.ApiVersion=EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;set.Parameter=&attr;set.ComparisonOp=EOS_EComparisonOp::EOS_CO_EQUAL;
    Check(EOS_LobbySearch_SetParameter(search,&set)==EOS_EResult::EOS_Success,"Search filter failed");
    EOS_LobbySearch_FindOptions find{};find.ApiVersion=EOS_LOBBYSEARCH_FIND_API_LATEST;find.LocalUserId=platform.user();
    EOS_LobbySearch_Find(search,&find,this,[](const EOS_LobbySearch_FindCallbackInfo* data){auto& p=*static_cast<Peer*>(data->ClientData);p.found=true;p.search_result=data->ResultCode;});
    Wait([&]{return found;},25,"Search timeout");Check(search_result==EOS_EResult::EOS_Success && directory.Refresh(search),"Search failed");
    Check(directory.entries().size()==1,"Expected exactly one diagnostic room");
    const auto entry=directory.entries().front();Check(entry.owner!=platform.user(),"Two genuinely distinct identities are required");
    Check(redbaron::net::ValidSessionRecord(entry.record),"Invalid native session record");
    std::cout<<"PASS: second identity discovered public native session"<<std::endl;
    std::vector<uint8_t> request{'R','B','Q','1',1,2,3,4,5,6,7,8};
    auto hex=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(size_t i=40;i<56;i+=2)request.push_back(uint8_t(hex(entry.record[i])*16+hex(entry.record[i+1])));
    EOS_P2P_SocketId socket{};socket.ApiVersion=EOS_P2P_SOCKETID_API_LATEST;std::memcpy(socket.SocketName,"RedBaronQosV1",sizeof("RedBaronQosV1"));
    EOS_P2P_AcceptConnectionOptions accept{};accept.ApiVersion=EOS_P2P_ACCEPTCONNECTION_API_LATEST;accept.LocalUserId=platform.user();accept.RemoteUserId=entry.owner;accept.SocketId=&socket;
    Check(EOS_P2P_AcceptConnection(p2p,&accept)==EOS_EResult::EOS_Success,"QoS accept failed");
    EOS_P2P_SendPacketOptions send{};send.ApiVersion=EOS_P2P_SENDPACKET_API_LATEST;send.LocalUserId=platform.user();send.RemoteUserId=entry.owner;send.SocketId=&socket;send.Channel=2;
    send.Data=request.data();send.DataLengthBytes=uint32_t(request.size());send.Reliability=EOS_EPacketReliability::EOS_PR_ReliableUnordered;send.bAllowDelayedDelivery=EOS_TRUE;
    Check(EOS_P2P_SendPacket(p2p,&send)==EOS_EResult::EOS_Success,"QoS send failed");
    bool qos=false;Wait([&]{
      uint8_t channel=2,received_channel=0;uint32_t size=0;EOS_ProductUserId remote{};EOS_P2P_SocketId received{};std::array<uint8_t,1170> data{};
      EOS_P2P_ReceivePacketOptions receive{};receive.ApiVersion=EOS_P2P_RECEIVEPACKET_API_LATEST;receive.LocalUserId=platform.user();receive.RequestedChannel=&channel;receive.MaxDataSizeBytes=uint32_t(data.size());
      if(EOS_P2P_ReceivePacket(p2p,&receive,&remote,&received,&received_channel,data.data(),&size)==EOS_EResult::EOS_Success){
        Check(remote==entry.owner && size==27 && !std::memcmp(data.data(),"RBR1",4) && std::equal(request.begin()+4,request.end(),data.begin()+4) && data[20]==1 && data[22]==4 && data[23]==1 && data[26]==4,"QoS reply mismatch");qos=true;}
      return qos;},20,"Real EOS QoS timeout");
    std::cout<<"PASS: actual native QoS response over EOS"<<std::endl;
    join=std::make_unique<LobbyJoin>(lobby,platform.user());Check(join->Start(entry.id,directory.TakeDetails(entry.id)),"Join start failed");
    Wait([&]{return join->state()!=LobbyJoin::State::Joining;},25,"Join timeout");Check(join->state()==LobbyJoin::State::Joined,"Join rejected");
    Wait([&]{
      EOS_Lobby_CopyLobbyDetailsHandleOptions copy{};copy.ApiVersion=EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;copy.LocalUserId=platform.user();copy.LobbyId=entry.id.c_str();EOS_HLobbyDetails details{};
      if(EOS_Lobby_CopyLobbyDetailsHandle(lobby,&copy,&details)!=EOS_EResult::EOS_Success)return false;
      const auto roster=ReadLobbyRoster(details);bool ok=false;
      if(roster){auto own=ProductUserString(platform.user());auto it=std::find(roster->begin(),roster->end(),own);
        if(it!=roster->end()){
          uint64_t hash=14695981039346656037ull;for(unsigned char c:entry.id){hash^=c;hash*=1099511628211ull;}
          wire=std::make_unique<EosWire>(p2p,platform.user(),uint16_t(it-roster->begin()),"RB"+std::to_string(hash));
          addressed=std::make_unique<AddressWire>(*wire,*roster);ok=ApplyLobbyRoster(details,*wire);
          local=NetworkId(own);host=NetworkId((*roster)[0]);}}
      EOS_LobbyDetails_Release(details);return ok;},20,"Authenticated roster timeout");
    std::cout<<"PASS: joined native host and received authenticated roster"<<std::endl;
    wire->packet=[&](uint16_t peer,const Bytes& b,bool reliable){addressed->Receive(peer,b,reliable);};
    addressed->packet=[&](uint16_t peer,const Bytes& b,bool reliable){Check(peer==host && b.size()>=20,"Invalid echo sender");
      switch(std::to_integer<int>(b[8])){case 6:accepted=true;break;case 3:Check(reliable,"TCP reliability mismatch");tcp.insert(tcp.end(),b.begin()+20,b.end());break;case 1:Check(!reliable,"UDP reliability mismatch");udp.assign(b.begin()+20,b.end());break;case 5:throw std::runtime_error("Host reset diagnostic stream");}};
    Check(addressed->Send(host,Frame(2,0,21000),true),"TCP open failed");Wait([&]{return accepted;},20,"TCP accept timeout");
    Bytes payload(32768);for(size_t i=0;i<payload.size();++i)payload[i]=std::byte(i%251);
    for(size_t offset=0;offset<payload.size();offset+=16384){auto data=Frame(3);data.insert(data.end(),payload.begin()+offset,payload.begin()+offset+16384);Check(addressed->Send(host,data,true),"TCP data send failed");}
    Wait([&]{return tcp.size()>=payload.size();},30,"TCP echo timeout");Check(tcp==payload,"TCP echo corrupt");
    auto datagram=Frame(1,21001,21001);datagram.insert(datagram.end(),payload.begin(),payload.begin()+1800);
    Check(addressed->Send(host,datagram,false),"UDP send failed");Wait([&]{return !udp.empty();},20,"UDP echo timeout");
    Check(udp==Bytes(payload.begin(),payload.begin()+1800),"UDP echo corrupt");
    Check(!force_relay || relayed,"Forced relay was not observed");
    Check(addressed->Send(host,Frame(4),true),"TCP finish failed");
    std::cout<<"PASS: real EOS fragmented TCP 32768 bytes and UDP 1800 bytes echoed through native socket bridge; relay="<<relayed<<std::endl;
  }
};
int main(int argc,char** argv){try{if(argc!=4){std::cerr<<"Usage: peer_check <local-config> <unique-bucket> <auto|relay>\n";return 2;}Peer peer(argv[1]);peer.Run(argv[2],std::string_view(argv[3])=="relay");return 0;}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
