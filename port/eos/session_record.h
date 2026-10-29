#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace redbaron::eos {
// Encode the LAN session record for EOS attributes without including game data.
// 8192 bytes become at most 53 chunks of 208 ASCII characters.
inline constexpr size_t kSessionMaxBytes=8192;
inline constexpr size_t kSessionChunkSize=208;
std::vector<std::string> EncodeSessionRecord(std::string_view hex);
std::optional<std::string> DecodeSessionRecord(const std::vector<std::string>& chunks);
}
