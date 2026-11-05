# Installation

## Requirements

- Windows 10/11 x64 with .NET Framework 4.8, an updated graphics driver and a Direct3D 12-capable GPU.
- Your own compatible Snoopy Flying Ace Xbox 360 files (Title ID `584109B5`). Optional DLC files are not included in either download.
- Free space for the game, extracted files, SDK and temporary compilation output. Installation needs more space than the finished game; offline tools need additional space.
- Internet access for tool downloads and EOS. The offline package includes build tools; Microsoft components can require a restart.

## Which download?

| Package | Includes |
|---|---|
| `RedBaron_Recomp-v0.20-windows-x64.zip` | Launcher, authored port sources and runtime/SDK payload; detects tools and downloads missing dependencies |
| `RedBaron_Recomp-v0.20-windows-x64-offline.zip` | The same port plus portable tools and Microsoft Build Tools layout |

Download from [project releases](https://github.com/BTr-RJ/RedBaron_Recomp/releases). Extract the complete ZIP before opening the launcher. Keep the offline package's `Tools` folder beside it until installation finishes. No game files or DLCs are bundled.

## First installation

1. Launch `RedBaron_Launcher.exe` and open **Install**.
2. Under **Game files**, browse for an extracted folder, `default.xex`, or the compatible original content package. Files without an extension are accepted; title/content type are validated.
3. Under **DLC files**, optionally select a folder with supported gameplay DLC packages. Avatar items are not supported.
4. Choose a new, empty **installation folder**, outside protected system folders. Optionally enable a shortcut.
5. Click **Install** once. The installer detects tools, prepares dependencies, extracts/copies content, generates code and compiles the port. Progress/logs show the active stage. Do not close the launcher during installation.
6. After success, set **Game language**, Video, Controls and Online **Player name**. Click **Save**, then **Play**. Language changes take effect on the next launch.

The seven original language files are used; translations are not downloaded. Portuguese is the game's original Portuguese localization. The launcher remains in English. The startup splash image sequence is disabled regardless of old splash preferences.

## Adding DLCs later

When **Installation found** is shown, select the DLC folder and click Install. Only supported DLCs are installed; identical existing content is retained. The executable and settings are preserved.

## Controls

Use the game's **FlyingAce** preset. **W/A/S/D** navigate, **Space** confirms, **Backspace** goes back and **Enter/Esc** send Start. Flight uses WASD, arrows for maneuvers, mouse buttons or J/K for weapons, Space for boost, C for brake and E for action. Mouse movement is disabled.

See [CONTROLS.txt](../port/config/CONTROLS.txt). For controller plus keyboard in local play, select **Exclusive player 2** for keyboard and keep player 1 on the controller. Save, restart and join through the local menu. Two native split-screen cameras are supported.

## Online

Sign in to Epic when prompted. Create a public Player Match or use Quick Game / Custom Match to find compatible hosts. Private matches use Epic invitations. RT in the hangar/lobby opens the official friends overlay; **Shift+F3** is its shortcut. Co-op has a separate two-player category.

EOS handles P2P and relay fallback. No room-server address or launcher invitation code is needed. Public access depends on Epic application authorization; see [EOS](EOS.md) for restrictions. Configure the player name in the launcher.

## Saves, logs and cleanup

Personal runtime settings are in `build_cpp_release/snoopyflyingace.toml` within the installation. Launcher settings/logs live in `launcher-data`. Each Play records a new log; use **Update > Open logs folder**. Saves and DLC content use the runtime user-data directory (normally the user's Documents area), separately from build files.

Successful installation removes owned temporary compilation/extraction files and keeps reusable tools in the local application-data cache. Failures retain diagnostic logs. Preserve your saves and original game/DLC files.

## Updates

**Update > Check for updates** checks published releases, including test prereleases. Downloaded ZIP size and SHA-256 are checked against GitHub metadata. A matching version is already up to date.

For v0.10 and v0.20, updates are downloaded installer ZIPs; they do not replace a running game in place. Extract the new installer and install into a new empty folder using your game files/original package. Keep the old installation until the new one works, and preserve saves/settings. An existing installation's Install action adds DLCs; it does not rebuild the port.

The next launcher, **v0.21**, adds automatic installation of updates. In an installed v0.21 game, use **Update > Check for updates > Download and install update**. Close the game first. The launcher downloads and verifies the standard installer, rebuilds using your already installed game files in a separate working folder, checks the new runtime, applies the update and reopens. Preparing the build can take several minutes and may need internet access or Microsoft tool approval when a dependency is missing. You can cancel preparation. File replacement finishes without cancellation.

Your original game files, DLCs, saves, runtime TOML, existing INI files and launcher settings are preserved. The executable, runtime DLLs, online components, launcher and licenses are updated together. Backups and an update log remain under `launcher-data/update-<id>`. A replacement failure restores the previous files; reopening the launcher after an interruption starts recovery before Play is allowed. Keep that working folder if recovery reports a problem.

**One-time transition from v0.10/v0.20:** those already distributed launchers still only download ZIPs. Once v0.21 is published, download and extract its full installer, open it, select your existing game through **Install > Use existing installation**, then choose **Update > Update selected installation**. This performs the rebuild and migration in the existing game folder. Subsequent updates can be downloaded and installed from the new launcher without manual extraction or selecting game files again. An incomplete/failed initial installation must still be installed into a new empty folder.

## Troubleshooting

- A compile error mentioning `std::chrono::clock_time_conversion` can occur in v0.10 when the installer reuses a MinGW/LLVM-MinGW Clang (including tools installed by another recompilation launcher). The bundled SDK needs **x64 Clang targeting `x86_64-pc-windows-msvc`**, plus Visual C++ Build Tools. A recent Clang version alone is not sufficient. The v0.20 installer checks the target before reusing Clang. For the original v0.10 installer, start the launcher from a terminal where official Windows LLVM is first on `PATH` (`clang++ --version` must show that target), then install into a new empty folder to avoid reusing the failed CMake cache. Keep the original game files and saves.
- Keep `render_target_path_d3d12 = "rov"`; automatic/RTV can produce a white 3D scene on NVIDIA.
- For temporal artifacts, try Bilinear, FSR 1 or CAS. HDR needs Windows HDR enabled on the active monitor.
- Hold **Shift** while opening the shortcut, or pass `--settings`, to return to the launcher when direct play is enabled.
- First-time Microsoft/EOS dependency installation may require elevation. The game does not need permanent administrator execution.
- Report errors and redacted logs through [Issues](https://github.com/BTr-RJ/RedBaron_Recomp/issues). Never attach `eos/config.ini`, tokens or original game/DLC files.
