# RedBaron_Recomp launcher

Native Windows x64 launcher built with WPF/.NET Framework 4.8, without NuGet packages. Uses the simple dark theme requested by the project owner. The interface, game action labels, help, validation and installer messages are in English.

## Start and play

Run `launcher/out/RedBaron_Launcher.exe`. In a development checkout, it finds the project root and uses `build_cpp_release`. In an installation created by the launcher, it lives in the selected root folder.

The desktop shortcut opens the launcher and respects **Show launcher next time**. Clear the checkbox and save to start the game directly next time. Hold **Shift while opening** or run `RedBaron_Launcher.exe --settings` to return to settings. Opening `snoopyflyingace.exe` directly still works and does not use this launcher preference.

When starting the game, the launcher remains visible while the runtime prepares
previously discovered shaders and pipelines. The game window opens after that work.
A new installation has no complete cache: shaders first encountered in gameplay
may still compile at runtime and become available for preparation on later launches.

| Tab | Available features |
|---|---|
| Install | Select extracted game files, validate XEX/Title ID `584109B5`, copy to an empty destination, generate C++, build and optionally create a desktop shortcut; select a folder with owned DLC packages to install compatible game content |
| Video | GPU, window resolutions through 8K, internal 1×–6× scaling, fullscreen/windowed mode, monitor VSync, anisotropic filtering, FXAA, spatial FSR 1/CAS, experimental FSR 3.1.5/DLSS, FSR sharpness, FPS counter and HDR10 output; options are checked against the installed runtime |
| Controls | Four configurable profiles, SDL controller selection, button/trigger/stick-direction remapping, keyboard and mouse-button editing/capture |
| Online | Primary player name, experimental WebRTC room creation/joining and LAN host IPv4; public server not hosted yet; UPnP disabled |
| Update | Check public GitHub releases and download the matching Windows ZIP after SHA-256 verification |
| Save / Play | Save with backups and start the game; Play saves first, and errors prevent launch and appear in the footer |

For downloadable content, open **Install**, select the folder containing your own
STFS packages in **DLC files (optional)** and use the single **Install** button.
New installations build the game and then add the selected DLCs. When a game is
already installed, that button becomes **Install DLCs**, and only adds content.
The launcher recognizes this title's
marketplace packages, preserves existing installations and ignores avatar items.
Close the game before installing. In a local test, both supplied marketplace
packages were enumerated and mounted by the game; individual content and maps still
need gameplay verification. No game content is included with this project.

## Current limits

The Install tab checks Clang, CMake, Ninja, Visual C++ Build Tools and the Windows
SDK automatically. **Build tools** shows versions and whether each component is
already compatible or needs installation. The same inventory drives bootstrap;
compatible tools are reused, including cached tool paths from earlier installs.
Offline builds carry a `Tools` folder beside the launcher. Their embedded SHA-256
manifest validates only the components needed on the current machine. Microsoft
components install from the signed layout with `--noweb`; portable tools are
copied into the installation only when missing. Normal builds retain downloads.

Two local players now work in the native split-screen mode, including bots. Four input profiles do not enable four local cameras. For keyboard + controller, select Exclusive · player 2 and No controller/Automatic for Player 2, then press its Confirm binding to join in Enlist Now → Local Multiplayer. One keyboard/mouse serves one player. Profiles 3 and 4 remain reserved for future support. Actions follow the **FlyingAce** preset; in-game prompts still use Xbox labels. Mouse motion does not control the camera.

The runtime includes spatial FSR 1/CAS and experimental **FSR 3.1.5 / NVIDIA DLSS Super Resolution**. Temporal reconstruction uses real depth/camera motion, but object velocities and separate HUD composition remain unfinished: moving objects/HUD can leave trails. No frame generation. DLSS requires an NGX-compatible NVIDIA GPU/driver, checked per adapter.

FPS targets reach 240, with values above 120 experimental. Physics now uses measured game time instead of a fixed 1/30 second per frame; at 120 FPS the tutorial measured 5.008 simulated seconds over 5.008 real seconds. Other gameplay still needs validation. Video includes **Show in-game FPS counter** and **Use HDR output when available**. HDR10 maps the original SDR image to 200-nit white when Windows HDR is active; it does not add native HDR lighting. VSync limits the presentation queue to one frame. See [implementation, reproduction and remaining work](../docs/VIDEO.md).

