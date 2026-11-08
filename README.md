# RedBaron_Recomp

A native Windows x64 recompilation of **Snoopy Flying Ace** for Xbox 360, built with ReXGlue/ReXAuto.

**v0.23 is a public testing release.** It fixes the launcher version display: the original v0.22 showed **0.21** even though its updater already used **0.22.0**. The version label now follows the actual updater version, **0.23.0**. It retains the LLVM extraction fix from v0.22, automatic updates from v0.21 and the v0.20 compiler-selection fix. The installer builds the game locally from files you supply. No original game files or DLCs are distributed here.

v0.23 also keeps the verified compatible Clang ahead of shared CMake/Ninja directories after importing the Visual Studio environment. This fixes a reproduced case where an incompatible Clang in a reused tool directory hid the selected compiler. Final tool checks now report selected paths and identify incompatible tools.

[Download v0.23](https://github.com/BTr-RJ/RedBaron_Recomp/releases/tag/v0.23) | [Step-by-step guide](docs/INSTALLATION.md) | [Report a problem](https://github.com/BTr-RJ/RedBaron_Recomp/issues)

**Epic sign-in update — November 7, 2026:** If you downloaded the game and saw **Application Access Is Restricted**, Epic's application-access restriction likely prevented your account from signing in. The project owner reports that the RedBaron_Recomp brand is now published in Epic Account Services, so previously affected players are encouraged to try again. Close and reopen the game, then retry Epic sign-in. Successful sign-in with an account outside the developer organization still needs player confirmation. See [details and reporting steps](docs/EOS.md#epic-sign-in-update--november-7-2026).

| Start here | Instructions |
|---|---|
| First installation, or an earlier installation failed | [Install v0.23 into a new empty folder](docs/INSTALLATION.md#install-the-game-or-retry-a-failed-installation) |
| Working v0.10/v0.20 installation | [One-time migration to v0.23](docs/INSTALLATION.md#move-a-working-v010-or-v020-installation-to-v023) |
| Working v0.21 or newer installation | [Download and install from the new launcher](docs/INSTALLATION.md#install-future-updates-from-v021-onward) |
| Add your own DLCs | [DLC installation steps](docs/INSTALLATION.md#add-dlcs-to-an-installed-game) |

## Install and play

1. Download **RedBaron_Recomp-v0.23-windows-x64.zip** and use **Extract All**. Choose the larger **offline** ZIP for bundled build tools; both contain the same port. Keep the offline **Tools** folder beside its launcher.
2. Open **RedBaron_Launcher.exe**. In **Install**, select your extracted game folder / `default.xex`, or the compatible original Xbox 360 content package (including packages without an extension).
3. Optionally select your **DLC files** folder. Select a new, empty installation folder and click **Install**. Missing tools are detected and downloaded, or installed from the offline package. Allow the Microsoft installer elevation prompt if requested.
4. Wait for extraction, code generation and compilation. Successful installation removes temporary build files and preserves reusable tools, saves and logs.
5. Choose **Game language**, video settings and controls, then **Save** and **Play**. Epic sign-in enables online features; offline play remains available if authentication is unavailable.

To add DLCs later, select the DLC folder in the installed launcher and click Install. An existing installation receives only the DLCs; the game is not recompiled. DLC files are supplied separately by the player.

See the [full installation guide](docs/INSTALLATION.md) for requirements, controls, updates, saves and logs.

## Updating an existing game

If you already use **v0.21 or v0.22**, close the game, open the installed launcher and select **Update > Check for updates > Download and install update**. The updater downloads v0.23, verifies it, rebuilds and installs the new components, then reopens the launcher. No manual ZIP extraction is needed. Checking alone does not start the download.

The v0.10/v0.20 updater can find and download v0.23 after **Check for updates > Download update**. Close the old launcher, extract the new ZIP into a separate folder and open its launcher. Select **Install > Use existing installation…**, choose your installed game folder (the one containing `game` and `build_cpp_release`), then select **Update > Update selected installation**. Wait for the updater to finish and reopen the installed launcher. Your installed game files, saves, settings and DLCs are kept.

This manual extraction is needed once to move from the old launcher. From v0.21 onward, **Update > Download and install update** prepares and applies future releases automatically after you click it. Checking alone does not start a download. If your original installation failed, use a new empty destination for v0.23 instead. See the [full migration steps](docs/INSTALLATION.md#move-a-working-v010-or-v020-installation-to-v023).

## Features

- English launcher with installation progress, tool detection, optional shortcut and verified automatic updates with previous-version backup and interruption recovery.
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
- Friends/overlay require Epic Account Services authorization. The project owner reports the RedBaron_Recomp brand is now published; successful sign-in by external accounts and the full online flow still need player confirmation. See the [Epic sign-in update](docs/EOS.md#epic-sign-in-update--november-7-2026). Sign-in failures do not prevent offline play.
- Temporal upscaling may show HUD shimmer, ghosting and moving-object artifacts. HUD separation and full object motion vectors remain unfinished. Bilinear, FSR 1 or CAS provide alternatives.
- Leaderboard writes, friend filters, full pagination and weekly rollover need gameplay testing. Weekly periods require service-side administration. Historic Xbox Live scores are not imported.
- Only Windows x64 is supported. Linux, Android and cross-platform play are future work.

The version-display defect does not establish the cause of every reported update-check failure.

Read [Online](docs/EOS.md), [Multiplayer](docs/MULTIPLAYER.md), [Video](docs/VIDEO.md) and [Leaderboards](docs/LEADERBOARDS.md). Report reproducible problems through [Issues](https://github.com/BTr-RJ/RedBaron_Recomp/issues), with version, hardware and a redacted log. Do not attach game files, EOS configuration or authentication data.

## Build and contribute

The installer is recommended for players. Developers can follow [Building from source](docs/BUILDING.md). Generated game sources, assets, installed SDKs, saves and credentials are excluded from Git. SDK changes are maintained as [patches](patches/README.md).

See [Contributing](CONTRIBUTING.md) and [Credits](CREDITS.md). This is an independent fan project, not an official release or endorsement by the original game's rights holders or service vendors.
