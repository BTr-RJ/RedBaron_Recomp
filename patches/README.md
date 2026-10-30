# ReXGlue patches

`scripts/build-video-sdk.ps1` applies these patches to an isolated copy in dependency order. Compatible base: `7236893039fac7e6ce8a21351a430933899f9320`.

| Patch | Purpose |
|---|---|
| rexglue-codegen-repro | Complete codegen fixes and restricted cross-chunk fallback |
| rexglue-spatial-fsr-vsync | Spatial FSR/CAS, VSync and physical window sizing |
| rexglue-fsr315-preview | Optional experimental FSR 3.1.5 |
| rexglue-video-pacing-hdr-dlss | Pacing, HDR10 conversion and optional DLSS |
| rexglue-dlss-modes-hdr-cursor | DLSS presets, HDR reporting and cursor |
| rexglue-local-profiles-lan | Local profiles/network primitives |
| rexglue-online-transport | Transport abstraction |
| rexglue-online-provider | Native provider bridge |
| rexglue-eos-social | Overlay invites and private sessions |
| rexglue-eos-matchmaking | Native Quick Game and compatible filters |
| rexglue-native-leaderboards | Stats/ranking enumerator and provider ABI |
| rexglue-language-selection | Configured XGetLanguage result |
| rexglue-temporal-stability | Optional history/resource and stable sampling fixes |

Filenames use `.patch`. The historical standalone `rexglue-snoopy-fallback-v2.patch` is already included in the complete codegen patch; do not apply both. That fallback is specific to verified addresses/grouping in this title.

The script checks applicability. Preserve original SDK/working binaries and upstream licenses. See [Building](../docs/BUILDING.md), [Video](../docs/VIDEO.md), [EOS](../docs/EOS.md) and [Leaderboards](../docs/LEADERBOARDS.md).
