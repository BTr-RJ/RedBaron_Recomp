#pragma once
#include <functional>
#include <memory>
#include <string>

namespace redbaron::net {
class Signaling {
 public:
  Signaling(std::string url,std::function<void()> opened,
            std::function<void(std::string)> message,std::function<void(std::string)> closed);
  ~Signaling();
  void Send(std::string text);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
