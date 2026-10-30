#include "platform.h"
#include <eos_init.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <stdexcept>

namespace redbaron::eos {
namespace {
std::map<std::string,std::string> ReadConfig(const std::filesystem::path& path) {
  std::ifstream file(path,std::ios::binary);
  if(!file)throw std::runtime_error("Cannot read the local EOS configuration");
  std::map<std::string,std::string> values;std::string line;size_t total=0;
  while(std::getline(file,line)) {
    total+=line.size();if(total>4096)throw std::runtime_error("EOS configuration is too large");
    if(line.starts_with("\xEF\xBB\xBF"))line.erase(0,3);
    if(!line.empty() && line.back()=='\r')line.pop_back();
    if(line.empty() || line[0]=='#')continue;
    const auto equal=line.find('=');
    if(equal==std::string::npos || !equal || equal+1==line.size())throw std::runtime_error("Complete all EOS configuration fields");
    auto key=line.substr(0,equal),value=line.substr(equal+1);
    if(value.size()>256 || !std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=33 && c<=126;}) ||
       !values.emplace(key,value).second)throw std::runtime_error("Invalid EOS configuration field");
  }
  for(auto* key:{"product_id","sandbox_id","deployment_id","client_id","client_secret"})
    if(!values.contains(key))throw std::runtime_error("Complete all EOS configuration fields");
  if(values.size()!=5)throw std::runtime_error("Unexpected EOS configuration field");
  return values;
}
}
std::string ProductUserString(EOS_ProductUserId user) {
  std::array<char,EOS_PRODUCTUSERID_MAX_LENGTH+1> text{};int32_t size=int32_t(text.size());
  if(!EOS_ProductUserId_IsValid(user) || EOS_ProductUserId_ToString(user,text.data(),&size)!=EOS_EResult::EOS_Success)return {};
  return text.data();
}
Platform::Platform(const std::filesystem::path& path,std::string name):name_(std::move(name)) {
  const auto config=ReadConfig(path);
  if(name_.empty() || name_.size()>32)name_="RedBaron Pilot";
  EOS_InitializeOptions init{};init.ApiVersion=EOS_INITIALIZE_API_LATEST;
  init.ProductName="RedBaron_Recomp";init.ProductVersion="0.10-dev";
  if(EOS_Initialize(&init)!=EOS_EResult::EOS_Success)throw std::runtime_error("EOS initialization failed");
  EOS_Platform_Options options{};options.ApiVersion=EOS_PLATFORM_OPTIONS_API_LATEST;
  options.ProductId=config.at("product_id").c_str();options.SandboxId=config.at("sandbox_id").c_str();
  options.DeploymentId=config.at("deployment_id").c_str();
  options.ClientCredentials.ClientId=config.at("client_id").c_str();options.ClientCredentials.ClientSecret=config.at("client_secret").c_str();
  options.Flags=EOS_PF_DISABLE_OVERLAY;
  handle_=EOS_Platform_Create(&options);
  if(!handle_){EOS_Shutdown();throw std::runtime_error("EOS platform creation failed");}
  connect_=EOS_Platform_GetConnectInterface(handle_);
  EOS_Connect_AddNotifyAuthExpirationOptions notify{};notify.ApiVersion=EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST;
  expiration_=EOS_Connect_AddNotifyAuthExpiration(connect_,&notify,this,Expiring);
  EOS_Connect_CreateDeviceIdOptions device{};device.ApiVersion=EOS_CONNECT_CREATEDEVICEID_API_LATEST;device.DeviceModel="Windows PC";
  EOS_Connect_CreateDeviceId(connect_,&device,this,DeviceCreated);
}
Platform::~Platform() {
  Release();
  EOS_Shutdown();
}
void Platform::Release() {
  if(!handle_)return;
  if(expiration_!=EOS_INVALID_NOTIFICATIONID)EOS_Connect_RemoveNotifyAuthExpiration(connect_,expiration_);
  EOS_Platform_Release(handle_);handle_=nullptr;
}
void Platform::Tick(){EOS_Platform_Tick(handle_);}
void Platform::Login() {
  EOS_Connect_Credentials credentials{};credentials.ApiVersion=EOS_CONNECT_CREDENTIALS_API_LATEST;
  credentials.Type=EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
  EOS_Connect_UserLoginInfo info{};info.ApiVersion=EOS_CONNECT_USERLOGININFO_API_LATEST;info.DisplayName=name_.c_str();
  EOS_Connect_LoginOptions options{};options.ApiVersion=EOS_CONNECT_LOGIN_API_LATEST;options.Credentials=&credentials;options.UserLoginInfo=&info;
  EOS_Connect_Login(connect_,&options,this,LoggedInCallback);
}
void EOS_CALL Platform::DeviceCreated(const EOS_Connect_CreateDeviceIdCallbackInfo* data) {
  auto& s=*static_cast<Platform*>(data->ClientData);
  if(data->ResultCode==EOS_EResult::EOS_Success || data->ResultCode==EOS_EResult::EOS_DuplicateNotAllowed)s.Login();
  else{s.result_=data->ResultCode;s.login_done_=true;}
}
void EOS_CALL Platform::LoggedInCallback(const EOS_Connect_LoginCallbackInfo* data) {
  auto& s=*static_cast<Platform*>(data->ClientData);
  if(data->ResultCode==EOS_EResult::EOS_InvalidUser && data->ContinuanceToken) {
    EOS_Connect_CreateUserOptions options{};options.ApiVersion=EOS_CONNECT_CREATEUSER_API_LATEST;options.ContinuanceToken=data->ContinuanceToken;
    EOS_Connect_CreateUser(s.connect_,&options,&s,UserCreated);
  } else {s.result_=data->ResultCode;s.user_=data->LocalUserId;s.login_done_=true;}
}
void EOS_CALL Platform::UserCreated(const EOS_Connect_CreateUserCallbackInfo* data) {
  auto& s=*static_cast<Platform*>(data->ClientData);s.result_=data->ResultCode;s.user_=data->LocalUserId;s.login_done_=true;
}
void EOS_CALL Platform::Expiring(const EOS_Connect_AuthExpirationCallbackInfo* data) {static_cast<Platform*>(data->ClientData)->Login();}
}
