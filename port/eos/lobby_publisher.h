#pragma once
#include <eos_lobby.h>
#include <string>
#include <string_view>

namespace redbaron::eos {
// Single-threaded owner. Keep alive through callbacks and EOS_Platform_Release.
// Empty records unpublish the game; updates are coalesced while EOS is busy.
class LobbyPublisher {
 public:
  LobbyPublisher(EOS_HLobby lobby,EOS_ProductUserId owner,std::string lobby_id);
  bool Offer(std::string_view record);
  void Poll();
  bool Synced() const {return !in_flight_ && error_==EOS_EResult::EOS_Success && desired_==published_;}
  EOS_EResult error() const {return error_;}
 private:
  static void EOS_CALL Updated(const EOS_Lobby_UpdateLobbyCallbackInfo* data);
  EOS_HLobby lobby_;
  EOS_ProductUserId owner_;
  std::string lobby_id_,desired_,published_,submitted_;
  size_t published_count_=0,submitted_count_=0;
  bool in_flight_=false;
  EOS_EResult error_=EOS_EResult::EOS_Success;
};
}
