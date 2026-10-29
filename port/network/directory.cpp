#include "directory.h"
#include "signaling.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <regex>
#include <set>
#include <stdexcept>
#include <vector>

namespace redbaron::net {
namespace {
struct State {
  std::mutex mutex;
  std::condition_variable changed;
  bool opened = false, done = false;
  std::string response, error;
};
bool ValidSession(const nlohmann::json& value) {
  if (!value.is_string()) return false;
  const auto hex = value.get<std::string>();
  if (hex.size() < 208 || hex.size() > 16384 || hex.size() % 2 ||
      !std::regex_match(hex, std::regex("[0-9a-f]+"))) return false;
  std::vector<unsigned char> bytes; bytes.reserve(hex.size()/2);
  auto nibble = [](char c) { return unsigned(c <= '9' ? c-'0' : c-'a'+10); };
  for (size_t i=0; i<hex.size(); i+=2) bytes.push_back(static_cast<unsigned char>(nibble(hex[i])*16+nibble(hex[i+1])));
  const unsigned char prefix[] = {'R','B','L','A','N','0','1',2};
  if (!std::equal(std::begin(prefix), std::end(prefix), bytes.begin()) ||
      bytes[16]!=0x58 || bytes[17]!=0x41 || bytes[18]!=0x09 || bytes[19]!=0xB5) return false;
  auto u32 = [&](size_t i) { return (uint32_t(bytes[i])<<24) | (uint32_t(bytes[i+1])<<16) |
                                    (uint32_t(bytes[i+2])<<8) | uint32_t(bytes[i+3]); };
  uint64_t slots = uint64_t(u32(80))+u32(84)+u32(88)+u32(92);
  const auto contexts=u32(96), properties=u32(100);
  if (!u32(80) || slots>32 || contexts>32 || properties>32) return false;
  size_t pos=104; std::set<uint32_t> keys;
  for (uint32_t i=0; i<contexts; ++i) {
    if (pos+8>bytes.size() || !keys.insert(u32(pos)).second) return false;
    pos+=8;
  }
  keys.clear();
  for (uint32_t i=0; i<properties; ++i) {
    if (pos+8>bytes.size() || !keys.insert(u32(pos)).second) return false;
    const auto length=u32(pos+4); pos+=8;
    if (length>1024 || length>bytes.size()-pos) return false;
    pos+=length;
  }
  return pos==bytes.size();
}
}
nlohmann::json ReadDirectory(const nlohmann::json& options) {
  using Json = nlohmann::json;
  const auto url = options.at("server").get<std::string>();
  ValidateServerUrl(url);
  const auto mode = options.value("mode", "all");
  const auto offset = options.value("offset", 0);
  if ((mode != "all" && mode != "multiplayer" && mode != "coop") || offset < 0 || offset > 1000)
    throw std::runtime_error("Invalid room directory request");
  auto state = std::make_shared<State>();
  Signaling socket(url, [state] {
    std::lock_guard lock(state->mutex); state->opened = true; state->changed.notify_all();
  }, [state](std::string text) {
    std::lock_guard lock(state->mutex);
    if (!state->done) { state->response = std::move(text); state->done = true; state->changed.notify_all(); }
  }, [state](std::string error) {
    std::lock_guard lock(state->mutex);
    if (!state->done) { state->error = std::move(error); state->done = true; state->changed.notify_all(); }
  });
  {
    std::unique_lock lock(state->mutex);
    if (!state->changed.wait_for(lock, std::chrono::seconds(20), [&] { return state->opened || state->done; }))
      throw std::runtime_error("The room server did not respond");
    if (state->done) throw std::runtime_error(state->error);
  }
  socket.Send(Json{{"op", "list"}, {"protocol", 1}, {"build", "redbaron-wire-1"},
                   {"mode", mode}, {"offset", offset}}.dump());
  std::string text;
  {
    std::unique_lock lock(state->mutex);
    if (!state->changed.wait_for(lock, std::chrono::seconds(20), [&] { return state->done; }))
      throw std::runtime_error("The room server did not respond");
    if (!state->error.empty()) throw std::runtime_error(state->error);
    text = state->response;
  }
  auto response = Json::parse(text);
  if (!response.is_object()) throw std::runtime_error("Invalid room directory response");
  if (response.value("op", "") == "error")
    throw std::runtime_error(response.value("message", "Room server rejected the request"));
  if (response.value("op", "") != "rooms" || !response.contains("rooms") ||
      !response["rooms"].is_array() || response["rooms"].size() > 50 ||
      !response.contains("next_offset") ||
      (!response["next_offset"].is_null() && (!response["next_offset"].is_number_integer() ||
       response["next_offset"].get<int>() <= offset || response["next_offset"].get<int>() > 1000)))
    throw std::runtime_error("Invalid room directory response");
  for (const auto& room : response["rooms"]) {
    if (!room.is_object() || !room.contains("room") || !room["room"].is_string() ||
        !std::regex_match(room["room"].get<std::string>(), std::regex("[0-9A-F]{16}")) ||
        !room.contains("host") || !room["host"].is_string() ||
        !std::regex_match(room["host"].get<std::string>(), std::regex("[A-Za-z0-9 _-]{1,15}")) ||
        !room.contains("mode") || !room["mode"].is_string() ||
        (room["mode"] != "multiplayer" && room["mode"] != "coop") ||
        !room.contains("players") || !room["players"].is_number_integer() ||
        !room.contains("capacity") || !room["capacity"].is_number_integer() ||
        !room.contains("session") || !ValidSession(room["session"]))
      throw std::runtime_error("Invalid public room entry");
    const auto players = room["players"].get<int>(), capacity = room["capacity"].get<int>();
    if ((capacity != 2 && capacity != 8) || players < 1 || players >= capacity ||
        (room["mode"] == "coop" && capacity != 2) || (room["mode"] == "multiplayer" && capacity != 8))
      throw std::runtime_error("Invalid public room capacity");
  }
  return response;
}
}
