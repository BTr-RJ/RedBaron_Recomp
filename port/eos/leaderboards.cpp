#include "leaderboards.h"
#include "leaderboard_policy.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
namespace redbaron::eos {
using namespace rex::system::xam;
using Clock = std::chrono::steady_clock;
namespace {
EOS_ELeaderboardAggregation Aggregation(const std::string &a) {
  return a == "SUM"   ? EOS_ELeaderboardAggregation::EOS_LA_Sum
         : a == "MAX" ? EOS_ELeaderboardAggregation::EOS_LA_Max
         : a == "MIN" ? EOS_ELeaderboardAggregation::EOS_LA_Min
                      : EOS_ELeaderboardAggregation::EOS_LA_Latest;
}
} // namespace
Leaderboards::Leaderboards(Platform &p, std::vector<LeaderboardView> schema,
                           uint64_t xuid, std::function<void(std::string)> log)
    : platform_(p), schema_(std::move(schema)), local_xuid_(xuid),
      log_(std::move(log)) {
  boards_ = EOS_Platform_GetLeaderboardsInterface(p.handle());
  stats_ = EOS_Platform_GetStatsInterface(p.handle());
}
Leaderboards::~Leaderboards() { Stop(); }
void Leaderboards::Stop() {
  std::lock_guard lock(mutex_);
  stopped_ = true;
  for (auto &r : pending_)
    r->Finish(false);
  pending_.clear();
  if (active_)
    active_->Finish(false);
}
const LeaderboardView *Leaderboards::View(uint32_t id) const {
  auto i = std::find_if(schema_.begin(), schema_.end(),
                        [&](const auto &v) { return v.id == id; });
  return i == schema_.end() ? nullptr : &*i;
}
uint64_t Leaderboards::NativeId(EOS_ProductUserId user) {
  if (user == platform_.user())
    return local_xuid_;
  const auto id = ProductUserString(user);
  auto i = identities_.find(id);
  if (i != identities_.end())
    return i->second;
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char c : id) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  const uint64_t value = 0x0009000000000000ull | (hash & 0x0000FFFFFFFFFF00ull);
  identities_[id] = value;
  return value;
}
bool Leaderboards::Write(uint32_t title, uint64_t user,
                         const std::vector<StatWrite> &views) {
  if (title != 0x584109B5)
    return false;
  // A player client may ingest only its authenticated PUID. Never impersonate
  // a remote native XUID supplied by a host.
  if (user != local_xuid_)
    return true;
  auto r = std::make_shared<Request>();
  r->write = views;
  r->owner = this;
  {
    std::lock_guard lock(mutex_);
    if (stopped_ || pending_.size() >= 32)
      return false;
    pending_.push_back(r);
  }
  std::unique_lock lock(r->mutex);
  return r->cv.wait_for(lock, std::chrono::seconds(20), [&] {
    return r->done;
  }) && r->result.ok;
}
StatResult Leaderboards::Read(const StatQuery &q) {
  if (q.title != 0x584109B5 || !View(q.view) || !q.count || q.count > 100)
    return {};
  auto r = std::make_shared<Request>();
  r->query = q;
  r->owner = this;
  {
    std::lock_guard lock(mutex_);
    if (stopped_ || pending_.size() >= 32)
      return {};
    pending_.push_back(r);
  }
  std::unique_lock lock(r->mutex);
  if (!r->cv.wait_for(lock, std::chrono::seconds(20), [&] { return r->done; }))
    return {};
  return r->result;
}
void Leaderboards::Poll() {
  const auto now = Clock::now();
  if (!definitions_busy_ && now >= next_definitions_) {
    definitions_busy_ = true;
    next_definitions_ = now + std::chrono::minutes(1);
    EOS_Leaderboards_QueryLeaderboardDefinitionsOptions q{};
    q.ApiVersion = EOS_LEADERBOARDS_QUERYLEADERBOARDDEFINITIONS_API_LATEST;
    q.LocalUserId = platform_.user();
    q.StartTime = q.EndTime = EOS_LEADERBOARDS_TIME_UNDEFINED;
    EOS_Leaderboards_QueryLeaderboardDefinitions(boards_, &q, this,
                                                 Definitions);
  }
  {
    std::lock_guard lock(mutex_);
    if (stopped_)
      return;
    if (active_) {
      bool done;
      {
        std::lock_guard item(active_->mutex);
        done = active_->done;
      }
      if (!done)
        return;
      active_.reset();
    }
    if (pending_.empty() || definitions_busy_)
      return;
    active_ = pending_.front();
    pending_.pop_front();
  }
  auto &r = *active_;
  if (!r.write.empty()) {
    for (const auto &v : r.write) {
      const auto *schema = View(v.view);
      if (!schema || !definitions_.contains(v.view)) {
        log_("Leaderboard stats not sent: native view is not configured in "
             "this EOS deployment");
        r.Finish(false);
        return;
      }
      for (const auto &c : schema->columns) {
        auto f = v.properties.find(c.property);
        if (c.id >= 65533 || f == v.properties.end())
          continue;
        if (f->second < INT32_MIN || f->second > INT32_MAX) {
          log_("Leaderboard stat exceeds EOS int32 range; submission rejected");
          r.Finish(false);
          return;
        }
        r.names.push_back(LeaderboardStat(v.view, c.property));
      }
      // Rating is a numeric native property too, despite its reserved column
      // ID.
      auto rating = std::find_if(schema->columns.begin(), schema->columns.end(),
                                 [](const auto &c) { return c.id == 65534; });
      if (rating != schema->columns.end() &&
          v.properties.contains(rating->property)) {
        auto n = v.properties.at(rating->property);
        if (n < INT32_MIN || n > INT32_MAX) {
          r.Finish(false);
          return;
        }
        r.names.push_back(LeaderboardStat(v.view, rating->property));
      }
    }
    if (r.names.empty()) {
      r.Finish(true);
      return;
    }
    for (const auto &name : r.names) {
      int64_t value = 0;
      bool found = false;
      for (const auto &v : r.write)
        for (const auto &[id, n] : v.properties)
          if (LeaderboardStat(v.view, id) == name) {
            value = n;
            found = true;
          }
      if (found)
        r.ingests.push_back(
            {EOS_STATS_INGESTDATA_API_LATEST, name.c_str(), int32_t(value)});
    }
    EOS_Stats_IngestStatOptions q{};
    q.ApiVersion = EOS_STATS_INGESTSTAT_API_LATEST;
    q.LocalUserId = q.TargetUserId = platform_.user();
    q.Stats = r.ingests.data();
    q.StatsCount = uint32_t(r.ingests.size());
    EOS_Stats_IngestStat(stats_, &q, &r, Ingested);
    return;
  }
  if (!definitions_.contains(r.query.view)) {
    log_("Leaderboard unavailable: configure " + LeaderboardId(r.query.view) +
         " and its native stats in the Epic portal");
    r.Finish(false);
    return;
  }
  const auto &definition = definitions_.at(r.query.view);
  r.start = definition.start;
  r.end = definition.end;
  r.names.push_back(definition.id);
  EOS_Leaderboards_QueryLeaderboardRanksOptions q{};
  q.ApiVersion = EOS_LEADERBOARDS_QUERYLEADERBOARDRANKS_API_LATEST;
  q.LeaderboardId = r.names.front().c_str();
  q.LocalUserId = platform_.user();
  EOS_Leaderboards_QueryLeaderboardRanks(boards_, &q, &r, Ranks);
}
void EOS_CALL Leaderboards::Definitions(
    const EOS_Leaderboards_OnQueryLeaderboardDefinitionsCompleteCallbackInfo
        *d) {
  auto &s = *static_cast<Leaderboards *>(d->ClientData);
  s.definitions_busy_ = false;
  if (d->ResultCode != EOS_EResult::EOS_Success) {
    s.log_("EOS leaderboard definitions failed: " +
           std::string(EOS_EResult_ToString(d->ResultCode)));
    return;
  }
  s.definitions_.clear();
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  EOS_Leaderboards_GetLeaderboardDefinitionCountOptions count{};
  count.ApiVersion = EOS_LEADERBOARDS_GETLEADERBOARDDEFINITIONCOUNT_API_LATEST;
  const auto n =
      EOS_Leaderboards_GetLeaderboardDefinitionCount(s.boards_, &count);
  for (uint32_t i = 0; i < n; ++i) {
    EOS_Leaderboards_CopyLeaderboardDefinitionByIndexOptions q{};
    q.ApiVersion = EOS_LEADERBOARDS_COPYLEADERBOARDDEFINITIONBYINDEX_API_LATEST;
    q.LeaderboardIndex = i;
    EOS_Leaderboards_Definition *definition{};
    if (EOS_Leaderboards_CopyLeaderboardDefinitionByIndex(
            s.boards_, &q, &definition) != EOS_EResult::EOS_Success ||
        !definition)
      continue;
    if (definition->LeaderboardId && definition->StatName)
      for (const auto &v : s.schema_) {
        const auto name = LeaderboardId(v.id);
        const std::string id = definition->LeaderboardId;
        auto rating = std::find_if(v.columns.begin(), v.columns.end(),
                                   [](const auto &c) { return c.id == 65534; });
        const bool weekly = v.reset == "Weekly";
        // Weekly periods are configured administratively. Never silently turn a
        // missing weekly period into an all-time board or reset a user's stats.
        if (ActiveLeaderboardPeriod(weekly, definition->StartTime,
                                    definition->EndTime, now) &&
            (id == name || (weekly && id.starts_with(name + "_"))) &&
            rating != v.columns.end() &&
            definition->StatName == LeaderboardStat(v.id, rating->property) &&
            definition->Aggregation == Aggregation(rating->aggregation)) {
          auto old = s.definitions_.find(v.id);
          if (old == s.definitions_.end() ||
              definition->StartTime > old->second.start)
            s.definitions_[v.id] = {id, definition->StartTime,
                                    definition->EndTime};
        }
      }
    EOS_Leaderboards_Definition_Release(definition);
  }
  s.log_("EOS native leaderboard definitions: " +
         std::to_string(s.definitions_.size()) + "/" +
         std::to_string(s.schema_.size()) +
         " configured; missing views remain unavailable");
}
void EOS_CALL
Leaderboards::Ingested(const EOS_Stats_IngestStatCompleteCallbackInfo *d) {
  auto &r = *static_cast<Request *>(d->ClientData);
  r.owner->log_("EOS native stats submission: " +
                std::string(EOS_EResult_ToString(d->ResultCode)) +
                " (server acceptance is not score-readback confirmation)");
  r.Finish(d->ResultCode == EOS_EResult::EOS_Success);
}
void EOS_CALL Leaderboards::Ranks(
    const EOS_Leaderboards_OnQueryLeaderboardRanksCompleteCallbackInfo *d) {
  auto &r = *static_cast<Request *>(d->ClientData);
  auto &s = *r.owner;
  if (d->ResultCode != EOS_EResult::EOS_Success) {
    s.log_("EOS leaderboard query failed: " +
           std::string(EOS_EResult_ToString(d->ResultCode)));
    r.Finish(false);
    return;
  }
  EOS_Leaderboards_GetLeaderboardRecordCountOptions count{};
  count.ApiVersion = EOS_LEADERBOARDS_GETLEADERBOARDRECORDCOUNT_API_LATEST;
  const auto n = EOS_Leaderboards_GetLeaderboardRecordCount(s.boards_, &count);
  r.result.total = n;
  struct Candidate {
    StatRow row;
    EOS_ProductUserId user;
  };
  std::vector<Candidate> all;
  for (uint32_t i = 0; i < n; ++i) {
    EOS_Leaderboards_CopyLeaderboardRecordByIndexOptions q{};
    q.ApiVersion = EOS_LEADERBOARDS_COPYLEADERBOARDRECORDBYINDEX_API_LATEST;
    q.LeaderboardRecordIndex = i;
    EOS_Leaderboards_LeaderboardRecord *rec{};
    if (EOS_Leaderboards_CopyLeaderboardRecordByIndex(s.boards_, &q, &rec) !=
            EOS_EResult::EOS_Success ||
        !rec)
      continue;
    Candidate c;
    c.user = rec->UserId;
    c.row.xuid = s.NativeId(rec->UserId);
    c.row.rank = rec->Rank + 1;
    c.row.rating = rec->Score;
    c.row.name = rec->UserDisplayName ? rec->UserDisplayName : "Pilot";
    all.push_back(std::move(c));
    EOS_Leaderboards_LeaderboardRecord_Release(rec);
  }
  size_t start = r.query.start ? r.query.start - 1 : 0;
  if (r.query.pivot) {
    auto p = std::find_if(all.begin(), all.end(), [&](const auto &c) {
      return c.row.xuid == r.query.pivot;
    });
    if (p == all.end()) {
      r.Finish(true);
      return;
    }
    size_t index = size_t(p - all.begin());
    start = index > r.query.count / 2 ? index - r.query.count / 2 : 0;
  }
  for (size_t i = 0; i < all.size(); ++i) {
    auto &c = all[i];
    if (!r.query.users.empty()) {
      if (std::find(r.query.users.begin(), r.query.users.end(), c.row.xuid) ==
          r.query.users.end())
        continue;
    } else if (i < start || r.result.rows.size() >= r.query.count)
      continue;
    r.users.push_back(c.user);
    r.result.rows.push_back(std::move(c.row));
  }
  if (r.result.rows.empty()) {
    s.log_("EOS native leaderboard read: 0 row(s) for " +
           LeaderboardId(r.query.view));
    r.Finish(true);
    return;
  }
  const auto *schema = s.View(r.query.view);
  r.names.clear();
  for (auto col : r.query.columns) {
    auto c = std::find_if(schema->columns.begin(), schema->columns.end(),
                          [&](const auto &c) { return c.id == col; });
    if (c != schema->columns.end() && col < 65533)
      r.names.push_back(LeaderboardStat(schema->id, c->property));
  }
  for (const auto &name : r.names) {
    auto c = std::find_if(
        schema->columns.begin(), schema->columns.end(), [&](const auto &c) {
          return LeaderboardStat(schema->id, c.property) == name;
        });
    r.columns.push_back({EOS_LEADERBOARDS_USERSCORESQUERYSTATINFO_API_LATEST,
                         name.c_str(), Aggregation(c->aggregation)});
  }
  if (r.columns.empty()) {
    r.Finish(true);
    return;
  }
  EOS_Leaderboards_QueryLeaderboardUserScoresOptions q{};
  q.ApiVersion = EOS_LEADERBOARDS_QUERYLEADERBOARDUSERSCORES_API_LATEST;
  q.LocalUserId = s.platform_.user();
  q.UserIds = r.users.data();
  q.UserIdsCount = uint32_t(r.users.size());
  q.StatInfo = r.columns.data();
  q.StatInfoCount = uint32_t(r.columns.size());
  q.StartTime = r.start;
  q.EndTime = r.end;
  EOS_Leaderboards_QueryLeaderboardUserScores(s.boards_, &q, &r, Columns);
}
void EOS_CALL Leaderboards::Columns(
    const EOS_Leaderboards_OnQueryLeaderboardUserScoresCompleteCallbackInfo
        *d) {
  auto &r = *static_cast<Request *>(d->ClientData);
  auto &s = *r.owner;
  if (d->ResultCode != EOS_EResult::EOS_Success) {
    r.Finish(false);
    return;
  }
  const auto *schema = s.View(r.query.view);
  for (size_t i = 0; i < r.users.size(); ++i)
    for (const auto &c : schema->columns) {
      if (std::find(r.query.columns.begin(), r.query.columns.end(), c.id) ==
              r.query.columns.end() ||
          c.id >= 65533)
        continue;
      const auto name = LeaderboardStat(schema->id, c.property);
      EOS_Leaderboards_CopyLeaderboardUserScoreByUserIdOptions q{};
      q.ApiVersion =
          EOS_LEADERBOARDS_COPYLEADERBOARDUSERSCOREBYUSERID_API_LATEST;
      q.UserId = r.users[i];
      q.StatName = name.c_str();
      EOS_Leaderboards_LeaderboardUserScore *score{};
      if (EOS_Leaderboards_CopyLeaderboardUserScoreByUserId(
              s.boards_, &q, &score) == EOS_EResult::EOS_Success &&
          score) {
        r.result.rows[i].columns[c.id] = score->Score;
        EOS_Leaderboards_LeaderboardUserScore_Release(score);
      }
    }
  s.log_("EOS native leaderboard read: " +
         std::to_string(r.result.rows.size()) + " row(s)");
  r.Finish(true);
}
} // namespace redbaron::eos
