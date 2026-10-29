#pragma once
#include "lobby_directory.h"
#include "../network/directory_responder.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace redbaron::eos {
// Keeps virtual room addresses stable across directory refreshes. A selected
// address must be resolved before starting EOS_Lobby_JoinLobby.
class DirectoryMapping {
 public:
  explicit DirectoryMapping(redbaron::net::DirectoryResponder& responder):responder_(responder){}
  bool Update(const std::vector<LobbyEntry>& entries);
  const std::string* LobbyAt(uint16_t address) const;
 private:
  struct Room {uint16_t address;std::string record;};
  redbaron::net::DirectoryResponder& responder_;
  std::map<std::string,Room> rooms_;
};
}
