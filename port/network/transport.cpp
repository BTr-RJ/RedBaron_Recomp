#include "transport.h"
#include <stdexcept>
#include <regex>

namespace redbaron::net {
namespace {
constexpr size_t kLimit = 16 * 1024 * 1024;
uint16_t Read16(const Bytes& bytes, size_t at) {
  return uint16_t((std::to_integer<unsigned>(bytes[at]) << 8) | std::to_integer<unsigned>(bytes[at+1]));
}
}
Transport::Transport(Json options) : options_(std::move(options)) {
  const auto url = options_.at("server").get<std::string>();
  if (!url.starts_with("wss://") &&
      !std::regex_match(url, std::regex(R"(ws://(127\.0\.0\.1|localhost):[0-9]{1,5}(/[^\s]*)?)")))
    throw std::runtime_error("Public room servers require wss:// with a valid TLS certificate");
  socket_ = std::make_unique<Signaling>(url, [this] { Queue([this] {
    Json hello{{"op", options_.value("room", "").empty() ? "create" : "join"},
      {"protocol", 1}, {"build", "redbaron-wire-1"}, {"name", options_.at("name")},
      {"players", options_.value("players", 1)}, {"mode", options_.value("mode", "multiplayer")}};
    if (options_.contains("room")) hello["room"] = options_["room"];
    socket_->Send(hello.dump());
  }); }, [this](std::string text) {
    auto size = text.size();
    Queue([this, text = std::move(text)] { Signal(Json::parse(text)); }, size);
  }, [this](std::string error) {
    Queue([this, error] { Emit({{"op", "closed"}, {"reason", error}}); Stop(); });
  });
}
Transport::~Transport() {
  stopped_ = true;
  socket_.reset();
  for (auto& [id, peer] : peers_) {
    peer.pc->resetCallbacks();
    if (peer.reliable) peer.reliable->resetCallbacks();
    if (peer.datagrams) peer.datagrams->resetCallbacks();
    peer.pc->close();
  }
  peers_.clear();
  { std::lock_guard lock(mutex_); pending_.clear(); }
  rtc::Cleanup().wait();
}
void Transport::Queue(std::function<void()> fn, size_t size) {
  std::lock_guard lock(mutex_);
  if (stopped_) return;
  if (pending_.size() >= 4096 || pending_bytes_ + size > kLimit) { stopped_ = true; return; }
  pending_bytes_ += size;
  pending_.emplace_back(std::move(fn), size);
}
void Transport::Poll() {
  std::deque<std::pair<std::function<void()>, size_t>> pending;
  { std::lock_guard lock(mutex_); pending.swap(pending_); pending_bytes_ = 0; }
  for (auto& [fn, size] : pending) {
    if (stopped_) break;
    try { fn(); } catch (const std::exception& error) {
      Emit({{"op", "error"}, {"message", error.what()}}); Stop();
    }
  }
  if (id_ == 0) {
    // A peer that never completes ICE must not reserve a player slot forever.
    // Collect IDs first: RejectPeer removes entries from peers_.
    std::vector<uint16_t> expired;
    const auto now = std::chrono::steady_clock::now();
    for (const auto& [id, peer] : peers_) {
      if (!peer.reported_ready && now - peer.started > std::chrono::seconds(30))
        expired.push_back(id);
    }
    for (auto id : expired) RejectPeer(id, "Could not establish a connection to this player");
  }
  if(id_ != 0 && peers_.contains(0) && !peers_.at(0).reported_ready &&
      std::chrono::steady_clock::now()-peers_.at(0).started > std::chrono::seconds(30)) {
    Emit({{"op","error"},{"message","Could not reach the host. Check the relay server and try joining again."}});Stop();
  }
}
void Transport::Emit(const Json& message) { if (status) status(message); }
void Transport::Stop() { stopped_ = true; }
void Transport::Signal(const Json& data) {
  const auto op = data.at("op").get<std::string>();
  if (op == "welcome") {
    if (admitted_ || data.at("build") != "redbaron-wire-1" || data.at("protocol") != 1)
      throw std::runtime_error("Incompatible room server");
    id_ = data.at("id").get<uint16_t>();
    config_.disableAutoNegotiation = true;
    config_.maxMessageSize = 65536;
    config_.mtu = 1200;
    if (options_.value("force_relay", false)) config_.iceTransportPolicy = rtc::TransportPolicy::Relay;
    for (const auto& item : data.at("ice")) {
      rtc::IceServer server(item.at("urls").get<std::string>());
      server.username = item.value("username", ""); server.password = item.value("credential", "");
      config_.iceServers.push_back(std::move(server));
    }
    admitted_ = true;
    for (const auto& member : data.at("members")) {
      auto id = member.at("id").get<uint16_t>();
      members_.insert(id);
      if (id != id_ && membership) membership(id, true);
    }
    Json public_message = data;
    public_message.erase("ice"); // Do not write relay credentials to logs or status files.
    Emit(public_message);
    if (id_ == 0) for (auto id : members_) if (id) AddPeer(id, true);
  } else if (op == "joined") {
    auto id = data.at("member").at("id").get<uint16_t>();
    if (members_.size() >= 8 || members_.contains(id)) throw std::runtime_error("Invalid room membership");
    members_.insert(id);
    if (membership) membership(id, true);
    Emit(data);
    if (id_ == 0) AddPeer(id, true);
  } else if (op == "left") {
    auto id = data.at("id").get<uint16_t>();
    members_.erase(id);
    if (auto it = peers_.find(id); it != peers_.end()) {
      it->second.pc->close(); peers_.erase(it);
    }
    if (membership) membership(id, false);
    Emit(data);
  } else if (op == "signal") {
    auto id = data.at("from").get<uint16_t>();
    if (!members_.contains(id)) return; // An in-flight signal may precede a departure.
    if (id_ != 0 && id != 0) throw std::runtime_error("Invalid signaling peer");
    auto kind = data.at("kind").get<std::string>(), body = data.at("data").get<std::string>();
    if (!peers_.contains(id)) AddPeer(id, false);
    auto& pc = peers_.at(id).pc;
    try {
      if (kind == "offer" || kind == "answer") {
        pc->setRemoteDescription(rtc::Description(body, kind));
        if (kind == "offer") pc->setLocalDescription(rtc::Description::Type::Answer);
      } else if (kind == "candidate") pc->addRemoteCandidate(rtc::Candidate(body, "0"));
    } catch (const std::exception&) {
      if (id_ != 0) throw;
      RejectPeer(id, "Invalid connection response");
    }
  } else if (op == "closed" || op == "error") { Emit(data); Stop(); }
}
void Transport::RejectPeer(uint16_t id, const std::string& reason) {
  if (id_ != 0 || id == 0 || !members_.contains(id)) return;
  if (auto it=peers_.find(id);it!=peers_.end()) {
    it->second.pc->resetCallbacks();
    if(it->second.reliable)it->second.reliable->resetCallbacks();
    if(it->second.datagrams)it->second.datagrams->resetCallbacks();
    it->second.pc->close();peers_.erase(it);
  }
  if(membership)membership(id,false);
  members_.erase(id);
  socket_->Send(Json{{"op","kick"},{"id",id}}.dump());
  Emit({{"op","peer_error"},{"id",id},{"message",reason}});
}
void Transport::AddPeer(uint16_t id, bool offer) {
  if (peers_.contains(id)) return;
  auto pc = std::make_shared<rtc::PeerConnection>(config_);
  peers_[id].pc = pc;
  pc->onLocalDescription([this, id](rtc::Description description) {
    Queue([this, id, type = description.typeString(), text = std::string(description)] {
      if (members_.contains(id)) socket_->Send(Json{{"op", "signal"}, {"to", id}, {"kind", type}, {"data", text}}.dump());
    });
  });
  pc->onLocalCandidate([this, id](rtc::Candidate candidate) {
    Queue([this, id, text = std::string(candidate)] {
      if (members_.contains(id)) socket_->Send(Json{{"op", "signal"}, {"to", id}, {"kind", "candidate"}, {"data", text}}.dump());
    });
  });
  pc->onStateChange([this, id](rtc::PeerConnection::State state) { Queue([this, id, state] {
    if(!peers_.contains(id))return;
    Emit({{"op", "peer_state"}, {"id", id}, {"state", int(state)}});
    if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Disconnected ||
        state == rtc::PeerConnection::State::Closed) {
      if (id_ == 0 && state != rtc::PeerConnection::State::Disconnected) {
        RejectPeer(id, "The connection to this player was lost");
        return;
      }
      if (membership) membership(id, false);
      if(id_ != 0 && id == 0) {Emit({{"op","error"},{"message","The connection to the host was lost. Join the room again."}});Stop();}
    }
  }); });
  pc->onDataChannel([this, id](std::shared_ptr<rtc::DataChannel> channel) {
    Queue([this, id, channel] { AddChannel(id, channel); });
  });
  if (offer) {
    AddChannel(id, pc->createDataChannel("reliable"));
    rtc::DataChannelInit init; init.reliability.unordered = true; init.reliability.maxRetransmits = 0;
    AddChannel(id, pc->createDataChannel("datagrams", init));
    pc->setLocalDescription(rtc::Description::Type::Offer);
  }
}
void Transport::AddChannel(uint16_t peer, std::shared_ptr<rtc::DataChannel> channel) {
  if (!peers_.contains(peer)) { channel->close(); return; }
  const bool reliable = channel->label() == "reliable";
  if (!reliable && channel->label() != "datagrams") { channel->close(); return; }
  auto& destination = reliable ? peers_.at(peer).reliable : peers_.at(peer).datagrams;
  if (destination) { channel->close(); return; }
  destination = channel;
  channel->onOpen([this, peer] { Queue([this, peer] {
    if (Ready(peer) && !peers_.at(peer).reported_ready) {
      peers_.at(peer).reported_ready=true;Emit({{"op", "peer_ready"}, {"id", peer}});
    }
  }); });
  channel->onMessage([this, peer, reliable](rtc::message_variant message) {
    if (auto* bytes = std::get_if<rtc::binary>(&message)) {
      const auto size = bytes->size();
      Queue([this, peer, reliable, bytes = std::move(*bytes)] { Incoming(peer, bytes, reliable); }, size);
    }
  });
  if (Ready(peer) && !peers_.at(peer).reported_ready) {
    peers_.at(peer).reported_ready=true;Emit({{"op", "peer_ready"}, {"id", peer}});
  }
}
bool Transport::Ready(uint16_t to) const {
  const auto it = peers_.find(id_ == 0 ? to : 0);
  return members_.contains(to) && it != peers_.end() && it->second.reliable && it->second.datagrams &&
      it->second.reliable->isOpen() && it->second.datagrams->isOpen();
}
size_t Transport::Buffered(uint16_t to) const {
  auto it = peers_.find(id_ == 0 ? to : 0);
  return it == peers_.end() || !it->second.reliable ? kLimit : it->second.reliable->bufferedAmount();
}
bool Transport::Send(uint16_t to, const Bytes& bytes, bool reliable) {
  if (!Ready(to) || bytes.size() < 8 || bytes.size() > 32768) return false;
  auto& peer = peers_.at(id_ == 0 ? to : 0);
  auto& channel = reliable ? peer.reliable : peer.datagrams;
  if (channel->bufferedAmount() > 1024*1024) return false;
  channel->send(bytes); // false means queued, not rejected, in libdatachannel.
  return true;
}
void Transport::Incoming(uint16_t peer, const Bytes& bytes, bool reliable) {
  if (bytes.size() < 8 || bytes.size() > 32768 || bytes[0] != std::byte{'R'} || bytes[1] != std::byte{'B'} ||
      bytes[2] != std::byte{'N'} || bytes[3] != std::byte{1}) return;
  const auto from = Read16(bytes, 4), to = Read16(bytes, 6);
  if (!members_.contains(from) || !members_.contains(to) || from == to ||
      (id_ == 0 && from != peer) || (id_ != 0 && peer != 0)) return;
  if (to != id_) {
    if (id_ == 0 && Ready(to) && !Send(to, bytes, reliable) && reliable)
      RejectPeer(to, "Peer relay queue exceeded");
  } else if (packet) packet(from, bytes, reliable);
}
}
