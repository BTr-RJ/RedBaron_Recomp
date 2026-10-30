#pragma once
#include "../network/wire.h"
#include "roster.h"
#include <array>
#include <functional>
#include <string_view>

namespace redbaron::eos {
// Stable game addresses are independent of the position allocated after joining.
// Collisions are rejected before admitting a roster; they are never silently aliased.
uint16_t NetworkId(std::string_view product_user_id);
class AddressWire final : public redbaron::net::Wire {
 public:
  AddressWire(redbaron::net::Wire& wire, const Roster& roster);
  bool Send(uint16_t peer,const redbaron::net::Bytes& data,bool reliable) override;
  bool Ready(uint16_t peer) const override;
  size_t Buffered(uint16_t peer) const override;
  uint16_t id() const override;
  void Receive(uint16_t peer,const redbaron::net::Bytes& data,bool reliable);
  bool Update(const Roster& roster);
  const std::array<uint16_t,8>& addresses() const {return addresses_;}
  std::function<void(uint16_t,const redbaron::net::Bytes&,bool)> packet;
 private:
  int Slot(uint16_t address) const;
  redbaron::net::Wire& wire_;
  std::array<uint16_t,8> addresses_{};
  std::array<uint64_t,8> identities_{};
};
}
