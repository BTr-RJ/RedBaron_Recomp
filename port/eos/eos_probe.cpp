#include <WinSock2.h>
#include <WS2tcpip.h>
#include <eos_init.h>
#include <eos_version.h>
#include <eos_sdk.h>
#include <eos_connect.h>
#include <eos_lobby.h>
#include "eos_wire.h"
#include "session_record.h"
#include "lobby_record.h"
#include "lobby_publisher.h"
#include "lobby_directory.h"
#include "directory_mapping.h"
#include "selection_join.h"
#include "../network/session_publisher.h"
#include "../network/directory_responder.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

struct DeviceFlow {
  EOS_HConnect connect{};
  EOS_EResult result=EOS_EResult::EOS_UnexpectedError;
  EOS_ProductUserId user{};
  bool done=false;
  static void EOS_CALL CreatedUser(const EOS_Connect_CreateUserCallbackInfo* data) {
    auto& self=*static_cast<DeviceFlow*>(data->ClientData);
    self.result=data->ResultCode;self.user=data->LocalUserId;self.done=true;
  }
  static void EOS_CALL LoggedIn(const EOS_Connect_LoginCallbackInfo* data) {
    auto& self=*static_cast<DeviceFlow*>(data->ClientData);
    if (data->ResultCode==EOS_EResult::EOS_InvalidUser && data->ContinuanceToken) {
      EOS_Connect_CreateUserOptions options{};
      options.ApiVersion=EOS_CONNECT_CREATEUSER_API_LATEST;
      options.ContinuanceToken=data->ContinuanceToken;
      EOS_Connect_CreateUser(self.connect,&options,&self,CreatedUser);
    } else {self.result=data->ResultCode;self.user=data->LocalUserId;self.done=true;}
  }
  static void EOS_CALL DeviceCreated(const EOS_Connect_CreateDeviceIdCallbackInfo* data) {
    auto& self=*static_cast<DeviceFlow*>(data->ClientData);
    if (data->ResultCode!=EOS_EResult::EOS_Success && data->ResultCode!=EOS_EResult::EOS_DuplicateNotAllowed) {
      self.result=data->ResultCode;self.done=true;return;
    }
    EOS_Connect_Credentials credentials{};
    credentials.ApiVersion=EOS_CONNECT_CREDENTIALS_API_LATEST;
    credentials.Type=EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    EOS_Connect_UserLoginInfo info{};
    info.ApiVersion=EOS_CONNECT_USERLOGININFO_API_LATEST;
    info.DisplayName="RedBaron Pilot";
    EOS_Connect_LoginOptions options{};
    options.ApiVersion=EOS_CONNECT_LOGIN_API_LATEST;
    options.Credentials=&credentials;options.UserLoginInfo=&info;
    EOS_Connect_Login(self.connect,&options,&self,LoggedIn);
  }
};

