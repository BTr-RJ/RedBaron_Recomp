#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace redbaron::eos {
using Roster=std::array<std::string,8>;
// Host allocates slots; existing members retain theirs until they depart.
std::optional<Roster> AssignRoster(const Roster& previous,std::string_view owner,
                                   const std::vector<std::string>& members);
// Two bounded lobby attributes hold four entries each. The input identities
// must originate from authenticated EOS lobby membership, not packets.
std::array<std::string,2> EncodeRoster(const Roster& roster);
std::optional<Roster> DecodeRoster(const std::array<std::string,2>& encoded,
                                   std::string_view owner,const std::vector<std::string>& members);
}
