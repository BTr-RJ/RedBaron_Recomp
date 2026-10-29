#include <WinSock2.h>
#include <WS2tcpip.h>
#include "session_publisher.h"
#include "transport.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <random>
#include <stdexcept>

namespace redbaron::net {
using Clock = std::chrono::steady_clock;
struct SessionPublisher::Impl {
  Transport& transport;
  SOCKET socket = INVALID_SOCKET;
  sockaddr_in game{};
  std::array<unsigned char, 20> query{};
  Clock::time_point next{}, last_response{};
  std::string published;
  std::random_device random;
  Impl(Transport& value, unsigned char ns) : transport(value) {
    socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET) throw std::runtime_error("Could not create session discovery socket");
    game.sin_family = AF_INET;
    game.sin_addr.s_addr = htonl(0x7F000000u | (uint32_t(ns) << 16));
    game.sin_port = htons(43821);
    auto local = game; local.sin_port = 0;
    u_long nonblocking = 1;
    if (bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) || ioctlsocket(socket, FIONBIO, &nonblocking)) {
      closesocket(socket); socket = INVALID_SOCKET;
      throw std::runtime_error("Could not initialize session discovery socket");
    }
    const unsigned char prefix[] = {'R','B','L','A','N','0','1',1};
    std::copy(std::begin(prefix), std::end(prefix), query.begin());
    query[16]=0x58; query[17]=0x41; query[18]=0x09; query[19]=0xB5;
  }
  ~Impl() { if (socket != INVALID_SOCKET) closesocket(socket); }
  void Poll() {
    if (!transport.admitted() || transport.id() != 0) return;
    auto now = Clock::now();
    if (now >= next) {
      for (size_t i=8; i<16; ++i) query[i] = static_cast<unsigned char>(random());
      sendto(socket, reinterpret_cast<const char*>(query.data()), int(query.size()), 0,
             reinterpret_cast<const sockaddr*>(&game), sizeof(game));
      next = now + std::chrono::seconds(1);
    }
    for (unsigned n=0; n<16; ++n) {
      std::array<unsigned char,8192> data{}; sockaddr_in from{}; int from_size=sizeof(from);
      const int length=recvfrom(socket, reinterpret_cast<char*>(data.data()), int(data.size()), 0,
                                reinterpret_cast<sockaddr*>(&from), &from_size);
      if (length < 0) break;
      if (length < 104 || from.sin_addr.s_addr != game.sin_addr.s_addr || from.sin_port != game.sin_port ||
          !std::equal(data.begin(), data.begin()+7, query.begin()) || data[7] != 2 ||
          !std::equal(data.begin()+8, data.begin()+16, query.begin()+8) ||
          !std::equal(data.begin()+16, data.begin()+20, query.begin()+16)) continue;
      last_response = now;
      // Do not advertise a private or full native game session.
      if (!(data[80] | data[81] | data[82] | data[83])) {
        if (!published.empty()) { transport.PublishSession(""); published.clear(); }
        continue;
      }
      // A new query nonce must not make an unchanged session look different.
      std::fill(data.begin()+8, data.begin()+16, 0);
      static constexpr char hex[] = "0123456789abcdef";
      std::string record; record.reserve(size_t(length)*2);
      for (int i=0; i<length; ++i) { record.push_back(hex[data[i]>>4]); record.push_back(hex[data[i]&15]); }
      if (record != published) { transport.PublishSession(record); published=std::move(record); }
    }
    if (!published.empty() && now-last_response > std::chrono::seconds(3)) {
      transport.PublishSession(""); published.clear();
    }
  }
};
SessionPublisher::SessionPublisher(Transport& t, unsigned char ns) : impl_(std::make_unique<Impl>(t,ns)) {}
SessionPublisher::~SessionPublisher() = default;
void SessionPublisher::Poll() { impl_->Poll(); }
}
