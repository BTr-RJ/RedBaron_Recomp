#pragma once
#include <cstdint>
namespace redbaron::eos {
inline bool ActiveLeaderboardPeriod(bool weekly, int64_t start, int64_t end,
                                    int64_t now) {
  if ((start >= 0 && start > now) || (end >= 0 && end <= now))
    return false;
  if (start >= 0 && end >= 0 && end <= start)
    return false;
  return !weekly || (start >= 0 && end > start && end - start <= 8 * 86400);
}
} // namespace redbaron::eos