static std::string TestSession(bool large) {
  std::vector<uint8_t> data(104,0);
  const char prefix[]="RBLAN01";std::copy_n(prefix,7,data.begin());data[7]=2;
  data[16]=0x58;data[17]=0x41;data[18]=0x09;data[19]=0xb5;data[83]=8;
  auto put=[&](uint32_t value){for(int shift=24;shift>=0;shift-=8)data.push_back(uint8_t(value>>shift));};
  if(large) {
    data[99]=3;data[103]=8;
    for(uint32_t i=0;i<3;++i) {put(i);put(0);}
    for(uint32_t i=0;i<8;++i) {put(i);put(1000);data.insert(data.end(),1000,uint8_t(i));}
  }
  constexpr char digits[]="0123456789abcdef";std::string hex;hex.reserve(data.size()*2);
  for(const auto b:data) {hex.push_back(digits[b>>4]);hex.push_back(digits[b&15]);}
  return hex;
}
static bool TestDirectoryResponder(const std::vector<redbaron::eos::LobbyEntry>& entries) {
  redbaron::net::DirectoryResponder responder(198);
  redbaron::eos::DirectoryMapping mapping(responder);
  if(entries.size()!=1 || !mapping.Update(entries) || !mapping.LobbyAt(256) || *mapping.LobbyAt(256)!=entries[0].id)return false;
  const auto& record=entries[0].record;
  SOCKET query_socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
  if(query_socket==INVALID_SOCKET)return false;
  sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(0x7fc60001u);
  sockaddr_in destination{};destination.sin_family=AF_INET;destination.sin_addr.s_addr=htonl(0x7fc60000u);destination.sin_port=htons(43821);
  std::array<uint8_t,20> query{'R','B','L','A','N','0','1',1,1,2,3,4,5,6,7,8,0x58,0x41,0x09,0xb5};
  bool ok=bind(query_socket,reinterpret_cast<sockaddr*>(&local),sizeof(local))==0 &&
    sendto(query_socket,reinterpret_cast<const char*>(query.data()),int(query.size()),0,reinterpret_cast<sockaddr*>(&destination),sizeof(destination))==int(query.size());
  if(ok) {
    responder.Poll();fd_set reads;FD_ZERO(&reads);FD_SET(query_socket,&reads);timeval timeout{1,0};
    ok=select(0,&reads,nullptr,nullptr,&timeout)>0;
    if(ok) {
      std::array<uint8_t,8192> response{};sockaddr_in from{};int from_size=sizeof(from);
      const int n=recvfrom(query_socket,reinterpret_cast<char*>(response.data()),int(response.size()),0,reinterpret_cast<sockaddr*>(&from),&from_size);
      ok=n==int(record.size()/2) && (ntohl(from.sin_addr.s_addr)&0xffff)==256 &&
         std::equal(query.begin()+8,query.begin()+16,response.begin()+8);
    }
  }
  closesocket(query_socket);return ok;
}
namespace {
struct FakeWire : redbaron::net::Wire {
  bool ready=false;std::vector<redbaron::net::Bytes> sent;
  bool Send(uint16_t id,const redbaron::net::Bytes& data,bool reliable) override {
    if(id!=0 || !reliable)return false;sent.push_back(data);return true;
  }
  bool Ready(uint16_t id) const override {return ready && id==0;}
  size_t Buffered(uint16_t) const override {return 0;}
  uint16_t id() const override {return 1;}
};
void* join_context{};
EOS_Lobby_OnJoinLobbyCallback join_callback{};
void EOS_CALL FakeJoin(EOS_HLobby,const EOS_Lobby_JoinLobbyOptions*,void* context,EOS_Lobby_OnJoinLobbyCallback callback) {
  join_context=context;join_callback=callback;
}
void EOS_CALL FakeLeave(EOS_HLobby,const EOS_Lobby_LeaveLobbyOptions* options,void* context,EOS_Lobby_OnLeaveLobbyCallback callback) {
  EOS_Lobby_LeaveLobbyCallbackInfo result{};result.ClientData=context;result.LobbyId=options->LobbyId;result.ResultCode=EOS_EResult::EOS_Success;
  callback(&result);
}
}
static bool TestSelectionJoin(redbaron::eos::LobbyDirectory& directory,EOS_ProductUserId user) {
  if(directory.entries().size()!=1)return false;
  const std::string id=directory.entries()[0].id;
  redbaron::net::DirectoryResponder responder(198);
  redbaron::eos::DirectoryMapping mapping(responder);
  if(!mapping.Update(directory.entries()))return false;
  redbaron::net::SelectionGateway gateway(198,1);if(!gateway.Add(256))return false;
  FakeWire wire;redbaron::net::SocketBridge bridge(wire,198);
  redbaron::eos::LobbyJoin join(reinterpret_cast<EOS_HLobby>(1),user,{FakeJoin,FakeLeave,EOS_LobbyDetails_Release});
  redbaron::eos::SelectionJoin selection(gateway,bridge,wire,mapping,directory,join,[&](const std::string& room){
    if(room!=id)return false;wire.ready=true;return true;
  });
  SOCKET game=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(game==INVALID_SOCKET)return false;
  sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(0x7fc60001u);
  sockaddr_in destination{};destination.sin_family=AF_INET;destination.sin_addr.s_addr=htonl(0x7fc60100u);destination.sin_port=htons(21000);
  bool ok=bind(game,reinterpret_cast<sockaddr*>(&local),sizeof(local))==0 &&
          connect(game,reinterpret_cast<sockaddr*>(&destination),sizeof(destination))==0 && send(game,"test",4,0)==4;
  if(ok) {
    selection.Poll();ok=join.state()==redbaron::eos::LobbyJoin::State::Joining && join_callback;
    if(ok) {
      EOS_Lobby_JoinLobbyCallbackInfo result{};result.ClientData=join_context;result.ResultCode=EOS_EResult::EOS_Success;result.LobbyId=id.c_str();
      join_callback(&result);join_callback=nullptr;selection.Poll();selection.Poll();
      ok=selection.state()==redbaron::net::SelectionAdmission::State::Connected && wire.sent.size()==1 && wire.sent[0][8]==std::byte{2};
      selection.Cancel();selection.Poll();
    }
  }
  closesocket(game);return ok;
}

