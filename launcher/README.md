# RedBaron launcher

A Windows WPF/.NET Framework 4.8 launcher/installer with an English interface.

- **Install:** game folder/XEX/content package, optional DLC folder, unified action, tool detection, progress, cleanup and shortcut. Existing installations accept DLC-only additions.
- **Video:** GPU, resolution, display mode, scale, FPS, VSync, anisotropic/FXAA, FSR/DLSS, HDR and FPS counter.
- **Controls:** bindings and assignments for four configurable profiles with a keyboard slot. Two local cameras are supported.
- **Online:** player name. EOS/Epic is automatic; no room-server switches.
- **Update:** published test/stable release checks, verified downloads and logs. v0.10 downloads installers; it does not upgrade in place.
- **Footer:** game language, Save/Play and the option to show the launcher next time. Hold Shift or pass `--settings` to return.

See [Installation](../docs/INSTALLATION.md) and [Building](../docs/BUILDING.md). Full installers embed authored source/SDK payloads; installed lightweight launchers omit them. Original assets, generated C++ and personal login caches are not embedded.
