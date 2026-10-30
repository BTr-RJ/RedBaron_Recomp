# Video / GPU — 29 October 2026

The launcher and the local runtime now expose spatial FSR 1/CAS, monitor VSync,
internal render scaling, FXAA and anisotropic filtering. This is a development
build, not the completed installer release. FSR 3.1.5 Super Resolution is now
available as an opt-in experimental preview. NVIDIA DLSS Super Resolution is also available as an experimental preview. HDR10 output and an FPS counter are implemented. Four-player multiplayer and online remain deferred.

## Available settings

| Control | Runtime behavior |
|---|---|
| GPU | Selects the DXGI adapter; Automatic preserves the SDK selection |
| Window resolution | 720p through 8K, plus 3440×1440 and 5120×1440; dimensions are physical pixels, independent of Windows DPI |
| Display mode | Windowed or borderless fullscreen; fullscreen uses the desktop mode |
| Internal render resolution | Uniform 1×–6× scaling of Xenos render targets; nominally 720p through 8K for this title |
| VSync | `present_vsync`: DXGI presentation with maximum frame latency 1 and integer refresh divisors where possible; separate from the guest timer |
| Anisotropic | Game default, off, 1×, 2×, 4×, 8×, 16× |
| Antialiasing | Off, FXAA, FXAA extreme, according to runtime capabilities |
| Upscaling | Bilinear, AMD FSR 1, AMD CAS, experimental AMD FSR 3.1.5 and NVIDIA DLSS Super Resolution |
| FPS counter | Actual game-frame FPS and average milliseconds, upper-right corner |
| HDR output | HDR10 when Windows HDR is active on the game monitor; SDR fallback otherwise |
| FSR sharpness | Soft, Medium, Sharp, Maximum; mapped to RCAS sharpness reduction 2.0, 1.0, 0.2, 0.0 |

Choose an internal resolution below the output resolution to upscale with FSR.
For example, 1× → 1080p or 2× → a 4K desktop. At equal resolution FSR 1 sharpens and FSR 3.1.5 performs temporal antialiasing;
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
without requiring an external temporal FidelityFX provider. The original SDK
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

The previous implementation only changed the display deadline. Its general
engine clock tracked real time, but **PhysX still simulated 1/30 second each
game frame**. The previous clock-only measurements did not establish correct
gameplay speed.

The correction captures the actual pre-update delta at `0x82976FA8` and applies
it to the PhysX calls made by `0x8281D6C8`. The fixed-step solver uses equal
substeps no longer than 1/60 second. Only those identified call sites change;
system/audio clocks are unchanged. Game default installs no physics overrides.

At 120 FPS, the tutorial measured **601 physics updates and 5.008 simulation
seconds over 5.008 wall seconds**. The scene and HUD timer advanced during the
run. Other missions, cutscenes, physics interactions and long sessions still
need gameplay validation. The personal configuration is unchanged until Save.

Additional tutorial checks after the correction:

| Target | Observed game FPS | Physics / real time (steady sample) |
|---|---|---|
| 60, VSync on | about 60 | 5.017 s / 5.016 s |
| 120 | about 119–120 | 5.008 s / 5.008 s |
| 240, VSync off (experimental) | about 218–233 | 5.009 s / 5.004 s |

These are local measurements on the RTX 5070 Ti, not guarantees for all scenes.

## Presentation, FPS counter and HDR

`patches/rexglue-video-pacing-hdr-dlss.patch` applies after the spatial and
FSR 3.1.5 patches. DXGI uses a waitable flip chain with maximum frame latency 1.
The presenter waits before choosing the newest color mailbox. VSync never uses
tearing flags. Targets that divide monitor refresh use integer sync intervals,
up to four refreshes. On a 120 Hz display, start with 60 or 120 FPS. Unmatched
targets can still have uneven displayed cadence. These changes limit queued
work; end-to-end input latency has not been instrumented.

**Show in-game FPS counter** (`redbaron_show_fps`) displays actual game update
frames and average milliseconds, not repeated UI presentations. It does not
intercept game input.

**Use HDR output when available** (`present_hdr`) selects a 10-bit Rec.2020/PQ
swap chain when Windows HDR is active on the monitor containing the game.
Original SDR content and overlays are composed first, then converted to HDR10
with **200-nit SDR white**. This does not recover native HDR lighting/highlights
from the title. Inactive/unsupported displays use SDR; enable Windows HDR before
starting. The launcher reports connected displays; runtime checks the actual
game monitor. Local logs confirmed HDR10 with VSync at 120 Hz. TV appearance
and calibration need user testing; ordinary SDR captures cannot assess HDR.
See [Microsoft HDR guidance](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range).