// The diagnostic owns and destroys only the lobby it creates.
struct LobbyFlow {
  EOS_HLobby lobby{};
  EOS_ProductUserId user{};
  EOS_HLobbySearch search{};
  std::unique_ptr<redbaron::eos::LobbyPublisher> publisher;
  std::unique_ptr<redbaron::net::SessionPublisher> source;
  SOCKET fake_game=INVALID_SOCKET;
  bool winsock=false;
  std::string id;
  std::string bucket;
  EOS_EResult result=EOS_EResult::EOS_UnexpectedError;
  EOS_EResult search_result=EOS_EResult::EOS_Success;
  bool search_requested=false;
  uint32_t search_count=0;
  unsigned search_attempts=0;
  unsigned record_stage=0;
  bool attribute_verified=false;
  bool waiting_for_publish=false;
  std::string test_record=TestSession(true);
  bool retry_search=false;
  std::chrono::steady_clock::time_point retry_at{};
  bool done=false;
  ~LobbyFlow() {
    source.reset();
    if(fake_game!=INVALID_SOCKET)closesocket(fake_game);
    if(winsock)WSACleanup();
  }
  bool StartSource() {
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return false;winsock=true;
    fake_game=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(fake_game==INVALID_SOCKET)return false;
    int exclusive=1;setsockopt(fake_game,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive));
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(0x7fc50000u);address.sin_port=htons(43821);
    u_long nonblocking=1;
    if(bind(fake_game,reinterpret_cast<sockaddr*>(&address),sizeof(address)) || ioctlsocket(fake_game,FIONBIO,&nonblocking))return false;
    source=std::make_unique<redbaron::net::SessionPublisher>([this](const std::string& record) {
      if(!publisher->Offer(record)) {search_result=EOS_EResult::EOS_InvalidParameters;Destroy();}
      else waiting_for_publish=true;
    },197);
    return true;
  }
  void PollSource() {
    if(!source)return;
    source->Poll();
    for(int n=0;n<16;++n) {
      std::array<uint8_t,20> query{};sockaddr_in remote{};int remote_size=sizeof(remote);
      const int got=recvfrom(fake_game,reinterpret_cast<char*>(query.data()),int(query.size()),0,reinterpret_cast<sockaddr*>(&remote),&remote_size);
      if(got<0)break;
      if(got!=20 || record_stage==2 || std::memcmp(query.data(),"RBLAN01\x01",8))continue;
      std::vector<uint8_t> response;response.reserve(test_record.size()/2);
      const auto nibble=[](char c){return c<='9' ? c-'0' : c-'a'+10;};
      for(size_t i=0;i<test_record.size();i+=2)response.push_back(uint8_t(nibble(test_record[i])*16+nibble(test_record[i+1])));
      std::copy(query.begin()+8,query.begin()+16,response.begin()+8);
      sendto(fake_game,reinterpret_cast<const char*>(response.data()),int(response.size()),0,reinterpret_cast<sockaddr*>(&remote),remote_size);
    }
  }
  static void EOS_CALL Destroyed(const EOS_Lobby_DestroyLobbyCallbackInfo* data) {
    auto& self=*static_cast<LobbyFlow*>(data->ClientData);
    self.result=data->ResultCode;self.done=true;
  }
  void Destroy() {
    EOS_Lobby_DestroyLobbyOptions options{};
    options.ApiVersion=EOS_LOBBY_DESTROYLOBBY_API_LATEST;
    options.LocalUserId=user;options.LobbyId=id.c_str();
    EOS_Lobby_DestroyLobby(lobby,&options,this,Destroyed);
  }
  static void EOS_CALL Found(const EOS_LobbySearch_FindCallbackInfo* data) {
    auto& self=*static_cast<LobbyFlow*>(data->ClientData);
    self.search_result=data->ResultCode;
    if (data->ResultCode==EOS_EResult::EOS_Success) {
      EOS_LobbySearch_GetSearchResultCountOptions options{};
      options.ApiVersion=EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
      self.search_count=EOS_LobbySearch_GetSearchResultCount(self.search,&options);
      redbaron::eos::LobbyDirectory directory;
      const bool directory_ok=directory.Refresh(self.search) &&
        (self.record_stage==2 ? directory.entries().empty() :
          directory.entries().size()==1 && directory.entries()[0].record==self.test_record && directory.entries()[0].id==self.id &&
          TestDirectoryResponder(directory.entries()) && TestSelectionJoin(directory,self.user));
      if (self.search_count) {
        EOS_LobbySearch_CopySearchResultByIndexOptions copy{};
        copy.ApiVersion=EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
        EOS_HLobbyDetails details{};
        if (EOS_LobbySearch_CopySearchResultByIndex(self.search,&copy,&details)==EOS_EResult::EOS_Success) {
          const auto record=redbaron::eos::ReadLobbyRecord(details);
          self.attribute_verified=self.record_stage==2 ? !record : record==self.test_record;
          self.attribute_verified=self.attribute_verified && directory_ok;
          if(self.attribute_verified && self.record_stage==1) {
            EOS_LobbyDetails_CopyAttributeByKeyOptions stale{};
            stale.ApiVersion=EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST;stale.AttrKey="RB_RECORD_52";
            EOS_Lobby_Attribute* old{};
            const auto result=EOS_LobbyDetails_CopyAttributeByKey(details,&stale,&old);
            if(old) EOS_Lobby_Attribute_Release(old);
            self.attribute_verified=result==EOS_EResult::EOS_NotFound;
          }
          EOS_LobbyDetails_Release(details);
        }
      }
    }
    if(self.attribute_verified && self.record_stage==0) {
      EOS_LobbySearch_Release(self.search);self.search=nullptr;
      self.record_stage=1;self.test_record=TestSession(false);self.attribute_verified=false;self.search_attempts=0;
    } else if(self.attribute_verified && self.record_stage==1) {
      EOS_LobbySearch_Release(self.search);self.search=nullptr;
      self.record_stage=2;self.attribute_verified=false;self.search_attempts=0;
    } else if (self.search_result==EOS_EResult::EOS_Success && !self.attribute_verified && self.search_attempts<5) {
      self.retry_search=true;self.retry_at=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    } else {
      EOS_LobbySearch_Release(self.search);self.search=nullptr;
      self.Destroy();
    }
  }
  void Find() {
    retry_search=false;++search_attempts;
    EOS_LobbySearch_FindOptions find{};
    find.ApiVersion=EOS_LOBBYSEARCH_FIND_API_LATEST;find.LocalUserId=user;
    EOS_LobbySearch_Find(search,&find,this,Found);
  }
  void BeginSearch() {
    EOS_Lobby_CreateLobbySearchOptions create{};
    create.ApiVersion=EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
    create.MaxResults=1;
    search_result=EOS_Lobby_CreateLobbySearch(lobby,&create,&search);
    if (search_result!=EOS_EResult::EOS_Success) {Destroy();return;}
    EOS_Lobby_AttributeData attribute{};
    attribute.ApiVersion=EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
    attribute.Key=EOS_LOBBY_SEARCH_BUCKET_ID;
    attribute.ValueType=EOS_ELobbyAttributeType::EOS_AT_STRING;
    attribute.Value.AsUtf8=bucket.c_str();
    EOS_LobbySearch_SetParameterOptions set{};
    set.ApiVersion=EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
    set.Parameter=&attribute;set.ComparisonOp=EOS_EComparisonOp::EOS_CO_EQUAL;
    search_result=EOS_LobbySearch_SetParameter(search,&set);
    if (search_result!=EOS_EResult::EOS_Success) {
      EOS_LobbySearch_Release(search);search=nullptr;Destroy();return;
    }
    Find();
  }
  void TickPublisher() {
    if(!waiting_for_publish)return;
    publisher->Poll();
    if(publisher->error()!=EOS_EResult::EOS_Success) {
      waiting_for_publish=false;search_result=publisher->error();Destroy();
    } else if(publisher->Synced()) {waiting_for_publish=false;BeginSearch();}
  }
  static void EOS_CALL Created(const EOS_Lobby_CreateLobbyCallbackInfo* data) {
    auto& self=*static_cast<LobbyFlow*>(data->ClientData);
    if (data->ResultCode!=EOS_EResult::EOS_Success || !data->LobbyId) {
      self.result=data->ResultCode;self.done=true;return;
    }
    self.id=data->LobbyId;
    std::cout << "EOS test lobby created\n";
    if (!self.search_requested) {self.Destroy();return;}
    self.publisher=std::make_unique<redbaron::eos::LobbyPublisher>(self.lobby,self.user,self.id);
    if(!self.StartSource()) {self.search_result=EOS_EResult::EOS_UnexpectedError;self.Destroy();}
  }
};

