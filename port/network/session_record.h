#pragma once
#include <string_view>
namespace redbaron::net {
// Validates the untrusted discovery payload before passing it to the game.
bool ValidSessionRecord(std::string_view hex);
}
