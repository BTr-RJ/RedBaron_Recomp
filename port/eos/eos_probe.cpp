#include <eos_init.h>
#include <eos_version.h>
#include <eos_sdk.h>
#include <eos_connect.h>
#include <eos_lobby.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
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

// The diagnostic owns and destroys only the lobby it creates.
struct LobbyFlow {
  EOS_HLobby lobby{};
  EOS_ProductUserId user{};
  std::string id;
  EOS_EResult result=EOS_EResult::EOS_UnexpectedError;
  bool done=false;
  static void EOS_CALL Destroyed(const EOS_Lobby_DestroyLobbyCallbackInfo* data) {
    auto& self=*static_cast<LobbyFlow*>(data->ClientData);
    self.result=data->ResultCode;self.done=true;
  }
  static void EOS_CALL Created(const EOS_Lobby_CreateLobbyCallbackInfo* data) {
    auto& self=*static_cast<LobbyFlow*>(data->ClientData);
    if (data->ResultCode!=EOS_EResult::EOS_Success || !data->LobbyId) {
      self.result=data->ResultCode;self.done=true;return;
    }
    self.id=data->LobbyId;
    std::cout << "EOS test lobby created; destroying it\n";
    EOS_Lobby_DestroyLobbyOptions options{};
    options.ApiVersion=EOS_LOBBY_DESTROYLOBBY_API_LATEST;
    options.LocalUserId=self.user;options.LobbyId=self.id.c_str();
    EOS_Lobby_DestroyLobby(self.lobby,&options,&self,Destroyed);
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
  if (!sdk_check && !platform_check && !device_login && !lobby_check) {
    std::cerr << "Usage: redbaron_eos_probe --sdk-check | --platform-check <config> | --device-login <config> | --lobby-check <config>\n";
    return 2;
  }
  std::map<std::string,std::string> config;
  if (platform_check || device_login || lobby_check) {
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
  if (platform_check || device_login || lobby_check) {
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
      if (device_login || lobby_check) {
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
            if (lobby_check) {
              lobby_flow.lobby=EOS_Platform_GetLobbyInterface(handle);
              lobby_flow.user=flow.user;
              if (!lobby_flow.lobby) {std::cerr << "EOS Lobby interface unavailable\n";status=1;}
              else {
                EOS_Lobby_CreateLobbyOptions create_lobby{};
                create_lobby.ApiVersion=EOS_LOBBY_CREATELOBBY_API_LATEST;
                create_lobby.LocalUserId=flow.user;
                create_lobby.MaxLobbyMembers=2;
                create_lobby.PermissionLevel=EOS_ELobbyPermissionLevel::EOS_LPL_INVITEONLY;
                create_lobby.BucketId="redbaron-diagnostic-v1";
                create_lobby.bDisableHostMigration=EOS_TRUE;
                EOS_Lobby_CreateLobby(lobby_flow.lobby,&create_lobby,&lobby_flow,LobbyFlow::Created);
                const auto lobby_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
                while (!lobby_flow.done && std::chrono::steady_clock::now()<lobby_deadline) {
                  EOS_Platform_Tick(handle);std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                if (!lobby_flow.done || lobby_flow.result!=EOS_EResult::EOS_Success) {
                  std::cerr << "EOS lobby test failed: " << (lobby_flow.done ? int(lobby_flow.result) : -1) << '\n';status=1;
                } else std::cout << "EOS test lobby destroyed successfully\n";
              }
            }
          }
        }
      } else for (int i=0;i<10;++i) {EOS_Platform_Tick(handle);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
      EOS_Platform_Release(handle);
      if (!device_login && !lobby_check) std::cout << "EOS platform created and released (no login was attempted)\n";
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
