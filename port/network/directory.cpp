#include "directory.h"
#include "signaling.h"
#include "session_record.h"
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
  return value.is_string() && ValidSessionRecord(value.get_ref<const std::string&>());
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
