# RedBaron_Recomp launcher

Native Windows x64 launcher built with WPF/.NET Framework 4.8, without NuGet packages. Uses the simple dark theme requested by the project owner. The interface, game action labels, help, validation and installer messages are in English.

## Start and play

Run `launcher/out/RedBaron_Launcher.exe`. In a development checkout, it finds the project root and uses `build_cpp_release`. In an installation created by the launcher, it lives in the selected root folder.

The desktop shortcut opens the launcher and respects **Show launcher next time**. Clear the checkbox and save to start the game directly next time. Hold **Shift while opening** or run `RedBaron_Launcher.exe --settings` to return to settings. Opening `snoopyflyingace.exe` directly still works and does not use this launcher preference.

| Tab | Available features |
|---|---|
| Install | Select extracted game files, validate XEX/Title ID `584109B5`, copy to an empty destination, generate C++, build and optionally create a desktop shortcut |
| Video | GPU, 720p/1080p/1440p/4K resolution, fullscreen/windowed mode, VSync, anisotropic filtering and FXAA; options are checked against the installed runtime |
| Controls | Four configurable profiles, SDL controller selection, button/trigger/stick-direction remapping, keyboard and mouse-button editing/capture |
| Online | Validated name saved for future integration; UPnP remains disabled while online play is postponed |
| Save / Play | Save with backups and start the game; Play saves first, and errors prevent launch and appear in the footer |

## Current limits

The game recognizes only player 1. Configuring four input profiles does not enable four-player matches. One keyboard/mouse can share player 1 or be reserved for one profile at a time. Other profiles remain saved for later use. Actions follow the **FlyingAce** preset; in-game prompts still use Xbox labels. Mouse motion does not control the camera.

The installed runtime does not include FSR, and DLSS has not been integrated. These choices remain unavailable even if the GPU supports them in other games. Additional FPS limiting/unlocking is not implemented. Higher resolutions still need visual testing during gameplay. FXAA operates independently of upscaling.

Player names use the project's rule: 1–15 ASCII characters, starting with a letter or number; letters, numbers, internal spaces, hyphens and underscores are allowed. Trailing spaces are rejected. The name stays in launcher settings; it does not change the XAM profile or connect to Xbox Live. No router ports are opened. Sound and Achievements tabs remain planned.

Windows-owned folder dialogs and external tool diagnostics may follow the installed Windows/tool language. The launcher requests English UI resources for its process without changing Windows language settings.

## Installation

Provide an **already extracted folder** containing `default.xex`, `Config` and `Packages`, a compatible experimental SDK, and CMake, Ninja and Clang on PATH. STFS extraction and automatic tool installation are not included.

The launcher embeds authored port source/configuration files and the installer script. It does not embed the SDK, XEX, game assets, generated game C++ or saves. Code is generated locally, and runtime DLLs are copied from the SDK you provide. Installation rejects occupied destinations, linked source/destination paths and paths that overlap the source or SDK.

Logs are written to `launcher-data/install-<date>/install.log` under the source installation. The destination receives `.redbaron-install.json` with `installing`, `ready` or `failed` status. Cancellation stops the installer and its compiler processes; partial files remain for inspection. A retry requires an empty destination. Source files are not removed.

## Settings and input

- `launcher-settings.json` in the installation root stores four profiles, device selectors, player name and launch preference.
- `build_cpp_release/snoopyflyingace.toml` stores game settings. Unknown keys/tables are preserved; saves create dated backups.
- Old saved display labels are refreshed in English on load, using stable action keys. Keyboard/controller bindings and player preferences are preserved. Loading does not write files.
- Saving is blocked while this installation's game is running.
- `redbaron_pad_slots`: four semicolon-separated selectors, each `auto`, `none` or SDL `GUID#occurrence`.
- `redbaron_pad_map_p1` through `p4`: 24 source indices in the order defined by `Model.cs` / `pad_mapping.h`; `-1` unassigns an action, and an empty string means identity mapping.
- `redbaron_keyboard_player`: 0 shares player 1; 1–4 reserves that player. `mnk_mode=false` disables keyboard input and ignores the reservation.

Identical controllers use GUID plus occurrence order. Disconnecting one during a session does not renumber another. Check assignments if connection order changes between sessions. Analog inputs mapped to digital actions use a halfway threshold. Guide retains SDK behavior.

## Build and verify

```powershell
# Supply the SDK and game/ locally, as described in the main README.
.\scripts\build.ps1
.\launcher\build.ps1

cmake -S port -B build_cpp_release -DREDBARON_BUILD_INPUT_TESTS=ON
cmake --build build_cpp_release --target redbaron_input_tests --parallel 4
ctest --test-dir build_cpp_release --output-on-failure
Start-Process .\launcher\out\RedBaron_Launcher.exe -ArgumentList '--self-test','C:\test-folder' -Wait
```

The launcher uses the Windows .NET Framework compiler and WPF assemblies. `redbaron_probe.exe` queries runtime/GPU flags and SDL devices without loading game files. The controller database and its license are included in installations.

Validated: C++ build, routing/remapping tests, C# settings/validation/argument/shortcut tests, old-label migration without binding changes, a complete clean installation, rejection of overlapping/occupied/incompatible destinations, and Save/Play/direct launch/return via UIAutomation. The English UI was checked at normal and compact window sizes. The embedded source ZIP was checked against current sources and contains no game data. Local evidence lives in the ignored `prioridade5_launcher/` folder. Custom mappings, multiple physical controllers and higher resolutions still need gameplay testing.
