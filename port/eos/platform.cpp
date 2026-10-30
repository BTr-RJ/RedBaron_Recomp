#include "platform.h"
#include <eos_init.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <stdexcept>
#include <regex>

namespace redbaron::eos {
Platform* Platform::log_owner_=nullptr;
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
Platform::Platform(const std::filesystem::path& path,std::string name,bool epic_account,std::function<void(std::string)> log):name_(std::move(name)),epic_account_(epic_account),log_(std::move(log)) {
  const auto config=ReadConfig(path);
  if(name_.empty() || name_.size()>32)name_="RedBaron Pilot";
  EOS_InitializeOptions init{};init.ApiVersion=EOS_INITIALIZE_API_LATEST;
  init.ProductName="RedBaron_Recomp";init.ProductVersion="0.10-dev";
  if(EOS_Initialize(&init)!=EOS_EResult::EOS_Success)throw std::runtime_error("EOS initialization failed");
  if(log_) {
    log_owner_=this;EOS_Logging_SetCallback(SdkLog);
    EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_Auth,EOS_ELogLevel::EOS_LOG_Warning);
  }
  EOS_Platform_Options options{};options.ApiVersion=EOS_PLATFORM_OPTIONS_API_LATEST;
  options.ProductId=config.at("product_id").c_str();options.SandboxId=config.at("sandbox_id").c_str();
  options.DeploymentId=config.at("deployment_id").c_str();
  options.ClientCredentials.ClientId=config.at("client_id").c_str();options.ClientCredentials.ClientSecret=config.at("client_secret").c_str();
  options.Flags=epic_account ? 0 : EOS_PF_DISABLE_OVERLAY;
  handle_=EOS_Platform_Create(&options);
  if(!handle_){EOS_Shutdown();log_owner_=nullptr;throw std::runtime_error("EOS platform creation failed");}
  if(log_)log_("EOS platform initialized");
  connect_=EOS_Platform_GetConnectInterface(handle_);
  auth_=EOS_Platform_GetAuthInterface(handle_);
#ifdef _WIN32
  if(epic_account_) {
    EOS_Platform_GetDesktopCrossplayStatusOptions status{};status.ApiVersion=EOS_PLATFORM_GETDESKTOPCROSSPLAYSTATUS_API_LATEST;
    EOS_Platform_DesktopCrossplayStatusInfo info{};
    const auto code=EOS_Platform_GetDesktopCrossplayStatus(handle_,&status,&info);
    if(log_)log_("Epic desktop crossplay status: "+std::to_string(int(info.Status)));
    if(code!=EOS_EResult::EOS_Success || info.Status!=EOS_EDesktopCrossplayStatus::EOS_DCS_OK) {
      EOS_Platform_Release(handle_);handle_=nullptr;EOS_Shutdown();log_owner_=nullptr;
      throw std::runtime_error("Epic desktop components are not ready (status "+std::to_string(int(info.Status))+"); launch through RedBaron_Online.exe or repair the Epic runtime");
    }
  }
#endif
  EOS_Platform_SetApplicationStatus(handle_,EOS_EApplicationStatus::EOS_AS_Foreground);
  EOS_Platform_SetNetworkStatus(handle_,EOS_ENetworkStatus::EOS_NS_Online);
  EOS_Connect_AddNotifyAuthExpirationOptions notify{};notify.ApiVersion=EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST;
  expiration_=EOS_Connect_AddNotifyAuthExpiration(connect_,&notify,this,Expiring);
  if(epic_account_){EpicLogin(true);return;}
  EOS_Connect_CreateDeviceIdOptions device{};device.ApiVersion=EOS_CONNECT_CREATEDEVICEID_API_LATEST;device.DeviceModel="Windows PC";
  EOS_Connect_CreateDeviceId(connect_,&device,this,DeviceCreated);
}
Platform::~Platform() {
  Release();
  EOS_Shutdown();
  if(log_owner_==this)log_owner_=nullptr;
}
void EOS_CALL Platform::SdkLog(const EOS_LogMessage* message) {
  // SDK messages can contain identifiers. Keep only an allowlisted backend
  // error code; never forward request URLs, tokens, credentials or full text.
  try {
    if(!log_owner_ || !log_owner_->log_ || !message || !message->Message)return;
    static const std::regex code("errors\\.com\\.epicgames\\.[A-Za-z0-9_.-]+");
    std::cmatch found;if(std::regex_search(message->Message,found,code))log_owner_->log_("EOS backend error code: "+found.str());
  }catch(...){}
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
  if(s.log_)s.log_("EOS device identity: "+std::string(EOS_EResult_ToString(data->ResultCode)));
  if(data->ResultCode==EOS_EResult::EOS_Success || data->ResultCode==EOS_EResult::EOS_DuplicateNotAllowed)s.Login();
  else{s.result_=data->ResultCode;s.login_done_=true;}
}
void EOS_CALL Platform::LoggedInCallback(const EOS_Connect_LoginCallbackInfo* data) {
  auto& s=*static_cast<Platform*>(data->ClientData);
  if(data->ResultCode==EOS_EResult::EOS_InvalidUser && data->ContinuanceToken) {
    EOS_Connect_CreateUserOptions options{};options.ApiVersion=EOS_CONNECT_CREATEUSER_API_LATEST;options.ContinuanceToken=data->ContinuanceToken;
    EOS_Connect_CreateUser(s.connect_,&options,&s,UserCreated);
  } else {
    s.result_=data->ResultCode;s.user_=data->LocalUserId;
    if(s.epic_account_ && data->ResultCode==EOS_EResult::EOS_Success && !s.epic_user_)s.EpicLogin(true);
    else s.login_done_=true;
  }
}
void EOS_CALL Platform::UserCreated(const EOS_Connect_CreateUserCallbackInfo* data) {
  auto& s=*static_cast<Platform*>(data->ClientData);s.result_=data->ResultCode;s.user_=data->LocalUserId;
  if(s.epic_account_ && !s.epic_user_ && data->ResultCode==EOS_EResult::EOS_Success)s.EpicLogin(true);else s.login_done_=true;
}
void Platform::EpicLogin(bool persistent) {
  if(!persistent)portal_attempted_=true;
  EOS_Auth_Credentials credentials{};credentials.ApiVersion=EOS_AUTH_CREDENTIALS_API_LATEST;
  credentials.Type=persistent ? EOS_ELoginCredentialType::EOS_LCT_PersistentAuth : EOS_ELoginCredentialType::EOS_LCT_AccountPortal;
  EOS_Auth_LoginOptions options{};options.ApiVersion=EOS_AUTH_LOGIN_API_LATEST;options.Credentials=&credentials;
  options.ScopeFlags=EOS_EAuthScopeFlags::EOS_AS_BasicProfile|EOS_EAuthScopeFlags::EOS_AS_FriendsList|EOS_EAuthScopeFlags::EOS_AS_Presence;
  if(persistent)options.LoginFlags=EOS_LF_NO_USER_INTERFACE;
  EOS_Auth_Login(auth_,&options,this,EpicLoggedIn);
}
void EOS_CALL Platform::EpicLoggedIn(const EOS_Auth_LoginCallbackInfo* data) {
  auto& s=*static_cast<Platform*>(data->ClientData);
  if(s.log_)s.log_("Epic account authentication: "+std::string(EOS_EResult_ToString(data->ResultCode)));
  if(data->ResultCode!=EOS_EResult::EOS_Success) {
    // Never turn a cancelled/denied Epic login into a second account silently.
    if(!s.portal_attempted_ && data->ResultCode!=EOS_EResult::EOS_NoConnection &&
       data->ResultCode!=EOS_EResult::EOS_TimedOut){s.EpicLogin(false);return;}
    s.result_=data->ResultCode;s.login_done_=true;return;
  }
  s.epic_user_=data->LocalUserId;s.EpicConnect();
}
void Platform::EpicConnect() {
  EOS_Auth_CopyIdTokenOptions copy{};copy.ApiVersion=EOS_AUTH_COPYIDTOKEN_API_LATEST;copy.AccountId=epic_user_;
  EOS_Auth_IdToken* token=nullptr;
  const auto result=EOS_Auth_CopyIdToken(auth_,&copy,&token);
  if(result!=EOS_EResult::EOS_Success || !token){result_=result;login_done_=true;return;}
  EOS_Connect_Credentials credentials{};credentials.ApiVersion=EOS_CONNECT_CREDENTIALS_API_LATEST;
  credentials.Type=EOS_EExternalCredentialType::EOS_ECT_EPIC_ID_TOKEN;credentials.Token=token->JsonWebToken;
  EOS_Connect_LoginOptions login{};login.ApiVersion=EOS_CONNECT_LOGIN_API_LATEST;login.Credentials=&credentials;
  EOS_Connect_Login(connect_,&login,this,EpicConnected);EOS_Auth_IdToken_Release(token);
}
void EOS_CALL Platform::EpicConnected(const EOS_Connect_LoginCallbackInfo* data) {
  auto& s=*static_cast<Platform*>(data->ClientData);
  if(data->ResultCode==EOS_EResult::EOS_InvalidUser && data->ContinuanceToken) {
    if(s.user_) {
      EOS_Connect_LinkAccountOptions link{};link.ApiVersion=EOS_CONNECT_LINKACCOUNT_API_LATEST;
      link.LocalUserId=s.user_;link.ContinuanceToken=data->ContinuanceToken;
      EOS_Connect_LinkAccount(s.connect_,&link,&s,EpicLinked);
    } else {
      // First Epic login has no product user yet. Native saves remain in their
      // existing local directory and unlocked achievements are synchronized.
      EOS_Connect_CreateUserOptions create{};create.ApiVersion=EOS_CONNECT_CREATEUSER_API_LATEST;create.ContinuanceToken=data->ContinuanceToken;
      EOS_Connect_CreateUser(s.connect_,&create,&s,UserCreated);
    }
    return;
  }
  s.result_=data->ResultCode;
  if(data->ResultCode==EOS_EResult::EOS_Success)s.user_=data->LocalUserId;
  s.login_done_=true;
}
void EOS_CALL Platform::EpicLinked(const EOS_Connect_LinkAccountCallbackInfo* data) {
  auto& s=*static_cast<Platform*>(data->ClientData);s.result_=data->ResultCode;
  if(data->ResultCode==EOS_EResult::EOS_Success)s.user_=data->LocalUserId;
  s.login_done_=true;
}
void EOS_CALL Platform::Expiring(const EOS_Connect_AuthExpirationCallbackInfo* data) {
  auto& s=*static_cast<Platform*>(data->ClientData);if(s.epic_user_)s.EpicConnect();else s.Login();
}
}
