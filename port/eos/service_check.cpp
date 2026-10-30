// Opt-in live EOS/native-runtime check. Never included in unattended CTest.
// Host advertises authored diagnostic data, not game files or real gameplay.
#include <winsock2.h>
#include "online_service.h"
#include "platform.h"
#include "lobby_directory.h"
#include <rex/runtime.h>
#include <rex/logging.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
using namespace redbaron::eos;
using namespace rex::system::xam;
using Clock=std::chrono::steady_clock;
uint8_t test_namespace=198;
struct EchoSockets {
  SOCKET listener=INVALID_SOCKET,udp=INVALID_SOCKET;
  struct Stream {SOCKET socket;std::vector<char> pending;size_t offset=0;};
  std::vector<Stream> streams;
  explicit EchoSockets(uint16_t id){
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Echo Winsock failed");
    const auto bound=[&](bool tcp,uint16_t port){SOCKET s=socket(AF_INET,tcp?SOCK_STREAM:SOCK_DGRAM,0);
      sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(0x7f000000u|(uint32_t(test_namespace)<<16)|id);a.sin_port=htons(port);u_long one=1;
      if(s==INVALID_SOCKET || bind(s,reinterpret_cast<sockaddr*>(&a),sizeof(a)) || ioctlsocket(s,FIONBIO,&one)){if(s!=INVALID_SOCKET)closesocket(s);throw std::runtime_error("Echo bind failed");}return s;};
    listener=bound(true,21000);udp=bound(false,21001);if(listen(listener,8))throw std::runtime_error("Echo listen failed");
  }
  ~EchoSockets(){for(auto& s:streams)closesocket(s.socket);closesocket(listener);closesocket(udp);WSACleanup();}
  void Poll(){
    SOCKET s;while((s=accept(listener,nullptr,nullptr))!=INVALID_SOCKET){u_long one=1;ioctlsocket(s,FIONBIO,&one);streams.push_back({s,{}});}
    for(auto& stream:streams){
      char buffer[32768];const int n=recv(stream.socket,buffer,sizeof(buffer),0);if(n>0)stream.pending.insert(stream.pending.end(),buffer,buffer+n);
      if(stream.offset<stream.pending.size()){const int sent=send(stream.socket,stream.pending.data()+stream.offset,int(stream.pending.size()-stream.offset),0);if(sent>0)stream.offset+=sent;}
      if(stream.offset==stream.pending.size()){stream.pending.clear();stream.offset=0;}
    }
    char buffer[8192];sockaddr_in from{};int length=sizeof(from);
    const int n=recvfrom(udp,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&length);
    if(n>=0)sendto(udp,buffer,n,0,reinterpret_cast<sockaddr*>(&from),length);
  }
};
int main(int argc,char** argv) {
  try {
    if(argc==4 && std::string_view(argv[1])=="--browse") {
      Platform platform(argv[2],"RedBaron Diagnostic");
      auto end=Clock::now()+std::chrono::seconds(45);
      while(!platform.LoginDone() && Clock::now()<end){platform.Tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
      if(!platform.LoggedIn())throw std::runtime_error("Diagnostic login failed");
      const auto lobby=EOS_Platform_GetLobbyInterface(platform.handle());EOS_HLobbySearch search{};
      EOS_Lobby_CreateLobbySearchOptions create{};create.ApiVersion=EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;create.MaxResults=32;
      if(EOS_Lobby_CreateLobbySearch(lobby,&create,&search)!=EOS_EResult::EOS_Success)return 1;
      EOS_Lobby_AttributeData attr{};attr.ApiVersion=EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
      attr.Key=EOS_LOBBY_SEARCH_BUCKET_ID;attr.ValueType=EOS_ELobbyAttributeType::EOS_AT_STRING;attr.Value.AsUtf8=argv[3];
      EOS_LobbySearch_SetParameterOptions set{};set.ApiVersion=EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;set.Parameter=&attr;set.ComparisonOp=EOS_EComparisonOp::EOS_CO_EQUAL;
      if(EOS_LobbySearch_SetParameter(search,&set)!=EOS_EResult::EOS_Success)return 1;
      struct Result {bool done=false;EOS_EResult code=EOS_EResult::EOS_UnexpectedError;} result;
      EOS_LobbySearch_FindOptions find{};find.ApiVersion=EOS_LOBBYSEARCH_FIND_API_LATEST;find.LocalUserId=platform.user();
      EOS_LobbySearch_Find(search,&find,&result,[](const EOS_LobbySearch_FindCallbackInfo* data){auto& r=*static_cast<Result*>(data->ClientData);r.code=data->ResultCode;r.done=true;});
      end=Clock::now()+std::chrono::seconds(20);
      while(!result.done && Clock::now()<end){platform.Tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
      LobbyDirectory directory;
      const bool ok=result.done && result.code==EOS_EResult::EOS_Success && directory.Refresh(search);
      std::cout<<"Public native sessions: "<<(ok ? int(directory.entries().size()):-1)<<std::endl;
      EOS_LobbySearch_Release(search);return ok?0:1;
    }
    if((argc!=6 && argc!=7) || (std::string_view(argv[1])!="--host" && std::string_view(argv[1])!="--echo" && std::string_view(argv[1])!="--relay-echo")) {
      std::cerr<<"Usage: redbaron_eos_service_check --host <local-config> <test-userdata> <unique-bucket> <seconds> | --browse <local-config> <unique-bucket>\n";return 2;
    }
    const int seconds=std::stoi(argv[5]);if(seconds<15 || seconds>300)return 2;
    if(argc==7){const auto ns=std::stoi(argv[6]);if(ns<1 || ns>254)return 2;test_namespace=uint8_t(ns);}
    rex::InitLogging();std::atomic<unsigned> created=0,bridged=0,removed=0;
    OnlineOptions options;options.config=argv[2];options.bucket=argv[4];options.name_space=test_namespace;
    options.force_relay=std::string_view(argv[1])=="--relay-echo";
    options.log=[&](const std::string& message){
      std::cout<<message<<std::endl;
      if(message=="EOS public room created from the native game session")++created;
      if(message=="EOS gameplay socket bridge initialized")++bridged;
      if(message=="EOS room removed")++removed;
    };
    auto service=std::make_shared<OnlineService>(options);const auto id=service->network_id();
    const bool echo=std::string_view(argv[1])!="--host";
    auto sockets=echo?std::make_unique<EchoSockets>(id):nullptr;
    rex::cvar::SetFlagByName("redbaron_lan","true");rex::cvar::SetFlagByName("redbaron_p2p_namespace",std::to_string(test_namespace));
    rex::cvar::SetFlagByName("redbaron_p2p_id",std::to_string(id));
    rex::cvar::SetFlagByName("redbaron_lan_bind","127."+std::to_string(test_namespace)+"."+std::to_string(id>>8)+"."+std::to_string(id&255));
    rex::Runtime runtime({},std::filesystem::absolute(argv[3]));rex::RuntimeConfig config;config.tool_mode=true;
    if(runtime.Setup(std::move(config)))throw std::runtime_error("Diagnostic native runtime setup failed");
    LanSetOnlineProvider(service);
    LanRecord record;record.title=0x584109b5;record.open_public=7;record.filled_public=1;
    std::random_device random;for(size_t i=0;i<8;++i)record.info[i]=uint8_t(random());record.info[0]|=0x80;
    auto address=LanXnAddress();std::copy(address.begin(),address.end(),record.info.begin()+8);
    LanSessionId session;std::copy_n(record.info.begin(),8,session.begin());
    if(!LanPublish(record) || !LanQosListen(session,5,{1,2,3,4}))throw std::runtime_error("Native diagnostic publication failed");
    const auto start=Clock::now();bool withdrawn=false,full=false,reopened=false,hidden=false,visible=false;
    while(Clock::now()-start<std::chrono::seconds(seconds)) {
      const auto elapsed=std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-start).count();
      if(sockets)sockets->Poll();
      if(!echo && seconds>=45 && !full && elapsed>=12){record.open_public=0;record.filled_public=8;LanPublish(record);full=true;std::cout<<"Diagnostic session full; transport must remain active\n";}
      if(full && !reopened && elapsed>=17){record.open_public=7;record.filled_public=1;LanPublish(record);reopened=true;}
      if(reopened && !hidden && elapsed>=23){LanRemove(session);hidden=true;std::cout<<"Diagnostic session hidden; transport must remain active\n";}
      if(hidden && !visible && elapsed>=28){LanPublish(record);visible=true;}
      if(!withdrawn && Clock::now()-start>std::chrono::seconds(seconds-8)) {
        LanRemove(session);LanQosListen(session,16,{});LanLeaveOnline(session);withdrawn=true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    LanSetOnlineProvider({});service.reset();
    if(created!=1 || bridged!=1 || removed!=1)throw std::runtime_error("Native/EOS host lifecycle did not complete exactly once");
    std::cout<<"Live native/EOS host lifecycle passed; another player and gameplay are not tested by this check\n";return 0;
  }catch(const std::exception& e){LanSetOnlineProvider({});std::cerr<<e.what()<<std::endl;return 1;}
}
