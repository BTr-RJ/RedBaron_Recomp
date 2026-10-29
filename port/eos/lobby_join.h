#pragma once
#include <eos_lobby.h>
#include <string>

namespace redbaron::eos {
class DirectoryMapping;
class LobbyDirectory;
struct LobbyJoinApi {
  decltype(&EOS_Lobby_JoinLobby) join=&EOS_Lobby_JoinLobby;
  decltype(&EOS_Lobby_LeaveLobby) leave=&EOS_Lobby_LeaveLobby;
  decltype(&EOS_LobbyDetails_Release) release=&EOS_LobbyDetails_Release;
};
// Single-threaded and must remain alive through pending callbacks and platform
// release. Start consumes the details handle on success only.
class LobbyJoin {
 public:
  enum class State {Idle,Joining,Joined,Leaving,Failed};
  LobbyJoin(EOS_HLobby lobby,EOS_ProductUserId user,LobbyJoinApi api={});
  ~LobbyJoin();
  LobbyJoin(const LobbyJoin&)=delete;
  LobbyJoin& operator=(const LobbyJoin&)=delete;
  bool Start(std::string lobby_id,EOS_HLobbyDetails details);
  bool Leave();
  bool Reset();
  State state() const {return state_;}
  EOS_EResult error() const {return error_;}
  const std::string& lobby_id() const {return id_;}
 private:
  static void EOS_CALL Joined(const EOS_Lobby_JoinLobbyCallbackInfo* data);
  static void EOS_CALL Left(const EOS_Lobby_LeaveLobbyCallbackInfo* data);
  EOS_HLobby lobby_;
  EOS_ProductUserId user_;
  LobbyJoinApi api_;
  EOS_HLobbyDetails details_{};
  std::string id_;
  State state_=State::Idle;
  EOS_EResult error_=EOS_EResult::EOS_Success;
};
// Resolves a directory address and transfers its search-result handle to join.
bool JoinSelected(uint16_t address,const DirectoryMapping& mapping,LobbyDirectory& directory,LobbyJoin& join);
}
