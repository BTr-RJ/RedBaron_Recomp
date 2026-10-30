#pragma once
#include <rex/system/xam/lan_lobby.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace redbaron::eos {
struct OnlineOptions {
  std::filesystem::path config;
  std::string player_name="RedBaron Pilot";
  std::string bucket="redbaron-584109b5-v1";
  uint8_t name_space=120;
  bool force_relay=false; // Opt-in network diagnostics; normal play allows direct ICE.
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
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
