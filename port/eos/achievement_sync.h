#pragma once
#include "platform.h"
#include <eos_achievements.h>
#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace redbaron::eos {
std::string AchievementId(uint32_t native_id);
// Native progress is durable. Failed uploads remain eligible for a later retry.
class AchievementSync {
 public:
  AchievementSync(Platform& platform,std::function<void(std::string)> log,
                  std::function<std::map<uint32_t,bool>()> local,
                  std::function<void(uint32_t)> restore);
  void Poll();
 private:
  static void EOS_CALL Definitions(const EOS_Achievements_OnQueryDefinitionsCompleteCallbackInfo*);
  static void EOS_CALL Player(const EOS_Achievements_OnQueryPlayerAchievementsCompleteCallbackInfo*);
  static void EOS_CALL Unlocked(const EOS_Achievements_OnUnlockAchievementsCompleteCallbackInfo*);
  Platform& platform_;EOS_HAchievements achievements_{};
  std::function<void(std::string)> log_;
  std::function<std::map<uint32_t,bool>()> local_;
  std::function<void(uint32_t)> restore_;
  std::set<std::string> definitions_,remote_;
  std::vector<std::string> submitted_;
  std::vector<const char*> pointers_;
  bool definitions_busy_=false,player_busy_=false,unlock_busy_=false,player_ready_=false;
  unsigned failures_=0;
  std::chrono::steady_clock::time_point next_definitions_{},next_unlock_{},next_poll_{};
};
}
