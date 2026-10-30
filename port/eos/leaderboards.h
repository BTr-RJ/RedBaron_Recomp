#pragma once
#include "leaderboard_schema.h"
#include "platform.h"
#include <condition_variable>
#include <deque>
#include <eos_leaderboards.h>
#include <eos_stats.h>
#include <map>
#include <memory>
#include <mutex>
#include <rex/system/xam/stats_provider.h>
#include <set>
namespace redbaron::eos {
class Leaderboards final : public rex::system::xam::StatsProvider {
public:
  Leaderboards(Platform &, std::vector<LeaderboardView>, uint64_t local_xuid,
               std::function<void(std::string)> log);
  ~Leaderboards();
  void Poll();
  void Stop();
  bool Write(uint32_t, uint64_t,
             const std::vector<rex::system::xam::StatWrite> &) override;
  rex::system::xam::StatResult
  Read(const rex::system::xam::StatQuery &) override;
  const std::vector<LeaderboardView> &schema() const { return schema_; }

private:
  struct Request {
    rex::system::xam::StatQuery query;
    rex::system::xam::StatResult result;
    std::vector<rex::system::xam::StatWrite> write;
    std::vector<std::string> names;
    std::vector<EOS_Stats_IngestData> ingests;
    std::vector<EOS_ProductUserId> users;
    std::vector<EOS_Leaderboards_UserScoresQueryStatInfo> columns;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    Leaderboards *owner{};
    int64_t start = -1, end = -1;
    void Finish(bool ok) {
      std::lock_guard lock(mutex);
      result.ok = ok;
      done = true;
      cv.notify_all();
    }
  };
  static void EOS_CALL Definitions(
      const EOS_Leaderboards_OnQueryLeaderboardDefinitionsCompleteCallbackInfo
          *);
  static void EOS_CALL
  Ranks(const EOS_Leaderboards_OnQueryLeaderboardRanksCompleteCallbackInfo *);
  static void EOS_CALL Columns(
      const EOS_Leaderboards_OnQueryLeaderboardUserScoresCompleteCallbackInfo
          *);
  static void EOS_CALL
  Ingested(const EOS_Stats_IngestStatCompleteCallbackInfo *);
  const LeaderboardView *View(uint32_t) const;
  uint64_t NativeId(EOS_ProductUserId);
  Platform &platform_;
  EOS_HLeaderboards boards_{};
  EOS_HStats stats_{};
  std::vector<LeaderboardView> schema_;
  uint64_t local_xuid_;
  std::function<void(std::string)> log_;
  std::mutex mutex_;
  std::deque<std::shared_ptr<Request>> pending_;
  std::shared_ptr<Request> active_;
  struct Definition {
    std::string id;
    int64_t start = -1, end = -1;
  };
  std::map<std::string, uint64_t> identities_;
  std::map<uint32_t, Definition> definitions_;
  bool stopped_ = false, definitions_busy_ = false;
  std::chrono::steady_clock::time_point next_definitions_{};
};
} // namespace redbaron::eos
