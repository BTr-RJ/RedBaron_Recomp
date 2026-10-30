# Building from source

This Windows x64 project uses ReXGlue/ReXAuto and locally generated sources. A clone cannot run without compatible original files supplied by the user.

## Prerequisites

- Git, CMake, Ninja, Clang/LLVM, Microsoft C++ Build Tools and Windows SDK.
- ReXGlue source at base commit `7236893039fac7e6ce8a21351a430933899f9320`, with submodules populated.
- Original extracted game files under `game/`, including `game/default.xex`.
- The official EOS C SDK for EOS-enabled builds, obtained under Epic's applicable terms. No product credentials are committed.

## SDK and port

```powershell
.\scripts\build-video-sdk.ps1 -SdkSource C:\path\to\rexglue-source -Fsr315 -Dlss
# Use the installed SDK path printed by the previous command.
.\scripts\build.ps1 -SdkRoot C:\path\to\installed-sdk -EosSdk C:\path\to\EOS-SDK
```

SDK patches are applied in dependency order to an isolated copy. See [patches](../patches/README.md). The port script prepares local game files, generates C++, configures CMake and builds the game, probe and content inspector. Existing binaries are backed up. Keep the runtime/GPU DLL pair from the same SDK build.

Keep `REDBARON_INPUT_FEED=OFF` in production. ROV is required by the default configuration. Existing personal TOML files are preserved.

## Launcher and release packages

```powershell
.\launcher\build.ps1
.\launcher\out\RedBaron_Launcher.exe --self-test C:\temporary\launcher-tests
.\scripts\package-release.ps1 -EosClientConfig C:\private\player-client.ini
.\scripts\package-release.ps1 -EosClientConfig C:\private\player-client.ini -OfflineToolsRoot C:\prepared-tools
```

The launcher uses WPF/.NET Framework 4.8 and its framework compiler. Its installer embeds authored port sources and the prepared SDK, not generated game C++ or assets. Release packaging accepts a local five-field **player GameClient** configuration. Never use TrustedServer credentials, administrative keys or personal authentication caches. The installed lightweight launcher omits the configuration resource after local installation.

See [offline tooling](OFFLINE_INSTALLER.md). Retain dependency licenses/notices.

## Checks

```powershell
cmake --build build_cpp_release --parallel 4
ctest --test-dir build_cpp_release --output-on-failure
python scripts/audit_repository.py --public
```

EOS component tests are configured separately in `port/eos`. Launcher tests cover settings, languages, updates, installation planning and cleanup. Automated checks do not establish end-to-end cloud gameplay or visual quality. Never commit generated sources, extracted metadata, assets, private logs or credentials.
