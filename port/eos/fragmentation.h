#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace redbaron::eos {
using Packet = std::vector<uint8_t>;
// EOS_P2P_MAX_PACKET_SIZE is 1170 in the official 1.19.2.1 SDK.
inline constexpr size_t kPacketLimit = 1170;
inline constexpr size_t kHeader = 12;
inline constexpr size_t kMessageLimit = 32768;
std::vector<Packet> Fragment(uint32_t id, const Packet& message);

class Reassembler {
 public:
  using Clock = std::chrono::steady_clock;
  std::optional<Packet> Accept(uint16_t peer, const Packet& fragment, Clock::time_point now);
  void RemovePeer(uint16_t peer);
 private:
  struct Pending {
    uint16_t total;
    uint8_t count;
    Clock::time_point created;
    std::vector<Packet> parts;
    size_t received = 0;
  };
  std::map<std::pair<uint16_t,uint32_t>, Pending> pending_;
};
}
