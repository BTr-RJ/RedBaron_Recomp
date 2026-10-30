# Offline build tools package

Both installers contain the same port/runtime. The standard ZIP detects tools and downloads missing dependencies. The larger offline ZIP includes LLVM, CMake, Ninja and a Microsoft Build Tools layout. Both require the player's original files; EOS needs Internet access.

Extract the complete offline ZIP with its adjacent `Tools` folder. Follow [Installation](INSTALLATION.md). Existing usable tools are preferred. Microsoft installation can request elevation/restart. Successful cleanup retains reusable tools.

## Maintainer preparation

```powershell
.\scripts\prepare-offline-tools.ps1 -PortableRoot C:\portable-tools -BuildToolsLayout C:\ms-layout -Destination C:\prepared-tools
.\scripts\package-release.ps1 -EosClientConfig C:\private\player-client.ini -OfflineToolsRoot C:\prepared-tools
```

Preparation checks required binaries, Microsoft signature, SDK/compiler components and hashes. Packaging rechecks the manifest and rejects unexpected files/links. Retain licenses and the Microsoft layout. Never include game content, generated sources, authentication caches or administrative credentials.
