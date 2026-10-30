#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace redbaron::eos {
struct LeaderboardColumn {
  uint16_t id = 0;
  uint32_t property = 0;
  std::string aggregation;
};
struct LeaderboardView {
  uint32_t id = 0;
  std::string name, reset;
  std::vector<LeaderboardColumn> columns;
};
std::vector<LeaderboardView>
ReadLeaderboardSchema(std::span<const uint8_t> compressed);
std::string LeaderboardId(uint32_t view);
std::string LeaderboardStat(uint32_t view, uint32_t property);
} // namespace redbaron::eos
