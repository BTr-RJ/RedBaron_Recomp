#pragma once
#include <cstdint>
#include <memory>
#include <string_view>

namespace redbaron::net {
// Local discovery endpoint for a directory of remote sessions. IDs are stable
// while the caller retains them and must be associated with the same room by
// the connection manager; directory results alone do not join a room.
class DirectoryResponder {
 public:
  explicit DirectoryResponder(uint8_t name_space);
  ~DirectoryResponder();
  DirectoryResponder(const DirectoryResponder&)=delete;
  DirectoryResponder& operator=(const DirectoryResponder&)=delete;
  bool Set(uint16_t id,std::string_view record);
  void Remove(uint16_t id);
  void Poll();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
