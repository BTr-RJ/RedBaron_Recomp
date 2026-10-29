#pragma once
#include <memory>

namespace redbaron::net {
class Transport;
// Advertises only an actual local game session, never the room alone.
class SessionPublisher {
 public:
  SessionPublisher(Transport& transport, unsigned char name_space);
  ~SessionPublisher();
  void Poll();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
