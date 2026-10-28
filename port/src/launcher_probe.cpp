// Standalone capability/device discovery; never loads an XEX or game assets.
#include <windows.h>
#include <dxgi1_2.h>
#include <SDL3/SDL.h>
#include <rex/cvar.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

static std::string Quote(std::string_view value) {
  std::string out = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c >= 32) out += c;
    else out += ' ';
  }
  return out + '"';
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const auto folder = std::filesystem::absolute(argv[0]).parent_path();
  auto plugin = LoadLibraryW((folder / "rexgpu-xenos.dll").c_str());
  if (!plugin) return 3;
  std::ostringstream json;
  json << "{\"version\":1,\"flags\":{";
  bool comma = false;
  for (const auto& flag : rex::cvar::GetRegistry()) {
    if (flag.type == rex::cvar::FlagType::Command) continue;
    if (comma) json << ',';
    comma = true;
    json << Quote(flag.name) << ":{\"default\":" << Quote(flag.default_value)
         << ",\"allowed\":[";
    bool first = true;
    for (const auto& value : flag.constraints.allowed_values) {
      if (!first) json << ',';
      first = false;
      json << Quote(value);
    }
    json << "]}";
  }
  json << "},\"gpus\":[";
  IDXGIFactory1* factory = nullptr;
  if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
    IDXGIAdapter1* adapter = nullptr;
    bool first = true;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
      DXGI_ADAPTER_DESC1 desc{};
      adapter->GetDesc1(&desc);
      if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
        char name[512]{};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
        if (!first) json << ',';
        first = false;
        json << "{\"name\":" << Quote(name) << ",\"index\":" << index << '}';
      }
      adapter->Release();
    }
    factory->Release();
  }
  json << "],\"devices\":[";
  std::string input_error;
  if (SDL_Init(SDL_INIT_GAMEPAD)) {
    SDL_AddGamepadMappingsFromFile((folder / "gamecontrollerdb.txt").string().c_str());
    int count = 0;
    auto ids = SDL_GetGamepads(&count);
    std::map<std::string, unsigned> occurrences;
    bool first = true;
    for (int i = 0; i < count; ++i) {
      auto pad = SDL_OpenGamepad(ids[i]);
      if (!pad) continue;
      char guid[33]{};
      SDL_GUIDToString(SDL_GetJoystickGUID(SDL_GetGamepadJoystick(pad)), guid, sizeof(guid));
      const std::string key = std::string(guid) + "#" + std::to_string(occurrences[guid]++);
      if (!first) json << ',';
      first = false;
      const char* name = SDL_GetGamepadName(pad);
      json << "{\"key\":" << Quote(key) << ",\"name\":" << Quote(name ? name : "Gamepad") << '}';
      SDL_CloseGamepad(pad);
    }
    SDL_free(ids);
    SDL_Quit();
  } else input_error = SDL_GetError();
  json << "],\"inputError\":" << Quote(input_error) << '}';
  std::ofstream out(argv[1], std::ios::binary);
  out << json.str();
  return out.good() ? 0 : 4;
}
