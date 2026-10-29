#pragma once
#include "wire.h"
#include <memory>
#include <string>

namespace redbaron::net {
// Loopback-only TCP/UDP adapter. The game sees canonical peer addresses while
// separate local namespaces allow multiple diagnostic instances on one PC.
class SocketBridge {
 public:
  SocketBridge(Wire& transport, uint8_t name_space, bool diagnostics=false);
  ~SocketBridge();
  void Member(uint16_t id, bool present);
  // Consumes an accepted local TCP socket, including on failure. Used after
  // asynchronous room admission; the socket must already be loopback-verified.
  bool Adopt(uint16_t peer, uint16_t port, uintptr_t accepted_socket);
  void Packet(uint16_t peer, const Bytes& packet, bool reliable);
  void Poll();
  std::string Address(uint16_t peer) const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
