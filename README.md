# OptiScaler DLSSNR D18

0.1.3a restores the low-FPS F2 fix and bundled DXC reflection dependency.

[中文说明](README_CN.md) · [Download 0.1.3a](https://github.com/kkyleeb21/OptiScaler_DLSSNR/releases/tag/dlssnr-d18-v0.1.3) · [Installer guide](community/d18-installer/README.md)

## D18 0.1.3a

The unified installer adds RE Engine support with REFramework. It selects a matched build only from [REFramework-nightly](https://github.com/praydog/REFramework-nightly), with latest-nightly or manual-install fallback. REF menu: PgDn; D18: Insert. One game-root configuration. See [0.1.3a release notes](community/d18-installer/RELEASE_NOTES_0.1.3a.md).

## Introduction

D18 is a community fork of [OptiScaler DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR).
It runs NVIDIA Neural Rendering (Feature 18) after DLSS SR, with an adjustable internal network
resolution to reduce GPU cost. The SR image and final output stay at display resolution.

At 4K output, **50% means half the width and half the height**: a 1920×1080 network with
3840×2160 Color/Output. Lower ratios trade model detail and stability for speed; they do not change
the game's DLSS SR quality setting.

D18 internal scaling and the new diagnostics/reconstruction controls target DX12. GPU support depends
on the supplied 310.8 Runtime and driver.

## Features and controls

The tables describe the **common 0.1.3a D18 UI**. Older and game-specific packages may expose different
controls. Click **Save Settings** to persist changes in `OptiScaler.ini`. Changes to network ratio,
Runtime samplers or model tuning rebuild Feature 18 and reset its history; a brief pause is possible.

### Neural Rendering

| Option | What it changes |
| --- | --- |
| Enable Neural Rendering | Toggles the NR pass; does not toggle DLSS SR or FG. |
| Internal network scaling | Runs a smaller internal network while retaining full-resolution Color/Output. Off uses the full network; legacy `WorkingScale` is ignored in this DX12 path. |
| Network ratio / 50%, 66.7%, 75%, 100% | Scales both network axes, aligned to the Runtime's 16×8 grid. Range: 0.5–1.0. |
| Detail strength | Amount of the NR edit transferred during composition. 0 bypasses the edit; higher values strengthen it, not necessarily real detail. |
| Colour strength | Colour contribution: 0 retains the game's hue while allowing brightness edits; 1 transfers model colour. |
| Preserve original high frequencies | Retains fine SR detail while transferring lower-frequency NR changes at reduced ratios. |
| Motion-adaptive low-frequency transfer | Attenuates NR transfer where motion and low-frequency mismatch coincide. It is not temporal accumulation. |
| Motion protection starts / reaches full | Pixel-motion thresholds over which motion protection ramps up. |
| Mismatch protection starts / reaches full | Low-frequency difference thresholds over which mismatch protection ramps up. |
| Linear network output sampling | Switches the Runtime's output reconstruction from POINT to LINEAR. Can smooth blocks but also soften detail. |
| Linear model Color input | Switches Runtime Color input sampling to LINEAR; overridden by the custom prefilter. |
| Custom model Color prefilter | Prepares a phase-aligned Mitchell–Netravali input on the network grid, with a ringing clamp. Requires internal scaling; forces Runtime Color sampling to POINT. |
| Retry NR | Retries after failure. Read the displayed reason/log first; retrying does not repair an incompatible Runtime. |

<details>
<summary>Model tuning</summary>

These are undocumented Runtime parameters. The descriptions identify their intended role, not a
guaranteed visual effect. They are separate from the final composition sliders above.

| Option | What it changes |
| --- | --- |
| Model preset | Sends `DLSSNR.Hint.Render.Preset`. The inspected 310.8 build resolves the exposed presets to one weight set; these are not SR presets or measured speed tiers. |
| NR style | Runtime profile: 0 Standard, 1 Natural, 2 Cinematic. Names are community labels; a style can also affect post-process routing. |
| Intensity | Runtime NR strength, distinct from Detail strength. It can change the internal processing path. |
| Local structure | Runtime local-structure strength. |
| Local tone | Runtime local-tone strength. |
| Skin structure | Skin-specific structure strength; -1 follows Local structure. |
| Auto skin mask | Requests automatic skin masking from the Runtime. |

</details>

<details>
<summary>Optional low-ratio experiments</summary>

These options default to **Off**. Original D18 composition and the existing RCAS/DA sharpening route
are retained. The new frequency/guided transfer requires DX12, internal ratio below 1,
**Experimental low-ratio compose** and **Preserve original high frequencies**.

| Option | What it changes / dependency |
| --- | --- |
| Experimental low-ratio compose | Enables the experimental composition paths, including ratio-aware RGB frequency separation. |
| Enlargement: Classic / Matched residual | Selects the composition input: Classic uses the sampled model image; Matched residual transfers its edit onto the full-resolution proxy. Enabled for experimental reduced-ratio operation. |
| Guided network reconstruction | Uses full-resolution SR luminance to guide reconstruction between network cells and limit transfer across edges. |
| Area + gain-first reconstruction (50% A/B) | Averages 2×2 footprints and computes cell gains before interpolation. Requires guided reconstruction and exactly 50% network ratio. |
| Frequency radius | Low-pass radius in network pixels; moves the SR/model band split in the experimental low-pass path. Does not tune the guided path. |
| Luma trust / Chroma trust | Separately weights the model's brightness/colour edits in experimental composition. Greater trust can amplify instability. |
| Catmull-Rom input kernel (A/B) | Replaces Mitchell in the custom input prefilter. Requires internal scaling and Custom model Color prefilter; independent of the experimental compose switch. |

</details>

<details>
<summary>Exposure, comparison and diagnostics</summary>

| Option | What it changes |
| --- | --- |
| Use game exposure | Uses the game's available exposure information to normalize linear Color input. |
| Paper white / Paper white (x exposure) | Adjusts input white-point scaling; with game exposure enabled it multiplies the exposure-derived value. Can change the resulting image, not just the preview. |
| Highlight guard | Bounds relative luminance changes, including brightening and darkening in the current composition. |
| Debug view | Off, input Proxy, Raw model output, or Difference ×20. Proxy/model views are Runtime-visible images, not hidden network tensors. |
| Compare | Off, Side by side, or Wipe between SR-before and composed-after. |
| Swap sides / Label the sides / Label size | Changes comparison placement and labels. |
| Zoom / Wipe split | Side-by-side zoom / wipe boundary position. |
| Diagnostic mode | Off: no diagnostic ring writes. Summary: lifecycle/failures/skips. Trace: also per-frame contracts and processing events. |
| Capture 8 frames | Saves SR-before, composed-after, model-input and model-output under `dlssnr-capture` beside the proxy. Requires observed DX12 submission and GPU fence completion. |

Diagnostics use a bounded local `D18Diagnostics.ring` beside the game executable; no automatic upload
or image capture. Explicit captures consume readback memory and overwrite the previous batch.
Keep scene and settings fixed during a batch. See [analysis tools and event semantics](community/d18-installer/COMMON_FEATURES.md#diagnostics-and-capture--诊断与捕获).

</details>

<details>
<summary>DLSS SR, FG, sharpening and UI</summary>

| Option | What it changes |
| --- | --- |
| Use native NVIDIA DLSS SR | Enables OptiScaler's native DLSS provider. The game still supplies quality and resolution unless overridden. |
| Override render preset / Preset / Apply SR | Selects and applies a DLSS SR preset by rebuilding the backend. Separate from NR Model preset. |
| Enable OptiScaler FG route | Toggles an available OptiScaler-managed FG route. Game-native FG may remain controlled only by game settings. |
| MFG | Selects the multiplier supported by the active FG provider. |
| Provider debug overlay / Nukem debug view | Shows provider-specific FG diagnostics where supported. |
| Override game sharpness / Sharpness | Uses the selected strength instead of the game's value. |
| Enable OptiScaler sharpening (RCAS/DA) | Enables the existing post-upscale sharpener; this is not the Onimusha-specific integrated post-NR sharpener. |
| RCAS / Depth Aware (RCAS) / Depth Aware (DAS) | Selects contrast-adaptive, depth-aware RCAS, or depth-aware directional luma sharpening. |
| Contrast control / Contrast | Enables and adjusts the RCAS contrast extension. |
| Clamp output | Limits depth-aware sharpening output. |
| Depth bias / Depth scale / Reset depth values | Calibrates depth-edge sensitivity; Reset restores automatic values. |
| Enable Motion Adaptive Sharpness | Modulates sharpening with motion. |
| Motion sharpness / Motion threshold / Motion range | Maximum strength adjustment, activation threshold and ramp range. Negative strength reduces sharpening in motion. |
| MAS debug view / DA + MAS debug view | Visualizes motion/depth-aware sharpening behavior. |
| UI hotkey / NR hotkey | Separate menu and NR toggles. Defaults: Insert / unbound. Click then press a single key; Escape cancels, Backspace unbinds, R restores default. |
| UI scale / Auto scale | Manual / automatic menu scaling. Resize by dragging the lower-right corner; panels support scrolling. |
| Save Settings / Close | Saves configuration / closes the menu without disabling NR. |

Runtime-status cards show observed SR/FG/NR activity. **UNOBSERVED** means no reliable state was
observed, not that the feature is off. A recorded composition event alone does not prove GPU completion.

</details>

## Differences from the original

Comparison is against the **OptiScaler DLSSNR baseline used for D18**, not every later upstream release.

| Area | Baseline | D18 |
| --- | --- | --- |
| Lower-cost NR | Physical `WorkingScale` staging | Independent 2D internal network scaling; Color/Output remain full-resolution |
| Sampling | Existing Runtime input/output path | Independent POINT/LINEAR controls and optional grid-aligned input prefilter |
| Composition | Existing NR composition | Adds SR high-frequency preservation, motion/mismatch protection and optional reconstruction experiments |
| UI and diagnosis | General OptiScaler controls | Focused SR/FG/NR panel, editable hotkeys, event diagnostics and four-stream capture |
| Installation | Upstream setup workflow | Guarded Runtime patching, file manifests, backups, managed upgrades and uninstall |

D18 changes integration, sampling and composition; it does not ship newly trained weights.
Credits: [OptiScaler](https://github.com/optiscaler/OptiScaler),
[Dagherbou / OptiScaler DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR),
and [RenoDX](https://github.com/clshortfuse/renodx) for the underlying colour-composition work.
Code is [GPL-3.0](LICENSE); [third-party notices](community/d18-installer/THIRD_PARTY_NOTICES.md) accompany the package.

## Install, update and uninstall

1. Close the game and extract the complete [D18 0.1.3a ZIP](https://github.com/kkyleeb21/OptiScaler_DLSSNR/releases/tag/dlssnr-d18-v0.1.3).
2. Supply a **GPU/driver-compatible 310.8-based `nvngx_dlssnr.dll`** beside the game executable.
   It is not included. Do not confuse it with the included `nvngx.dll_dlssnr.dll` forwarder.
   Files with descriptive download names must be renamed.
3. Run `Install-D18.bat`, select the executable directory and proxy name (common default: `dxgi.dll`).
   Fresh installs ask for the UI key; Enter keeps Insert. Alternatively select a Runtime when prompted.
4. Start the game, enable its DLSS SR path, open D18 and check **NR Active**.
   Use 100% as a quality reference, then compare lower ratios. The common package ships 50% with
   the custom prefilter; that is a performance starting point, not a universal quality recommendation.

**Onimusha:** use its separate installer with the required Onimusha REFramework build. It manages
`d3d12.dll` and the `_storage_` mirror; do not substitute the common proxy/profile.
Follow that package's README for prerequisites.

**Update:** rerun the installer. Managed replacement validates the new files first, retains backups and
preserves the existing INI in full. Upgrading therefore does not reset personal settings.

**Uninstall:** close the game, run `Uninstall-D18.bat` and select the same directory.
It restores backed-up files and removes its own additions. Files modified after installation are
preserved separately. Keep `D18_Backups` and `.dlssnr-d18-install.json` until uninstall completes.

**Runtime checks:** `VERIFIED` identifies a reference file; `UNVERIFIED_COMPATIBLE` means its patch
locations passed checks; `ALREADY_PATCHED` means the required changes are present; `CONFLICT` stops
patching. GPU/driver compatibility is separate from patch compatibility. D18 patches preserve
compatible unrelated modifications.

For `nvngx_dlssnr.dll was not found`, check filename/location. For `the model would not initialise`,
provide GPU, driver, Runtime hash and the `DLSS-NR create failed: init ... create ...` log line.
[Manual installation and recovery details](community/d18-installer/README.md).

## Findings

These findings describe the inspected 310.8 Runtime and our tested D18 paths, not all drivers or
implementations.

- **Weights:** the inspected preset registry contains one `WEIGHTS_HT` entry; other exposed presets
  fall back to preset 1. We found no separate speed/quality networks through preset selection.
- **Cost:** a controlled Cyberpunk test attributed about 7.48 ms of a 7.50 ms NR GPU-busy increase to
  Feature 18. Reducing the actual internal network became the optimization target. The ratio alone
  does not predict end-to-end speedup.
- **Routing and dimensions:** `Style`/`Intensity` can change native post-processing branches.
  Active dimensions, history/scratch addressing and dispatch must agree. Full-resolution external
  resources can coexist with a smaller internal network.
- **Reconstruction:** point enlargement can expose repeated network cells. Frequency-separated
  composition preserves SR detail, but interpolation or a sharper input kernel alone did not close
  the reduced-ratio quality gap.
- **Quality:** 50% tests exposed motion softness in Elden Ring vegetation and face/material-detail and
  colour/tone differences in Onimusha. Guided/gain-first and input-kernel experiments showed trade-offs;
  they remain optional. Higher frequency energy does not prove recovered texture or correct colour.
- **Evidence limits:** four-stream capture separates composition from the Runtime-visible model
  boundary. It cannot isolate hidden inference from Runtime resampling. A near-unity energy ratio,
  API success or an unobserved queue is insufficient evidence of visual correctness or GPU completion.
