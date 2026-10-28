// Device routing for the PC port; independent of game profile/sign-in support.
#pragma once

#include <array>
#include <functional>
#include <utility>
#include <unordered_map>
#include <algorithm>

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

  void SetSelections(std::array<std::string,4> selections) {
    selections_=std::move(selections);
  }

  uint32_t PlayerForDevice(rex::input::DeviceId id) const {
    for(uint32_t slot=0;slot<users_.size();++slot)
      if(std::find(users_[slot].begin(),users_[slot].end(),id)!=users_[slot].end()) return slot;
    return rex::input::kMaxGuestUsers;
  }

  void OnDevicesChanged(const std::vector<rex::input::DeviceInfo>& devices) override {
    for (auto& user : users_) {
      user.clear();
    }
    const uint32_t keyboard_slot = keyboard_player_ ? keyboard_player_ - 1 : 0;
    // Preserve identity for identical pads across disconnects during this run.
    for(auto it=identities_.begin();it!=identities_.end();) {
      if(std::none_of(devices.begin(),devices.end(),[&](const auto& d){return static_cast<uint64_t>(d.id)==it->first;})) it=identities_.erase(it);
      else ++it;
    }
    for(const auto& device:devices) {
      if(device.synthetic || identities_.contains(static_cast<uint64_t>(device.id))) continue;
      unsigned occurrence=0;
      std::string key;
      do {
        key=device.guid+"#"+std::to_string(occurrence++);
      } while(std::any_of(identities_.begin(),identities_.end(),[&](const auto& p){return p.second==key;}));
      identities_[static_cast<uint64_t>(device.id)]=key;
    }
    for (const auto& device : devices) {
      uint32_t slot = keyboard_slot;
      if (!device.synthetic) {
        slot = device.ordinal;
        if (slot < users_.size() && keyboard_player_ && slot >= keyboard_slot) {
          ++slot;
        }
        const auto& identity=identities_.at(static_cast<uint64_t>(device.id));
        auto selected=std::find(selections_.begin(),selections_.end(),identity);
        if(selected!=selections_.end()) slot=static_cast<uint32_t>(selected-selections_.begin());
        else if(slot<users_.size() && selections_[slot]!="auto") slot=rex::input::kMaxGuestUsers;
        if(keyboard_player_ && slot==keyboard_slot) slot=rex::input::kMaxGuestUsers;
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
  std::array<std::string,4> selections_{"auto","auto","auto","auto"};
  std::unordered_map<uint64_t,std::string> identities_;
  std::array<std::vector<rex::input::DeviceId>, rex::input::kMaxGuestUsers> users_;
};

}  // namespace redbaron
