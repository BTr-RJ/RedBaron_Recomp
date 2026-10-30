#pragma once
#include <rex/system/xam/lan_lobby.h>
#include <optional>
#include <string>
#include <string_view>
namespace redbaron::eos {
std::string EncodeNativeRecord(const rex::system::xam::LanRecord& record);
std::optional<rex::system::xam::LanRecord> DecodeNativeRecord(std::string_view record);
}
