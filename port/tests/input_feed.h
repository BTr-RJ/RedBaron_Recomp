#pragma once
// Test builds only. Four controllable inputs for repeatable in-game join tests.
// The production executable is built without REDBARON_INPUT_FEED.
#include <chrono>
#include <fstream>
#include <mutex>
#include <rex/input/input_driver.h>

REXCVAR_DEFINE_STRING(redbaron_test_input, "", "Test", "Four-player test input file");
namespace redbaron {
using rex::X_RESULT; using rex::X_STATUS;
class TestInputFeed final : public rex::input::InputDriver {
 public:
  explicit TestInputFeed(std::string path) : InputDriver(nullptr, 0), path_(std::move(path)) {}
  rex::X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& devices) override {
    for (uint64_t i = 0; i < 4; ++i)
      devices.push_back({static_cast<rex::input::DeviceId>(i + 1), 0,
                         "Test player " + std::to_string(i + 1), std::to_string(i), false});
  }
  rex::X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE* out) override {
    const auto index = uint64_t(id) - 1;
    if (index >= 4) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    if (now - read_ > std::chrono::milliseconds(5)) {
      std::ifstream file(path_);
      std::array<unsigned, 4> values{};
      if (file >> values[0] >> values[1] >> values[2] >> values[3]) values_ = values;
      read_ = now;
    }
    if (out) { *out = {}; out->packet_number = ++packet_; out->gamepad.buttons = uint16_t(values_[index]); }
    return X_ERROR_SUCCESS;
  }
  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t,
                                     rex::input::X_INPUT_CAPABILITIES* out) override {
    if (uint64_t(id) < 1 || uint64_t(id) > 4) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out) { *out = {}; out->type = 1; out->sub_type = 1; out->gamepad.buttons = 0xFFFF; }
    return X_ERROR_SUCCESS;
  }
  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId, rex::input::X_INPUT_VIBRATION*) override { return X_ERROR_SUCCESS; }
  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId, uint32_t, rex::input::X_INPUT_KEYSTROKE*) override { return X_ERROR_EMPTY; }
 private:
  std::string path_;
  std::mutex mutex_;
  std::array<unsigned, 4> values_{};
  std::chrono::steady_clock::time_point read_{};
  uint32_t packet_ = 0;
};
}
