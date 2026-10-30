#include "achievement_sync.h"
#include <algorithm>
#include <cstdio>
namespace redbaron::eos {
std::string AchievementId(uint32_t id) {char text[32];std::snprintf(text,sizeof(text),"SFA_%08X",id);return text;}
AchievementSync::AchievementSync(Platform& platform,std::function<void(std::string)> log,
    std::function<std::map<uint32_t,bool>()> local,std::function<void(uint32_t)> restore)
    :platform_(platform),log_(std::move(log)),local_(std::move(local)),restore_(std::move(restore)) {
  achievements_=EOS_Platform_GetAchievementsInterface(platform.handle());
}
void AchievementSync::Poll() {
  using namespace std::chrono;const auto now=steady_clock::now();if(now<next_poll_ || !local_)return;
  next_poll_=now+seconds(1);
  // A deployment can gain definitions while the game is running. In particular,
  // sample definitions must not stop discovery of the native catalog later.
  if(!definitions_busy_ && now>=next_definitions_) {
      definitions_busy_=true;next_definitions_=now+minutes(1);
      EOS_Achievements_QueryDefinitionsOptions query{};query.ApiVersion=EOS_ACHIEVEMENTS_QUERYDEFINITIONS_API_LATEST;query.LocalUserId=platform_.user();
      EOS_Achievements_QueryDefinitions(achievements_,&query,this,Definitions);
  }
  if(definitions_.empty())return;
  if(!player_ready_) {
    if(!player_busy_ && now>=next_unlock_) {
      player_busy_=true;next_unlock_=now+seconds(30);
      EOS_Achievements_QueryPlayerAchievementsOptions query{};query.ApiVersion=EOS_ACHIEVEMENTS_QUERYPLAYERACHIEVEMENTS_API_LATEST;
      query.LocalUserId=query.TargetUserId=platform_.user();EOS_Achievements_QueryPlayerAchievements(achievements_,&query,this,Player);
    }
    return;
  }
  if(unlock_busy_ || now<next_unlock_)return;
  submitted_.clear();pointers_.clear();
  for(auto [id,unlocked]:local_()) {
    const auto key=AchievementId(id);if(!definitions_.contains(key))continue;
    if(remote_.contains(key)) {if(!unlocked && restore_)restore_(id);continue;}
    if(unlocked && submitted_.size()<16)submitted_.push_back(key);
  }
  if(submitted_.empty())return;
  for(const auto& id:submitted_)pointers_.push_back(id.c_str());
  EOS_Achievements_UnlockAchievementsOptions unlock{};unlock.ApiVersion=EOS_ACHIEVEMENTS_UNLOCKACHIEVEMENTS_API_LATEST;
  unlock.UserId=platform_.user();unlock.AchievementIds=pointers_.data();unlock.AchievementsCount=uint32_t(pointers_.size());
  unlock_busy_=true;EOS_Achievements_UnlockAchievements(achievements_,&unlock,this,Unlocked);
}
void EOS_CALL AchievementSync::Definitions(const EOS_Achievements_OnQueryDefinitionsCompleteCallbackInfo* data) {
  auto& s=*static_cast<AchievementSync*>(data->ClientData);s.definitions_busy_=false;
  if(data->ResultCode!=EOS_EResult::EOS_Success){s.log_("EOS achievement definitions unavailable: "+std::string(EOS_EResult_ToString(data->ResultCode)));return;}
  std::set<std::string> expected,available;
  for(const auto& [id,unlocked]:s.local_())expected.insert(AchievementId(id));
  uint32_t ignored=0;
  EOS_Achievements_GetAchievementDefinitionCountOptions count{};count.ApiVersion=EOS_ACHIEVEMENTS_GETACHIEVEMENTDEFINITIONCOUNT_API_LATEST;
  for(uint32_t i=0;i<EOS_Achievements_GetAchievementDefinitionCount(s.achievements_,&count);++i) {
    EOS_Achievements_CopyAchievementDefinitionV2ByIndexOptions copy{};copy.ApiVersion=EOS_ACHIEVEMENTS_COPYACHIEVEMENTDEFINITIONV2BYINDEX_API_LATEST;copy.AchievementIndex=i;
    EOS_Achievements_DefinitionV2* definition{};
    if(EOS_Achievements_CopyAchievementDefinitionV2ByIndex(s.achievements_,&copy,&definition)==EOS_EResult::EOS_Success && definition) {
      if(definition->AchievementId) {
        if(expected.contains(definition->AchievementId))available.insert(definition->AchievementId);
        else ++ignored;
      }
      EOS_Achievements_DefinitionV2_Release(definition);
    }
  }
  s.definitions_=std::move(available);
  if(!expected.empty() && s.definitions_.size()==expected.size())
    s.next_definitions_=std::chrono::steady_clock::now()+std::chrono::minutes(5);
  s.log_("EOS native achievement definitions loaded: "+std::to_string(s.definitions_.size())+
    "/"+std::to_string(expected.size())+"; unrelated definitions ignored: "+std::to_string(ignored));
}
void EOS_CALL AchievementSync::Player(const EOS_Achievements_OnQueryPlayerAchievementsCompleteCallbackInfo* data) {
  auto& s=*static_cast<AchievementSync*>(data->ClientData);s.player_busy_=false;
  if(data->ResultCode!=EOS_EResult::EOS_Success){s.log_("EOS player achievements query failed: "+std::string(EOS_EResult_ToString(data->ResultCode)));return;}
  EOS_Achievements_GetPlayerAchievementCountOptions count{};count.ApiVersion=EOS_ACHIEVEMENTS_GETPLAYERACHIEVEMENTCOUNT_API_LATEST;count.UserId=s.platform_.user();
  for(uint32_t i=0;i<EOS_Achievements_GetPlayerAchievementCount(s.achievements_,&count);++i) {
    EOS_Achievements_CopyPlayerAchievementByIndexOptions copy{};copy.ApiVersion=EOS_ACHIEVEMENTS_COPYPLAYERACHIEVEMENTBYINDEX_API_LATEST;
    copy.LocalUserId=copy.TargetUserId=s.platform_.user();copy.AchievementIndex=i;
    EOS_Achievements_PlayerAchievement* achievement{};
    if(EOS_Achievements_CopyPlayerAchievementByIndex(s.achievements_,&copy,&achievement)==EOS_EResult::EOS_Success && achievement) {
      if(achievement->AchievementId && achievement->UnlockTime!=EOS_ACHIEVEMENTS_ACHIEVEMENT_UNLOCKTIME_UNDEFINED)s.remote_.insert(achievement->AchievementId);
      EOS_Achievements_PlayerAchievement_Release(achievement);
    }
  }
  s.player_ready_=true;s.next_unlock_={};s.log_("EOS player achievements synchronized");
}
void EOS_CALL AchievementSync::Unlocked(const EOS_Achievements_OnUnlockAchievementsCompleteCallbackInfo* data) {
  auto& s=*static_cast<AchievementSync*>(data->ClientData);s.unlock_busy_=false;
  if(data->ResultCode==EOS_EResult::EOS_Success) {
    s.remote_.insert(s.submitted_.begin(),s.submitted_.end());s.failures_=0;s.log_("Native achievement unlocks uploaded to EOS");
  } else {
    s.next_unlock_=std::chrono::steady_clock::now()+std::chrono::seconds(5u<<std::min(s.failures_++,6u));
    s.log_("EOS achievement upload will retry; local progress retained: "+std::string(EOS_EResult_ToString(data->ResultCode)));
  }
}
}
