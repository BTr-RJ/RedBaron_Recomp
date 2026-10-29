#pragma once
#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include "signaling.h"

namespace redbaron::net {
using Json = nlohmann::json;
using Bytes = std::vector<std::byte>;
// All callbacks are marshalled onto Poll's owner thread. Socket code never runs
// on libdatachannel threads, and neither library thread waits for the game.
class Transport {
 public:
  explicit Transport(Json options);
  ~Transport();
  void Poll();
  bool Send(uint16_t to, const Bytes& bytes, bool reliable);
  bool Ready(uint16_t to) const;
  size_t Buffered(uint16_t to) const;
  void PublishSession(const std::string& record);
  void Stop();
  uint16_t id() const { return id_; }
  bool admitted() const { return admitted_; }
  bool stopped() const { return stopped_; }
  std::function<void(const Json&)> status;
  std::function<void(uint16_t, const Bytes&, bool)> packet;
  std::function<void(uint16_t, bool)> membership;
 private:
  struct Peer {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> reliable, datagrams;
    bool reported_ready=false;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
  };
  void Queue(std::function<void()> fn, size_t size = 0);
  void Signal(const Json& data);
  void AddPeer(uint16_t id, bool offer);
  void RejectPeer(uint16_t id, const std::string& reason);
  void AddChannel(uint16_t peer, std::shared_ptr<rtc::DataChannel> channel);
  void Incoming(uint16_t peer, const Bytes& bytes, bool reliable);
  void Emit(const Json& message);
  Json options_;
  rtc::Configuration config_;
  std::unique_ptr<Signaling> socket_;
  std::map<uint16_t, Peer> peers_;
  std::set<uint16_t> members_;
  uint16_t id_ = 0;
  bool admitted_ = false;
  std::atomic<bool> stopped_{false};
  std::mutex mutex_;
  std::deque<std::pair<std::function<void()>, size_t>> pending_;
  size_t pending_bytes_ = 0;
};
}
