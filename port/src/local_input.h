// Device routing for the PC port; independent of game profile/sign-in support.
#pragma once

#include <array>
#include <functional>
#include <utility>

#include <rex/input/device_assignment.h>

namespace redbaron {

class LocalDeviceAssignment final : public rex::input::DeviceAssignment {
 public:
  // 0: SDK-compatible shared player 1. 1..4: reserve that player for MnK.
  // The observer receives a zero-based slot, or kMaxGuestUsers if unassigned.
  using Observer = std::function<void(const rex::input::DeviceInfo&, uint32_t)>;

  explicit LocalDeviceAssignment(uint32_t keyboard_player, Observer observer = {})
      : keyboard_player_(keyboard_player <= rex::input::kMaxGuestUsers
                             ? keyboard_player : 0),
        observer_(std::move(observer)) {}

  void OnDevicesChanged(const std::vector<rex::input::DeviceInfo>& devices) override {
    for (auto& user : users_) {
      user.clear();
    }
    const uint32_t keyboard_slot = keyboard_player_ ? keyboard_player_ - 1 : 0;
    for (const auto& device : devices) {
      uint32_t slot = keyboard_slot;
      if (!device.synthetic) {
        slot = device.ordinal;
        if (slot < users_.size() && keyboard_player_ && slot >= keyboard_slot) {
          ++slot;
        }
      }
      if (slot < users_.size()) {
        users_[slot].push_back(device.id);
      }
      if (observer_) {
        observer_(device, slot < users_.size() ? slot : rex::input::kMaxGuestUsers);
      }
    }
  }

  void DevicesForUser(uint32_t user_index,
                      std::vector<rex::input::DeviceId>& out) const override {
    out.clear();
    if (user_index < users_.size()) {
      out = users_[user_index];
    }
  }

 private:
  uint32_t keyboard_player_;
  Observer observer_;
  std::array<std::vector<rex::input::DeviceId>, rex::input::kMaxGuestUsers> users_;
};

}  // namespace redbaron