## FSR 3.1.5 Super Resolution preview

`patches/rexglue-fsr315-preview.patch` applies after the spatial patch. It uses
the official signed DirectX 12 upscaler from FidelityFX SDK 2.0.0 and explicitly
selects provider version **3.1.5**. This is temporal Super Resolution, not frame
generation. AMD's release and upstream license are available at
[FidelityFX SDK 2.0.0](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.0.0).

```powershell
.\scripts\build-video-sdk.ps1 -SdkSource C:\path\to\compatible-rexglue-source -Fsr315
# Optional: -FidelityFxArchive C:\downloads\FidelityFX-SDK-v2.0.0.zip
```

The dependency script verifies the pinned archive SHA-256 before extraction.
The build copies `amd_fidelityfx_upscaler_dx12.dll` and `FidelityFX-LICENSE.txt`
into the installed SDK. Game builds stage both alongside the executable/probe.
Keep these files together with the runtime/GPU DLL pair. SDK archives and
third-party binaries stay local and are not committed to this repository.

In Video, select **AMD FSR 3.1.5 · experimental**, choose an internal resolution
below the output resolution, then Save/Play. Start with internal **2×**, a **4K**
output and **120 FPS**. The launcher's explicit render scale controls input size;
Save clears older forced FSR quality presets. Spatial FSR remains available.
The personal configuration is not changed by this update until Save is used.

The preview captures the main ROV D24FS8 depth buffer before post-processing
reuses EDRAM. A compute pass decodes the tiled depth and reconstructs camera
motion from current/previous view-projection matrices. Projection jitter,
frame timing and depth travel with the matching color mailbox. History survives
normal frames and resets for size changes, gaps and first use. A bounded buffer
pool and presentation fences protect snapshot and descriptor lifetimes.
Initialization/dispatch failure disables further jitter and falls back to spatial
FSR for that session. Menus without captured 3D depth also use the spatial path.

**Remaining limitations:** scene detection and camera constants are title-specific
and tested in the tutorial only. Object/skinning velocities are not yet captured;
near-foreground reactivity reduces, but does not eliminate, ghosting. The HUD is
still part of the input color, and can leave trails or look softer. Scene changes,
other missions, other GPUs and long sessions require further validation. This
preview must not be described as the completed production integration.

Local tests on an RTX 5070 Ti reached approximately **119–120 guest FPS** with
FSR 3.1.5 at **2560×1440 → 3840×2160** and a 120 FPS target in the tutorial.
Logs confirm the 3.1.5 provider, actual dispatches and preserved temporal history;
window captures confirm the 3D scene. 1280×720 → 1920×1080 was also exercised.
Windows could not enable the D3D12 debug layer, so these runs are runtime/visual
checks, not a clean GPU validation-layer report. Results are not guarantees for
all gameplay or hardware. Frame generation is not implemented.

## NVIDIA DLSS Super Resolution preview

