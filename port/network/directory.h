#pragma once
#include <nlohmann/json.hpp>

namespace redbaron::net {
// One page, using the same certificate validation as the room connection.
nlohmann::json ReadDirectory(const nlohmann::json& options);
}
