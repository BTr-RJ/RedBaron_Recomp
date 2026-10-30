# Offline build tools

## Official release variants

User decision: publish both variants of each official Windows release. The default
is the smaller installer, `RedBaron_Recomp-v<version>-windows-x64.zip`; it detects
compatible installed tools and downloads only missing components. Offer
`RedBaron_Recomp-v<version>-windows-x64-offline.zip` as the larger alternative with
the same launcher plus all tool payloads. Both use the same inventory and game/DLC
installation workflow. The updater selects the smaller standard asset by default.
These names describe the future official release; current EOS test packages remain
private and retain their private-test filenames.

An offline package contains the launcher and its `Tools` directory in one ZIP.
Extract the entire ZIP and run the launcher. No separate tool download is needed
for the included build components. EOS multiplayer still requires Internet.

The Install tab checks versions on startup and exposes **Check installed tools**
inside **Build tools**. The same inventory is used by the installer. Compatible
CMake, Ninja, Clang, Visual C++ and Windows SDK components are reused; only missing
or outdated components are installed. Cached tool paths are checked again, not
assumed to exist based on a previous installation marker.

Portable binaries are checked against a SHA-256 manifest embedded in the launcher
and copied into `.redbaron-tools` when needed. The Microsoft offline layout is
checked before execution, and its signed bootstrap installs only the missing
component IDs with `--noweb --noUpdateInstaller`. Both tools and normal Windows
administrator approval may still be needed on a new PC. Compatible existing tools
skip payload verification/copying for components that will not be used.

## Preparing a package

Use the official Microsoft Build Tools bootstrap to create a layout containing
`Microsoft.VisualStudio.Component.VC.Tools.x86.x64` and
`Microsoft.VisualStudio.Component.Windows11SDK.26100`, in English. For example:

```powershell
vs_BuildTools.exe --layout C:\RBTools\vs --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --lang en-US --passive --wait --norestart
vs_BuildTools.exe --layout C:\RBTools\vs --verify --passive --wait --norestart
```

The portable tree contains `llvm/bin/clang++.exe`, `llvm/bin/lld-link.exe`,
`llvm/lib/clang`, `cmake/bin/cmake.exe`, CMake's versioned `share` Modules,
Templates and Licenses directories, `ninja/ninja.exe`, and license notices under
`licenses`. Keep the compiler's resource directory and CMake's directory layout.

```powershell
scripts/prepare-offline-tools.ps1 -PortableRoot C:\RBTools\portable -BuildToolsLayout C:\RBTools\vs -Destination C:\RBTools\prepared
scripts/package-private-test.ps1 -EosConfig <local-configuration> -OutputDirectory <ignored-private-output> -OfflineToolsRoot C:\RBTools\prepared
```

The builder copies only manifest-listed tool files. It does not include game
assets. The private package embeds the separately supplied local EOS configuration
and must remain private. For a general launcher build, `launcher/build.ps1` accepts
`-OfflineToolsRoot` without requiring any private EOS configuration.

## Validation

- Existing compatible tools were identified and reused without copying payloads.
- With Clang/CMake/Ninja removed from the process PATH and no cache catalog, the
  portable payload supplied them without downloads. C++ and Windows resource
  compilation succeeded.
- The game was generated and compiled from the STFS folder with the portable
  Clang, CMake and Ninja paths; the full installation and EOS provisioning passed.
- The official Microsoft layout verification returned zero. Installing Microsoft
  components on a blank Windows 11 machine remains part of the user's PC test.

Official references: [offline layouts](https://learn.microsoft.com/en-us/visualstudio/install/create-an-offline-installation-of-visual-studio?view=vs-2022),
[installer switches](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=visualstudio),
[Build Tools component IDs](https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=vs-2022).
