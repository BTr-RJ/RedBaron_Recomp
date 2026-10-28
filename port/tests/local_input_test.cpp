#include "local_input.h"
#include "pad_mapping.h"

#include <iostream>
#include <stdexcept>

using rex::input::DeviceId;
using rex::input::DeviceInfo;

DeviceInfo Pad(uint64_t id, uint32_t ordinal) {
  DeviceInfo device;
  device.id = static_cast<DeviceId>(id);
  device.ordinal = ordinal;
  return device;
}

void Expect(const redbaron::LocalDeviceAssignment& policy, uint32_t user,
            std::initializer_list<uint64_t> expected) {
  // A nonempty output must also be cleared for empty/invalid users.
  std::vector<DeviceId> actual{static_cast<DeviceId>(999)};
  policy.DevicesForUser(user, actual);
  std::vector<DeviceId> wanted;
  for (auto id : expected) wanted.push_back(static_cast<DeviceId>(id));
  if (actual != wanted) throw std::runtime_error("Unexpected routing for user " +
                                                std::to_string(user));
}

int main() {
  try {
    auto keyboard = Pad(10, UINT32_MAX);
    keyboard.synthetic = true;
    const auto pad1 = Pad(20, 0);
    const auto pad2 = Pad(30, 1);
    const auto pad3 = Pad(40, 2);
    const auto pad4 = Pad(50, 3);

    redbaron::LocalDeviceAssignment shared(0);
    shared.OnDevicesChanged({pad1, pad2, pad3, pad4, keyboard});
    Expect(shared, 0, {20, 10});
    Expect(shared, 1, {30});
    Expect(shared, 2, {40});
    Expect(shared, 3, {50});
    Expect(shared, 4, {});
    Expect(shared, UINT32_MAX, {});

    // Each reserved slot consumes one of four guest slots; never duplicate pads.
    for (uint32_t player = 1; player <= 4; ++player) {
      uint32_t overflow = 0;
      redbaron::LocalDeviceAssignment separate(player,
          [&](const DeviceInfo& d, uint32_t slot) {
            if (slot == 4 && d.id == pad4.id) ++overflow;
          });
      // Enumeration order must not determine the controller's player number.
      separate.OnDevicesChanged({keyboard, pad3, pad1, pad4, pad2});
      uint64_t expected_pad = 20;
      for (uint32_t slot = 0; slot < 4; ++slot) {
        if (slot == player - 1) {
          Expect(separate, slot, {10});
        } else {
          Expect(separate, slot, {expected_pad});
          expected_pad += 10;
        }
      }
      if (overflow != 1) throw std::runtime_error("Fourth pad was not reported unassigned");
    }

    // Hot unplug must not move the surviving controller into the vacated slot.
    redbaron::LocalDeviceAssignment separate(1);
    separate.OnDevicesChanged({keyboard, pad1, pad2});
    separate.OnDevicesChanged({keyboard, pad2});
    Expect(separate, 0, {10});
    Expect(separate, 1, {});
    Expect(separate, 2, {30});
    // The SDK reuses a vacant ordinal on reconnection with a new device ID.
    separate.OnDevicesChanged({Pad(60, 0), pad2, keyboard});
    Expect(separate, 1, {60});
    Expect(separate, 2, {30});
    separate.OnDevicesChanged({});
    for (uint32_t slot = 0; slot < 4; ++slot) Expect(separate, slot, {});

    // Huge ordinals must be ignored without wrapping into a valid player.
    redbaron::LocalDeviceAssignment invalid_option(99);
    invalid_option.OnDevicesChanged({keyboard, pad1, Pad(70, UINT32_MAX)});
    Expect(invalid_option, 0, {10, 20});
    auto first=pad1, second=pad2;
    first.guid="same-guid"; second.guid="same-guid";
    redbaron::LocalDeviceAssignment selected(0);
    selected.SetSelections({"same-guid#1","same-guid#0","none","none"});
    selected.OnDevicesChanged({first,second,keyboard});
    Expect(selected,0,{30,10}); Expect(selected,1,{20});
    selected.OnDevicesChanged({second,keyboard});
    Expect(selected,0,{30,10}); Expect(selected,1,{});
    first.id=static_cast<DeviceId>(80);
    selected.OnDevicesChanged({first,second,keyboard});
    Expect(selected,0,{30,10}); Expect(selected,1,{80});

    auto mapping=redbaron::IdentityPadMap();
    redbaron::PadValues values{};
    for(size_t i=0;i<values.size();++i) values[i]=static_cast<int>(i)*1300;
    if(redbaron::RemapPad(values,mapping)!=values) throw std::runtime_error("Identity mapping changed input");
    mapping[0]=15; mapping[15]=0; mapping[1]=-1;
    auto mapped=redbaron::RemapPad(values,mapping);
    if(mapped[0]!=values[15] || mapped[15]!=values[0] || mapped[1]!=0) throw std::runtime_error("Button/trigger mapping failed");
    const auto before=mapping;
    if(redbaron::ParsePadMap("0,1",mapping) || mapping!=before) throw std::runtime_error("Invalid mapping was accepted");
    if(!redbaron::ParsePadMap("",mapping) || mapping!=redbaron::IdentityPadMap()) throw std::runtime_error("Default mapping failed");
    std::cout << "OK: shared/reserved slots, overflow, ordering, disconnect, reconnect\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
