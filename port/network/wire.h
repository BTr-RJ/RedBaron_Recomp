#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace redbaron::net {
using Bytes = std::vector<std::byte>;

// The game socket adapter is independent of the peer transport provider.
class Wire {
 public:
  virtual ~Wire() = default;
  virtual bool Send(uint16_t to, const Bytes& bytes, bool reliable) = 0;
  virtual bool Ready(uint16_t to) const = 0;
  virtual size_t Buffered(uint16_t to) const = 0;
  virtual uint16_t id() const = 0;
};
}
