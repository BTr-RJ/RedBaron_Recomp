#pragma once
#include <eos_sdk.h>
#include <eos_connect.h>
#include <eos_auth.h>
#include <eos_logging.h>
#include <filesystem>
#include <string>
#include <functional>

namespace redbaron::eos {
// All methods, callbacks and destruction belong to the service worker thread.
class Platform {
 public:
  Platform(const std::filesystem::path& config,std::string name,bool epic_account=false,std::function<void(std::string)> log={});
  ~Platform();
  Platform(const Platform&)=delete;
  Platform& operator=(const Platform&)=delete;
  void Tick();
  void Release();
  bool LoginDone() const {return login_done_;}
  bool LoggedIn() const {return user_ && result_==EOS_EResult::EOS_Success;}
  EOS_HPlatform handle() const {return handle_;}
  EOS_ProductUserId user() const {return user_;}
  EOS_EResult result() const {return result_;}
  EOS_EpicAccountId epic_user() const {return epic_user_;}
 private:
  void Login();
  static void EOS_CALL DeviceCreated(const EOS_Connect_CreateDeviceIdCallbackInfo*);
  static void EOS_CALL LoggedInCallback(const EOS_Connect_LoginCallbackInfo*);
  static void EOS_CALL UserCreated(const EOS_Connect_CreateUserCallbackInfo*);
  static void EOS_CALL Expiring(const EOS_Connect_AuthExpirationCallbackInfo*);
  void EpicLogin(bool persistent);
  void EpicConnect();
  static void EOS_CALL EpicLoggedIn(const EOS_Auth_LoginCallbackInfo*);
  static void EOS_CALL EpicConnected(const EOS_Connect_LoginCallbackInfo*);
  static void EOS_CALL EpicLinked(const EOS_Connect_LinkAccountCallbackInfo*);
  static void EOS_CALL SdkLog(const EOS_LogMessage*);
  static Platform* log_owner_;
  EOS_HPlatform handle_{};
  EOS_HConnect connect_{};
  EOS_HAuth auth_{};
  EOS_EpicAccountId epic_user_{};
  EOS_ProductUserId user_{};
  EOS_NotificationId expiration_=EOS_INVALID_NOTIFICATIONID;
  EOS_EResult result_=EOS_EResult::EOS_UnexpectedError;
  std::string name_;
  bool login_done_=false;
  bool epic_account_=false,portal_attempted_=false;
  std::function<void(std::string)> log_;
};
std::string ProductUserString(EOS_ProductUserId user);
}