static std::map<std::string, std::string> ReadConfig(const char* path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Cannot read the EOS configuration file");
  std::map<std::string, std::string> values;
  std::string line;
  size_t total=0;
  bool first=true;
  while (std::getline(file,line)) {
    total+=line.size();
    if (total>4096) throw std::runtime_error("EOS configuration is too large");
    if (first && line.size()>=3 && line.compare(0,3,"\xEF\xBB\xBF")==0) line.erase(0,3);
    first=false;
    if (!line.empty() && line.back()=='\r') line.pop_back();
    if (line.empty() || line[0]=='#') continue;
    const auto equal=line.find('=');
    if (equal==std::string::npos || equal==0 || equal+1==line.size())
      throw std::runtime_error("Complete all EOS configuration fields");
    auto key=line.substr(0,equal), value=line.substr(equal+1);
    if (value.size()>256 || !std::all_of(value.begin(),value.end(),
        [](unsigned char c) { return c>=33 && c<=126; }) ||
        !values.emplace(std::move(key),std::move(value)).second)
      throw std::runtime_error("Invalid EOS configuration field");
  }
  for (const char* name : {"product_id","sandbox_id","deployment_id","client_id","client_secret"})
    if (!values.contains(name)) throw std::runtime_error("Complete all EOS configuration fields");
  if (values.size()!=5) throw std::runtime_error("Unexpected EOS configuration field");
  return values;
}

