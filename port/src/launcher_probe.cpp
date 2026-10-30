// Standalone capability/device discovery; never loads an XEX or game assets.
#include <SDL3/SDL.h>
#include <dxgi1_6.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <rex/cvar.h>
#include <sstream>
#include <windows.h>

static std::string Quote(std::string_view value) {
  std::string out = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c >= 32)
      out += c;
    else
      out += ' ';
  }
  return out + '"';
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  const auto folder = std::filesystem::absolute(argv[0]).parent_path();
  auto plugin = LoadLibraryW((folder / "rexgpu-xenos.dll").c_str());
  if (!plugin)
    return 3;
  std::ostringstream json;
#ifdef REDBARON_EOS_ENABLED
  json << "{\"version\":1,\"eosAvailable\":true,\"flags\":{";
#else
  json << "{\"version\":1,\"eosAvailable\":false,\"flags\":{";
#endif
  bool comma = false;
  for (const auto &flag : rex::cvar::GetRegistry()) {
    if (flag.type == rex::cvar::FlagType::Command)
      continue;
    if (comma)
      json << ',';
    comma = true;
    json << Quote(flag.name) << ":{\"default\":" << Quote(flag.default_value)
         << ",\"allowed\":[";
    bool first = true;
    for (const auto &value : flag.constraints.allowed_values) {
      if (!first)
        json << ',';
      first = false;
      json << Quote(value);
    }
    json << "]}";
  }
  json << "},\"gpus\":[";
  IDXGIFactory1 *factory = nullptr;
  bool hdr_active = false;
  unsigned refresh = 0;
  DEVMODEW desktop{};
  desktop.dmSize = sizeof(desktop);
  EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &desktop);
  using DlssProbe = bool (*)(IDXGIAdapter1 *);
  auto dlss_probe = reinterpret_cast<DlssProbe>(GetProcAddress(
      GetModuleHandleW(L"rexruntime.dll"), "RedBaronDlssAvailable"));
  if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                   reinterpret_cast<void **>(&factory)))) {
    IDXGIAdapter1 *adapter = nullptr;
    bool first = true;
    for (UINT index = 0;
         factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++index) {
      DXGI_ADAPTER_DESC1 desc{};
      adapter->GetDesc1(&desc);
      if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
        char name[512]{};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name,
                            sizeof(name), nullptr, nullptr);
        if (!first)
          json << ',';
        first = false;
        const bool dlss = dlss_probe && dlss_probe(adapter);
        json << "{\"name\":" << Quote(name) << ",\"index\":" << index
             << ",\"dlss\":" << (dlss ? "true" : "false") << '}';
        for (UINT i = 0;; ++i) {
          IDXGIOutput *output = nullptr;
          if (adapter->EnumOutputs(i, &output) == DXGI_ERROR_NOT_FOUND)
            break;
          DXGI_OUTPUT_DESC display{};
          if (SUCCEEDED(output->GetDesc(&display)) &&
              display.AttachedToDesktop) {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (EnumDisplaySettingsW(display.DeviceName, ENUM_CURRENT_SETTINGS,
                                     &mode))
              refresh = std::max(refresh, unsigned(mode.dmDisplayFrequency));
            IDXGIOutput6 *output6 = nullptr;
            if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&output6)))) {
              DXGI_OUTPUT_DESC1 advanced{};
              if (SUCCEEDED(output6->GetDesc1(&advanced)))
                hdr_active |= advanced.ColorSpace ==
                              DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
              output6->Release();
            }
          }
          output->Release();
        }
      }
      adapter->Release();
    }
    factory->Release();
  }
  json << "],\"hdrActive\":" << (hdr_active ? "true" : "false")
       << ",\"desktopWidth\":" << desktop.dmPelsWidth
       << ",\"desktopHeight\":" << desktop.dmPelsHeight
       << ",\"refreshHz\":" << refresh << ",\"devices\":[";
  std::string input_error;
  if (SDL_Init(SDL_INIT_GAMEPAD)) {
    SDL_AddGamepadMappingsFromFile(
        (folder / "gamecontrollerdb.txt").string().c_str());
    int count = 0;
    auto ids = SDL_GetGamepads(&count);
    std::map<std::string, unsigned> occurrences;
    bool first = true;
    for (int i = 0; i < count; ++i) {
      auto pad = SDL_OpenGamepad(ids[i]);
      if (!pad)
        continue;
      char guid[33]{};
      SDL_GUIDToString(SDL_GetJoystickGUID(SDL_GetGamepadJoystick(pad)), guid,
                       sizeof(guid));
      const std::string key =
          std::string(guid) + "#" + std::to_string(occurrences[guid]++);
      if (!first)
        json << ',';
      first = false;
      const char *name = SDL_GetGamepadName(pad);
      json << "{\"key\":" << Quote(key)
           << ",\"name\":" << Quote(name ? name : "Gamepad") << '}';
      SDL_CloseGamepad(pad);
    }
    SDL_free(ids);
    SDL_Quit();
  } else
    input_error = SDL_GetError();
  json << "],\"inputError\":" << Quote(input_error) << '}';
  std::ofstream out(argv[1], std::ios::binary);
  out << json.str();
  return out.good() ? 0 : 4;
}