The launcher starts centered in the monitor's work area and fits smaller screens. Use the native window buttons to maximize or close it. F11/Esc remain available as launcher shortcuts. The Video tab's display mode affects the game. Borderless game fullscreen uses the desktop resolution; window resolution is measured in physical pixels. Ultrawide output retains the game's 16:9 framing.

Player names use the project's rule: 1–15 ASCII characters, starting with a letter or number; letters, numbers, internal spaces, hyphens and underscores are allowed. Trailing spaces are rejected. The name changes the primary XAM display name while retaining the existing save folder. LAN is a separate experimental service, not an Xbox Live account. No router ports are opened automatically. Sound and Achievements tabs remain planned.

Windows-owned folder dialogs and external tool diagnostics may follow the installed Windows/tool language. The launcher requests English UI resources for its process without changing Windows language settings.

## Installation

Select an extracted folder containing `default.xex`, `Config` and `Packages`,
`default.xex` itself, or the original Arcade STFS package for title `584109B5`
(type `000D0000`). A package may have no extension. A folder containing a single
compatible package is also accepted. The bundled content tool validates and
extracts it before code generation. Ordinary ZIP/RAR archives are not STFS files.
Launcher builds made with the SDK bundle include the modified SDK, package
extractor and networking component. Missing CMake, Ninja and Clang are downloaded
from pinned official releases; Microsoft C++ Build Tools and the Windows SDK are
installed when missing, with Windows administrator approval.

The Video tab requires the updated SDK patch. For launcher builds without a bundled SDK, build an isolated SDK installation with `scripts/build-video-sdk.ps1 -SdkSource <source> -Fsr315 -Dlss` for all current video features, then select it in Build tools. This launcher is not yet the completed official installer and has not been published in Releases.

The launcher embeds authored port source/configuration files, the installer and, when available at build time, an SDK bundle. It does not embed the XEX, game assets, generated game C++ or saves. Code is generated locally. Installation rejects occupied destinations, linked source/destination paths and paths that overlap the source or a separately selected SDK. A clean installation and launcher flow were exercised with tools already present. Missing CMake, Ninja and Python were tested in isolated paths; LLVM was downloaded and used to compile a Windows program. Installation of Microsoft Build Tools on a machine without them still needs verification.

Update checks query public releases from the project repository. The downloader
requires the official GitHub asset's SHA-256 and saves a verified ZIP in
`launcher-data/updates`. Installing an update over an existing game build is not
implemented yet. No public release exists at this stage.

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

Both installer variants show a persistent, stage-based progress bar. Downloads,
archive extraction and file copies report measured bytes; Ninja reports completed
build steps; DLC installation reports completed packages. Microsoft setup and
other operations without measurable totals show activity rather than a fabricated
percentage. Errors/cancellation stop the bar; only a completed game/DLC workflow
shows installation complete at 100%. Structured progress remains in the log.

Validated: C++ build, routing/remapping tests, C# settings/validation/argument/shortcut tests, old-label migration without binding changes, a complete clean installation, rejection of overlapping/occupied/incompatible destinations, and Save/Play/direct launch/return via UIAutomation. The English UI was checked at normal and compact window sizes. The embedded source ZIP was checked against current sources and contains no game data. Local evidence lives in the ignored `prioridade5_launcher/` folder. Custom mappings, multiple physical controllers and higher resolutions still need gameplay testing.

## DLSS modes and LAN preview

DLSS exposes DLAA (native), Quality, Balanced, Performance and Ultra Performance. Saving selects the internal render scale automatically from the output size. At 4K the integer scales are 3, 2, 2, 2 and 1; Balanced/Performance can share render cost. The FPS counter shows the active HDR10/SDR output. The mouse cursor is hidden in game fullscreen.

Enable experimental LAN on each installation and use Online → Player Match → Find Game with matching modes/options. An empty search creates a public match. Automatic discovery uses 255.255.255.255; joining clients can enter the host IPv4 instead. Two instances on one PC completed Team DogFight discovery, lobby and gameplay. Separate-PC testing is still pending. Internet matchmaking, invitations, ranked services and rollback are unavailable. See [setup, ports, tests and remaining work](../docs/MULTIPLAYER.md).
