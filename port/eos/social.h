#pragma once
#include "platform.h"
#include <eos_friends.h>
#include <eos_lobby.h>
#include <eos_presence.h>
#include <eos_ui.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>

namespace redbaron::eos {
// Owned by the online worker. The receiver owns accepted lobby details handles.
class Social {
 public:
  Social(Platform& platform,std::function<void(std::string)> log,
         std::function<void(EOS_HLobbyDetails,EOS_UI_EventId)> accepted);
  ~Social();
  void Detach();
  bool ShowFriends();
  bool captured() const {return captured_.load();}
  void Presence(const std::string& text);
  void Acknowledge(EOS_UI_EventId id,EOS_EResult result);
 private:
  static void EOS_CALL FriendsQueried(const EOS_Friends_QueryFriendsCallbackInfo*);
  static void EOS_CALL FriendsChanged(const EOS_Friends_OnFriendsUpdateInfo*);
  static void EOS_CALL DisplayChanged(const EOS_UI_OnDisplaySettingsUpdatedCallbackInfo*);
  static void EOS_CALL FriendsShown(const EOS_UI_ShowFriendsCallbackInfo*);
  static void EOS_CALL InviteAccepted(const EOS_Lobby_LobbyInviteAcceptedCallbackInfo*);
  static void EOS_CALL JoinAccepted(const EOS_Lobby_JoinLobbyAcceptedCallbackInfo*);
  static void EOS_CALL PresenceSet(const EOS_Presence_SetPresenceCallbackInfo*);
  Platform& platform_;
  EOS_HUI ui_{};EOS_HFriends friends_{};EOS_HPresence presence_{};EOS_HLobby lobby_{};
  EOS_NotificationId friends_notify_=EOS_INVALID_NOTIFICATIONID,display_notify_=EOS_INVALID_NOTIFICATIONID;
  EOS_NotificationId invite_notify_=EOS_INVALID_NOTIFICATIONID,join_notify_=EOS_INVALID_NOTIFICATIONID;
  std::function<void(std::string)> log_;
  std::function<void(EOS_HLobbyDetails,EOS_UI_EventId)> accepted_;
  std::atomic<bool> captured_{false};
  std::string presence_text_;
  bool presence_busy_=false;
  std::chrono::steady_clock::time_point next_presence_{};
};
}
