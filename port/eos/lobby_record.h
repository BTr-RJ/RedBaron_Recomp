#pragma once
#include <eos_lobby.h>
#include <optional>
#include <string>
#include <string_view>

namespace redbaron::eos {
// The caller owns the lobby, callback data, and platform until completion.
// old_count is the number of chunks from the last successful update.
EOS_EResult PublishLobbyRecord(EOS_HLobby lobby, EOS_ProductUserId owner, const char* lobby_id,
                              std::string_view record, size_t old_count, void* callback_data,
                              EOS_Lobby_OnUpdateLobbyCallback callback);
EOS_EResult ClearLobbyRecord(EOS_HLobby lobby, EOS_ProductUserId owner, const char* lobby_id,
                            size_t old_count, void* callback_data, EOS_Lobby_OnUpdateLobbyCallback callback);
// The caller must validate the decoded native session before using it.
std::optional<std::string> ReadLobbyRecord(EOS_HLobbyDetails details);
}
