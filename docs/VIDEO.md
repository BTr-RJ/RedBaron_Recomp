# Video / GPU — 28 October 2026

The launcher and the local runtime now expose spatial FSR 1/CAS, monitor VSync,
internal render scaling, FXAA and anisotropic filtering. This is a development
build, not the completed installer release. Temporal upscaling and DLSS remain
unfinished. Four-player multiplayer and online remain deferred.

## Available settings

| Control | Runtime behavior |
|---|---|
| GPU | Selects the DXGI adapter; Automatic preserves the SDK selection |
| Window resolution | 720p through 8K, plus 3440×1440 and 5120×1440; dimensions are physical pixels, independent of Windows DPI |
| Display mode | Windowed or borderless fullscreen; fullscreen uses the desktop mode |
| Internal render resolution | Uniform 1×–6× scaling of Xenos render targets; nominally 720p through 8K for this title |
| VSync | `present_vsync`: DXGI presentation at sync interval 1 or 0; separate from the guest vblank timer |
| Anisotropic | Game default, off, 1×, 2×, 4×, 8×, 16× |
| Antialiasing | Off, FXAA, FXAA extreme, according to runtime capabilities |
| Upscaling | Bilinear, AMD FSR 1 spatial reconstruction, AMD CAS sharpening |
| FSR 1 sharpness | Soft, Medium, Sharp, Maximum; mapped to RCAS sharpness reduction 2.0, 1.0, 0.2, 0.0 |

Choose an internal resolution below the output resolution to upscale with FSR.
For example, 1× → 1080p or 2× → a 4K desktop. At equal resolution FSR sharpens;
at a larger internal resolution the presenter downsamples. FSR operates on the
final color image, including the HUD. It does not generate frames. FXAA may be
combined with FSR. Larger render scales cost GPU memory and time; 5K/8K and
ultrawide options still need gameplay validation. Ultrawide keeps the original
16:9 image and does not expand the camera's field of view.

Saving preserves ROV, unknown TOML entries and input mappings. The title's video
mode remains 1280×720 while render targets and the host window scale separately.
The existing `vsync` option controls the guest timer; Save keeps it enabled.
Monitor VSync uses the new option and does not change the speed of the guest clock.

## SDK patch and reproduction

`patches/rexglue-spatial-fsr-vsync.patch` applies to the compatible experimental
ReXGlue checkout used by this project. It enables the bundled FSR 1/CAS shaders
without compiling the unfinished temporal FidelityFX backend. The original SDK
checkout is preserved. All generated code and binaries remain local.

```powershell
.\scripts\build-video-sdk.ps1 -SdkSource C:\path\to\compatible-rexglue-source
# Select the printed installed SDK path in Install → Build tools.
# Or pass it to scripts/build.ps1 -SdkRoot <installed-path>.
```

The script creates an isolated source/build/install tree, checks the patch and
builds the SDK. It requires populated SDK submodules, Git, CMake, Ninja, Clang and
the Windows SDK. Patch application accepts CRLF source context. The current
workspace's `sdk_experimental/bin` and `build_cpp_release` contain the tested DLL
pair. Both previous pairs and the previous executable are backed up locally.
The user's existing TOML was left byte-for-byte unchanged during this update.

FSR 1 follows AMD's spatial reconstruction and sharpening pipeline:
[AMD integration documentation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-spatial/).
DXGI synchronization follows Microsoft's `Present` requirements, including no
tearing flag with a nonzero sync interval:
[Microsoft DXGI Present](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present).

## Engine FPS targets

`port/src/video.cpp` overrides the native display interval getter at
`0x8294BB20`, called from `0x8294DC8C`. `redbaron_fps=0` preserves the original
configuration (31 FPS in this game). The launcher offers 30, 60, 90, 120, 144,
165 and 240 FPS; targets above 120 are marked experimental. Save checks the
installed game probe before accepting a target. Virtual refresh follows the
target, with a minimum of 60 Hz. Monitor VSync remains independent.

The engine continues calculating elapsed time using its original clock.
No system clock scalar, audio clock or fixed physics constant is multiplied.
In the tutorial on an RTX 5070 Ti, at internal scale 1 and host VSync off:

| Target | Measured guest frames in flight | Observed simulation timing |
|---|---|---|
| 120 | about 119–120 FPS | approximately 8.33 ms delta; engine seconds follow wall time |
| 240 (experimental) | about 218–234 FPS | delta follows actual frame time; engine seconds follow wall time |

The earlier approximately 34 FPS result was measured during loading, before
the scene became playable. Longer runs and window captures resolved this.
These are local tutorial results, not a guarantee across all missions, GPUs,
render scales, weapons, cutscenes or physics interactions. Keep Game default
available for comparison. The personal TOML is preserved until Save is used.

## Temporal FSR and DLSS: outstanding work

The SDK's experimental temporal upscaler currently supplies color resources in
place of depth and motion. It must not be advertised as FSR 2/3 support. DLSS has
no integration yet. It needs valid scene color, depth, motion, camera constants,
jitter/history handling, capability detection and the NVIDIA integration/runtime:
[NVIDIA DLSS integration guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md).
An RTX GPU alone does not make this implementation available.

## Validation and release status

- SDK runtime/GPU builds, final C++ executable/probe build and existing input CTest passed.
- Native probe reports FSR 1/CAS and the new VSync/physical-window-size flags.
- GPU diagnostics confirm spatial EASU/RCAS passes and CAS execution. ROV renders
  the menu's 3D character and scene. Internal 2× and 3× were exercised; missions
  at every scale have not been validated.
- Launcher self-tests cover 8K serialization, axis override replacement, separate
  host VSync, sharpness conversion, settings roundtrip and unsupported FPS rejection.
- UIAutomation verified centering at 300% Windows scaling, Fullscreen/Restore,
  Save, Close, Play, direct launch and return to settings. Normal/compact captures
  were inspected. F11 toggles fullscreen; Esc exits it unless binding capture is active.
- Local evidence is in ignored `prioridade4_video/`; game screenshots and binaries
  must not enter Git history.

No official Release is published. Before packaging the launcher as the official
installer, finish and validate the agreed video features, package extraction and
tool preparation, and test installation/play on a clean Windows environment.
The installer currently requires extracted game files and an installed compatible
SDK/toolchain. Multiplayer and online are explicitly outside this immediate milestone.
