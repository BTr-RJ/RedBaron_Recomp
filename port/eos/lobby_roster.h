#pragma once
#include "roster.h"
#include <eos_lobby.h>

namespace redbaron::eos {
class EosWire;
// These functions accept only identities taken from the actual lobby snapshot.
std::optional<Roster> AssignLobbyRoster(EOS_HLobbyDetails details,const Roster& previous);
std::optional<Roster> ReadLobbyRoster(EOS_HLobbyDetails details);
// Applies the roster only when each ID matches an actual lobby member.
bool ApplyLobbyRoster(EOS_HLobbyDetails details,EosWire& wire);
EOS_EResult PublishLobbyRoster(EOS_HLobby lobby,EOS_ProductUserId owner,const char* lobby_id,
                              EOS_HLobbyDetails details,const Roster& roster,void* context,EOS_Lobby_OnUpdateLobbyCallback callback);
}
