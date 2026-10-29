#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace redbaron::net {
struct PendingConnection {uint16_t room,port;uintptr_t socket;};
// Holds verified local game sockets while asynchronous lobby admission runs.
// Take transfers ownership; the caller must close or adopt the socket.
class SelectionGateway {
 public:
  SelectionGateway(uint8_t name_space,uint16_t game_id);
  ~SelectionGateway();
  SelectionGateway(const SelectionGateway&)=delete;
  SelectionGateway& operator=(const SelectionGateway&)=delete;
  bool Add(uint16_t room);
  void Remove(uint16_t room);
  void Poll();
  void Cancel();
  std::optional<PendingConnection> Take();
  std::function<void(uint16_t)> selected;
  std::function<void(uint16_t)> expired;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
