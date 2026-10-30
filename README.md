# RedBaron_Recomp

A native Windows x64 recompilation of **Snoopy Flying Ace** for Xbox 360, built with ReXGlue/ReXAuto.

**v0.10 is a public testing release.** The installer builds the game locally from files you supply. No original game files or DLCs are distributed here.

[Download v0.10](https://github.com/BTr-RJ/RedBaron_Recomp/releases/tag/v0.10) | [Installation](docs/INSTALLATION.md) | [Report a problem](https://github.com/BTr-RJ/RedBaron_Recomp/issues)

## Install and play

1. Download **RedBaron_Recomp-v0.10-windows-x64.zip** and extract it. Choose the larger **offline** ZIP for bundled build tools; both contain the same port.
2. Open **RedBaron_Launcher.exe**. In **Install**, select your extracted game folder / `default.xex`, or the compatible original Xbox 360 content package (including packages without an extension).
3. Optionally select your **DLC files** folder. Select a new, empty installation folder and click **Install**. Missing tools are detected and downloaded, or installed from the offline package. Allow the Microsoft installer elevation prompt if requested.
4. Wait for extraction, code generation and compilation. Successful installation removes temporary build files and preserves reusable tools, saves and logs.
5. Choose **Game language**, video settings and controls, then **Save** and **Play**. Epic sign-in enables online features; offline play remains available if authentication is unavailable.

To add DLCs later, select the DLC folder in the installed launcher and click Install. An existing installation receives only the DLCs; the game is not recompiled. DLC files are supplied separately by the player.

See the [full installation guide](docs/INSTALLATION.md) for requirements, controls, updates, saves and logs.

## Features

- English launcher with installation progress, tool detection, optional shortcut and GitHub update checks.
- Seven original game languages: English, French, German, Italian, Japanese, Portuguese and Spanish. Select in the launcher and restart the game. The startup splash image sequence is disabled.
- Keyboard/mouse-button and SDL controller mapping, device assignment and separate keyboard player selection. Mouse movement does not steer the aircraft.
- Output resolutions through 4K and beyond, windowed/borderless fullscreen, render scaling, VSync, anisotropic filtering, FXAA and an in-game FPS counter.
- Higher frame-rate targets with an engine/physics timing correction. Targets above 120 FPS remain experimental.
- Spatial FSR/CAS, experimental FSR 3.1.5 and NVIDIA DLSS Super Resolution/DLAA quality presets. No frame generation.
- Optional HDR10 output with Windows HDR enabled; the source image is SDR converted to HDR10.
- EOS P2P transport, public/private lobbies, native matchmaking and Epic friends/invites through the official overlay. Public PvP Quick Game searches compatible modes and chooses the lowest measured reachable ping.
- Native achievement and leaderboard integration backed by EOS. The catalog is read from the player's local game metadata.
- Local two-player split-screen and bots. Co-op remains limited to two participants. Supported competitive online sessions have capacity for up to eight.

## Testing status and known limitations

A clean Windows 11 installation and an Internet lobby/flight between two PCs have been exercised. This does **not** establish complete coverage of every mode, eight simultaneous human players, invitation acceptance, end-of-round statistics or achievement synchronization.

- Four configurable controller profiles do not enable four cameras. **Four-player local play is not implemented.**
- Friends/overlay require Epic Account Services authorization. Public access/brand review has not been verified; accounts outside the developer organization or approved test accounts may be restricted. Sign-in failures do not prevent offline play.
- Temporal upscaling may show HUD shimmer, ghosting and moving-object artifacts. HUD separation and full object motion vectors remain unfinished. Bilinear, FSR 1 or CAS provide alternatives.
- Leaderboard writes, friend filters, full pagination and weekly rollover need gameplay testing. Weekly periods require service-side administration. Historic Xbox Live scores are not imported.
- Only Windows x64 is supported. Linux, Android and cross-platform play are future work.

Read [Online](docs/EOS.md), [Multiplayer](docs/MULTIPLAYER.md), [Video](docs/VIDEO.md) and [Leaderboards](docs/LEADERBOARDS.md). Report reproducible problems through [Issues](https://github.com/BTr-RJ/RedBaron_Recomp/issues), with version, hardware and a redacted log. Do not attach game files, EOS configuration or authentication data.

## Build and contribute

The installer is recommended for players. Developers can follow [Building from source](docs/BUILDING.md). Generated game sources, assets, installed SDKs, saves and credentials are excluded from Git. SDK changes are maintained as [patches](patches/README.md).

See [Contributing](CONTRIBUTING.md) and [Credits](CREDITS.md). This is an independent fan project, not an official release or endorsement by the original game's rights holders or service vendors.
