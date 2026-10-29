#pragma once
#include "transport.h"
#include <memory>

namespace redbaron::net {
// Loopback-only TCP/UDP adapter. The game sees canonical peer addresses while
// separate local namespaces allow multiple diagnostic instances on one PC.
class SocketBridge {
 public:
  SocketBridge(Transport& transport, uint8_t name_space, bool diagnostics=false);
  ~SocketBridge();
  void Member(uint16_t id, bool present);
  void Packet(uint16_t peer, const Bytes& packet, bool reliable);
  void Poll();
  std::string Address(uint16_t peer) const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
