#pragma once
#include <rex/system/xam/lan_lobby.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <map>
#include <string>

namespace redbaron::eos {
struct OnlineOptions {
  std::filesystem::path config;
  std::string player_name="RedBaron Pilot";
  std::string bucket="redbaron-584109b5-v1";
  uint8_t name_space=120;
  bool force_relay=false; // Opt-in network diagnostics; normal play allows direct ICE.
  bool epic_account=false;
  std::function<void(const rex::system::xam::LanRecord&)> invite_accepted;
  std::function<std::map<uint32_t,bool>()> achievements;
  std::function<void(uint32_t)> restore_achievement;
  std::function<void()> show_achievements;
  std::function<void(const std::string&)> log;
};
// The same provider is used by the game and the native-runtime integration test.
// EOS API calls and P2P/socket dispatch are confined to one worker thread.
class OnlineService final : public rex::system::xam::LanOnlineProvider {
 public:
  explicit OnlineService(OnlineOptions options);
  ~OnlineService() override;
  void Stop();
  uint16_t network_id() const;
  std::vector<rex::system::xam::LanRecord> Search(uint32_t title) override;
  std::vector<rex::system::xam::LanQosResult> Probe(const std::vector<rex::system::xam::LanQosTarget>& targets,uint32_t probes) override;
  bool Connect(uint32_t guest_address) override;
  bool CanHost() const override;
  void Leave(const rex::system::xam::LanSessionId& session) override;
  bool ShowFriends(uint32_t user) override;
  bool ShowAchievements(uint32_t user) override;
  bool InputCaptured() const override;
  void RankMatches(std::vector<rex::system::xam::LanRecord>& records) override;
  void ConfigureLeaderboards(std::vector<uint8_t> schema,uint64_t local_xuid,const std::filesystem::path& private_output);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
