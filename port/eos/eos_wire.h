#pragma once
#include <eos_p2p.h>
#include "../network/wire.h"
#include "fragmentation.h"
#include <functional>
#include <map>
#include <string_view>

namespace redbaron::eos {
struct P2PApi {
  decltype(&EOS_P2P_AcceptConnection) accept=&EOS_P2P_AcceptConnection;
  decltype(&EOS_P2P_CloseConnection) close=&EOS_P2P_CloseConnection;
  decltype(&EOS_P2P_GetPacketQueueInfo) queue=&EOS_P2P_GetPacketQueueInfo;
  decltype(&EOS_P2P_SendPacket) send=&EOS_P2P_SendPacket;
  decltype(&EOS_P2P_GetNextReceivedPacketSize) next=&EOS_P2P_GetNextReceivedPacketSize;
  decltype(&EOS_P2P_ReceivePacket) receive=&EOS_P2P_ReceivePacket;
};
// Experimental adapter. Membership must come from an authenticated EOS lobby;
// it is not safe to populate it from untrusted packet contents.
class EosWire final : public redbaron::net::Wire {
 public:
  EosWire(EOS_HP2P p2p, EOS_ProductUserId local, uint16_t slot, std::string_view socket_name, P2PApi api={});
  ~EosWire() override;
  EosWire(const EosWire&) = delete;
  EosWire& operator=(const EosWire&) = delete;
  bool SetMember(uint16_t slot, EOS_ProductUserId user);
  void RemoveMember(uint16_t slot);
  bool Send(uint16_t to, const redbaron::net::Bytes& bytes, bool reliable) override;
  bool Ready(uint16_t to) const override;
  size_t Buffered(uint16_t to) const override;
  uint16_t id() const override { return local_slot_; }
  // The owner must call EOS_Platform_Tick separately on this same thread.
  // A false result signals a failed reliable relay and requires disconnection.
  bool Poll();
  std::function<void(uint16_t, const redbaron::net::Bytes&, bool)> packet;
 private:
  EOS_HP2P p2p_;
  P2PApi api_;
  EOS_ProductUserId local_;
  uint16_t local_slot_;
  EOS_P2P_SocketId socket_{};
  std::map<uint16_t, EOS_ProductUserId> members_;
  Reassembler reliable_, unreliable_;
  uint32_t next_message_=1;
};
}
