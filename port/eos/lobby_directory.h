#pragma once
#include <eos_lobby.h>
#include <string>
#include <vector>

namespace redbaron::eos {
struct LobbyEntry {
  std::string id,record;
  EOS_ProductUserId owner{};
  EOS_HLobbyDetails details{};
};
// Owns copied search results. Handles are valid until Refresh or destruction;
// callers must finish initiating any joins before replacing this snapshot.
class LobbyDirectory {
 public:
  ~LobbyDirectory();
  LobbyDirectory()=default;
  LobbyDirectory(const LobbyDirectory&)=delete;
  LobbyDirectory& operator=(const LobbyDirectory&)=delete;
  bool Refresh(EOS_HLobbySearch search);
  const std::vector<LobbyEntry>& entries() const {return entries_;}
 private:
  std::vector<LobbyEntry> entries_;
};
}
