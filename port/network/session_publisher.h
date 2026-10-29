#pragma once
#include <memory>
#include <functional>
#include <string>

namespace redbaron::net {
// Advertises only an actual local game session, never the room alone.
class SessionPublisher {
 public:
  using Publish = std::function<void(const std::string&)>;
  SessionPublisher(Publish publish, unsigned char name_space);
  ~SessionPublisher();
  void Poll();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
