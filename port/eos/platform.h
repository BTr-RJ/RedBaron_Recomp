#pragma once
#include <eos_sdk.h>
#include <eos_connect.h>
#include <filesystem>
#include <string>

namespace redbaron::eos {
// All methods, callbacks and destruction belong to the service worker thread.
class Platform {
 public:
  Platform(const std::filesystem::path& config,std::string name);
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
 private:
  void Login();
  static void EOS_CALL DeviceCreated(const EOS_Connect_CreateDeviceIdCallbackInfo*);
  static void EOS_CALL LoggedInCallback(const EOS_Connect_LoginCallbackInfo*);
  static void EOS_CALL UserCreated(const EOS_Connect_CreateUserCallbackInfo*);
  static void EOS_CALL Expiring(const EOS_Connect_AuthExpirationCallbackInfo*);
  EOS_HPlatform handle_{};
  EOS_HConnect connect_{};
  EOS_ProductUserId user_{};
  EOS_NotificationId expiration_=EOS_INVALID_NOTIFICATIONID;
  EOS_EResult result_=EOS_EResult::EOS_UnexpectedError;
  std::string name_;
  bool login_done_=false;
};
std::string ProductUserString(EOS_ProductUserId user);
}
