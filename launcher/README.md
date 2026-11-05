# RedBaron launcher

A Windows WPF/.NET Framework 4.8 launcher/installer with an English interface.

- **Install:** game folder/XEX/content package, optional DLC folder, unified action, tool detection, progress, cleanup and shortcut. Existing installations accept DLC-only additions.
- **Video:** GPU, resolution, display mode, scale, FPS, VSync, anisotropic/FXAA, FSR/DLSS, HDR and FPS counter.
- **Controls:** bindings and assignments for four configurable profiles with a keyboard slot. Two local cameras are supported.
- **Online:** player name. EOS/Epic is automatic; no room-server switches.
- **Update:** published test/stable release checks, SHA-256 verified downloads and logs. The v0.21 launcher builds updates in isolation, replaces managed runtime files with a persistent rollback journal and reopens. Existing game files, DLCs, saves and settings are preserved. Full installers also offer **Update selected installation** for a one-time migration; v0.10/v0.20 themselves still download ZIPs only.
- **Footer:** game language, Save/Play and the option to show the launcher next time. Hold Shift or pass `--settings` to return.

See [Installation](../docs/INSTALLATION.md) and [Building](../docs/BUILDING.md). Full installers embed authored source/SDK payloads; installed lightweight launchers omit them. Original assets, generated C++ and personal login caches are not embedded.