// This executable is a separate diagnostic, not the game or online provider.
// SDK initialization itself requires neither a product credential nor login.
int main(int argc, char** argv) {
  const bool sdk_check=argc==2 && std::string_view(argv[1])=="--sdk-check";
  const bool platform_check=argc==3 && std::string_view(argv[1])=="--platform-check";
  const bool device_login=argc==3 && std::string_view(argv[1])=="--device-login";
  const bool lobby_check=argc==3 && std::string_view(argv[1])=="--lobby-check";
  const bool search_check=argc==3 && std::string_view(argv[1])=="--search-check";
  const bool wire_check=argc==3 && std::string_view(argv[1])=="--wire-check";
  if (!sdk_check && !platform_check && !device_login && !lobby_check && !search_check && !wire_check) {
    std::cerr << "Usage: redbaron_eos_probe --sdk-check | --platform-check <config> | --device-login <config> | --lobby-check <config> | --search-check <config> | --wire-check <config>\n";
    return 2;
  }
  std::map<std::string,std::string> config;
  if (platform_check || device_login || lobby_check || search_check || wire_check) {
    try { config=ReadConfig(argv[2]); }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
  }
  EOS_InitializeOptions options{};
  options.ApiVersion = EOS_INITIALIZE_API_LATEST;
  options.ProductName = "RedBaron_Recomp";
  options.ProductVersion = "0.10-dev";
  const EOS_EResult initialized = EOS_Initialize(&options);
  if (initialized != EOS_EResult::EOS_Success) {
    std::cerr << "EOS SDK initialization failed: " << int(initialized) << '\n';
    return 1;
  }
  const char* version = EOS_GetVersion();
  std::cout << "EOS SDK initialized: " << (version ? version : "unknown version") << '\n';
  int status=0;
  if (platform_check || device_login || lobby_check || search_check || wire_check) {
    EOS_Platform_Options platform{};
    platform.ApiVersion=EOS_PLATFORM_OPTIONS_API_LATEST;
    platform.ProductId=config.at("product_id").c_str();
    platform.SandboxId=config.at("sandbox_id").c_str();
    platform.DeploymentId=config.at("deployment_id").c_str();
    platform.ClientCredentials.ClientId=config.at("client_id").c_str();
    platform.ClientCredentials.ClientSecret=config.at("client_secret").c_str();
    platform.bIsServer=EOS_FALSE;
    platform.Flags=EOS_PF_DISABLE_OVERLAY;
    const auto handle=EOS_Platform_Create(&platform);
    if (!handle) {std::cerr << "EOS platform creation failed; check the local configuration\n"; status=1;}
    else {
      DeviceFlow flow;
      LobbyFlow lobby_flow;
      if (device_login || lobby_check || search_check || wire_check) {
        flow.connect=EOS_Platform_GetConnectInterface(handle);
        if (!flow.connect) {std::cerr << "EOS Connect interface unavailable\n";status=1;}
        else {
          EOS_Connect_CreateDeviceIdOptions create{};
          create.ApiVersion=EOS_CONNECT_CREATEDEVICEID_API_LATEST;
          create.DeviceModel="Windows PC";
          EOS_Connect_CreateDeviceId(flow.connect,&create,&flow,DeviceFlow::DeviceCreated);
          auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(45);
          while (!flow.done && std::chrono::steady_clock::now()<deadline) {
            EOS_Platform_Tick(handle);std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
          if (!flow.done || flow.result!=EOS_EResult::EOS_Success || !flow.user) {
            std::cerr << "EOS device login failed: " << (flow.done ? int(flow.result) : -1) << '\n';status=1;
          } else {
            std::cout << "EOS device login succeeded\n";
            if (wire_check) {
              auto p2p=EOS_Platform_GetP2PInterface(handle);
              if (!p2p) {std::cerr << "EOS P2P interface unavailable\n";status=1;}
              else {
                redbaron::eos::EosWire wire(p2p,flow.user,0,"RedBaronDiagnostic");
                if (wire.Ready(1) || !wire.Poll()) {std::cerr << "EOS P2P empty-membership check failed\n";status=1;}
                else std::cout << "EOS P2P adapter initialized; no peers connected\n";
              }
            }
            if (lobby_check || search_check) {
              lobby_flow.lobby=EOS_Platform_GetLobbyInterface(handle);
              lobby_flow.user=flow.user;
              lobby_flow.search_requested=search_check;
              std::random_device random;
              lobby_flow.bucket="redbaron-test-"+std::to_string(random())+"-"+std::to_string(random());
              if (!lobby_flow.lobby) {std::cerr << "EOS Lobby interface unavailable\n";status=1;}
              else {
                EOS_Lobby_CreateLobbyOptions create_lobby{};
                create_lobby.ApiVersion=EOS_LOBBY_CREATELOBBY_API_LATEST;
                create_lobby.LocalUserId=flow.user;
                create_lobby.MaxLobbyMembers=2;
                create_lobby.PermissionLevel=search_check ? EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED : EOS_ELobbyPermissionLevel::EOS_LPL_INVITEONLY;
                create_lobby.BucketId=lobby_flow.bucket.c_str();
                create_lobby.bDisableHostMigration=EOS_TRUE;
                EOS_Lobby_CreateLobby(lobby_flow.lobby,&create_lobby,&lobby_flow,LobbyFlow::Created);
                const auto lobby_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
                while (!lobby_flow.done && std::chrono::steady_clock::now()<lobby_deadline) {
                  EOS_Platform_Tick(handle);std::this_thread::sleep_for(std::chrono::milliseconds(10));
                  lobby_flow.PollSource();
                  lobby_flow.TickPublisher();
                  if (lobby_flow.retry_search && std::chrono::steady_clock::now()>=lobby_flow.retry_at) lobby_flow.Find();
                }
                if (!lobby_flow.done || lobby_flow.result!=EOS_EResult::EOS_Success) {
                  std::cerr << "EOS lobby test failed: " << (lobby_flow.done ? int(lobby_flow.result) : -1) << '\n';status=1;
                } else {
                  std::cout << "EOS test lobby destroyed successfully\n";
                  if (search_check) {
                    std::cout << "EOS search result: " << int(lobby_flow.search_result) << ", matches: " << lobby_flow.search_count << '\n';
                    std::cout << "EOS public test attribute verified: " << (lobby_flow.attribute_verified ? "yes" : "no") << '\n';
                    if (lobby_flow.search_result!=EOS_EResult::EOS_Success || lobby_flow.search_count!=1 || !lobby_flow.attribute_verified) status=1;
                  }
                }
              }
            }
          }
        }
      } else for (int i=0;i<10;++i) {EOS_Platform_Tick(handle);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
      EOS_Platform_Release(handle);
      if (!device_login && !lobby_check && !search_check && !wire_check) std::cout << "EOS platform created and released (no login was attempted)\n";
    }
  }
  const EOS_EResult shutdown = EOS_Shutdown();
  if (shutdown != EOS_EResult::EOS_Success) {
    std::cerr << "EOS SDK shutdown failed: " << int(shutdown) << '\n';
    return 1;
  }
  std::cout << "EOS SDK shutdown complete\n";
  return status;
}