The NGX initialization/capability/create/evaluate flow was studied in
[Estacado The Darkness Recomp](https://github.com/invinceble55-wq/Estacado-The-Darkness-Recomp)
and its [Estacado-ReXGlue runtime](https://github.com/invinceble55-wq/Estacado-ReXGlue).
RedBaron supplies its own title-specific depth, jitter and camera-motion capture;
the other title's addresses and buffers cannot be reused directly.

Build with `scripts/build-video-sdk.ps1 -SdkSource <source> -Fsr315 -Dlss`.
`-Dlss` implies `-Fsr315`; optionally provide `-DlssSdk <local-pinned-checkout>`.
`prepare-dlss.ps1` obtains official NVIDIA DLSS **310.9.1** at commit
`374959484e79a640feaba44c93ac8cfb0a03f5b5` and checks the release DLL SHA-256.
Headers/libraries and binaries stay local. Keep `nvngx_dlss.dll` and
`NVIDIA-DLSS-LICENSE.txt` with the runtime/GPU DLL pair.
The [NVIDIA SDK license](https://github.com/NVIDIA/DLSS/blob/374959484e79a640feaba44c93ac8cfb0a03f5b5/LICENSE.txt)
applies separately; review its distribution conditions before a future release.

The native probe asks NGX about each adapter/driver. The launcher enables DLSS
only for a compatible selected GPU. Runtime failure falls back to spatial FSR.
The launcher offers DLAA (native), Quality, Balanced, Performance and Ultra
Performance, mapped to explicit NGX modes. Saving selects an integer render
scale from the output dimensions: at 4K the scales are 3, 2, 2, 2 and 1.
Balanced and Performance therefore share the 1440p scene-render cost at 4K,
though NGX input dimensions differ. This avoids rendering a full 4K scene
before upscaling in Quality mode. A tutorial run measured 120.03–120.10 FPS
at 2560×1440 → 3840×2160 with a 120 FPS target on the RTX 5070 Ti; this is
not a controlled comparison with the user’s reported 85 FPS in another scene.

Local tests confirmed successful initialization at **1280×720 → 1920×1080**,
thousands of evaluated frames and the 3D tutorial at a 120 FPS target. DLSS also
ran with HDR output and VSync. **Object/skinning velocities and separate HUD
composition remain incomplete**, so visible trails/ghosting remain. This is
an experimental integration, not production-quality completion. Spatial FSR 1
or bilinear remain available. No FSR/DLSS frame generation.

## Validation and release status

- The five SDK patches reproduce all modified SDK sources from the original checkout.
- UIAutomation saved DLSS, 120 FPS, VSync, FPS counter and HDR and captured normal/fullscreen layouts.
- SDK runtime/GPU builds, final C++ executable/probe build and existing input CTest passed.
- Native probe reports spatial FSR/CAS, the FSR 3.1.5/DLSS previews, FPS/HDR flags and actual NGX/display capabilities.
- GPU diagnostics confirm spatial EASU/RCAS passes and CAS execution. ROV renders
  the menu's 3D character and scene. Internal 2× and 3× were exercised; missions
  at every scale have not been validated.
- Launcher self-tests cover 8K serialization, axis override replacement, separate
  host VSync, sharpness conversion, settings roundtrip, unsupported FPS rejection, FPS/HDR persistence and DLSS adapter gating.
- UIAutomation verified centering at 300% Windows scaling, Fullscreen/Restore,
  Save, Close, Play, direct launch and return to settings. Normal/compact captures
  were inspected. F11 toggles fullscreen; Esc exits it unless binding capture is active.
- Local evidence is in ignored `prioridade4_video/`; game screenshots and binaries
  must not enter Git history.

No official Release is published. Before packaging the launcher as the official
installer, finish and validate the agreed video features, package extraction and
tool preparation, and test installation/play on a clean Windows environment.
The installer currently requires extracted game files and an installed compatible
SDK/toolchain. Online and local multiplayer have become the current priority; see MULTIPLAYER.md.

The FPS overlay now includes the actual HDR10/SDR output state, taken from
successful swap-chain color-space negotiation. The preference alone does not
prove HDR is active. It remains a conversion of SDR content, not native HDR
lighting. The game cursor is hidden in fullscreen and restored in windowed mode.

## Stability changes (2026-10-30)

The startup splash skip now uses a strong guest entry point so direct generated
calls cannot bypass it. It advances the image index past the sequence before
the original update/draw logic runs, preserving loading completion. The probe
does not link this guest-only entry point.

`rexglue-temporal-stability.patch` removes the depth-only DLSS foreground bias
mask, handles repeated guest frames whose presentation output resource changed,
and resets temporal history for backwards timestamps/frame IDs. Jitter amplitude
also accounts for internal resolution scale.

DLSS uses stable scene sampling by default (`present_dlss_scene_jitter=false`).
The previous path jittered 3D while passing an already composed, unjittered HUD
through the same NGX evaluation. Disabling that conflicting jitter is a stability
workaround, with less subpixel reconstruction detail. This is not full HUD
separation or dense object velocity support; motion trails and residual shimmer
remain possible. The advanced jitter flag requires restart and remains experimental.

A 24-frame static menu-text comparison at identical settings showed adjacent RGB
variation of 0.470 before / 0.440 after on a 431×83 crop. This small local result
does not establish flicker elimination throughout gameplay or a performance gain.
Real NGX evaluation, menu rendering and EOS overlay continued working after the
change. The personal graphics TOML was preserved exactly against the turn's backup.

A startup capture sampled 24 frames from the first game window through the menu
transition. The broken Xbox Arcade/custom image did not appear. The separate
original movie logos and loading notice still play.
