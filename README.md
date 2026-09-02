# OptiScaler DLSSNR D18

> Experimental internal-network-scaling research build based on OptiScaler DLSSNR.

[中文说明](README_CN.md) · [Community installer](community/d18-installer/README.md)

D18 keeps the game Color contract and final Output at display resolution while running NVIDIA DLSS
Neural Rendering (Feature 18) on an independently sized two-dimensional network lattice. Its purpose
is to reduce the cost of the NR model without inheriting the physical downsample/upscale blur and
colour errors of the original `WorkingScale` path.

At a 3840×2160 output and a Network Ratio of `0.5`:

```text
Game Color / final Output: 3840×2160
DLSSNR network lattice:    1920×1080
Depth / motion vectors:    the game's original guide contract
```

D18 is an experimental community build. It is not an official NVIDIA, OptiScaler or
OptiScaler_DLSSNR release.

## D18 features

### Full-resolution internal network scaling

- Keeps Color and final Output at display resolution.
- Provides a continuous `0.5–1.0` Network Ratio and 50%, 66.7%, 75% and 100% presets.
- Calculates width and height independently and aligns them to the Runtime's 16×8 network grid.
- Rebuilds Feature 18 when the ratio or sampling contract changes.
- Logs effective Output, Network, Guides, Ratio and sampler state.
- Retains the original physical `WorkingScale` path for direct A/B comparison.

### Phase-aligned Mitchell Color prefilter

The new `DlssNrMode_ColorPrefilter` shader pass constructs a Mitchell–Netravali Color surrogate on
the internal network grid. It replaces the Runtime's point-sampled input with a phase-correct,
anti-aliased source, while a local range clamp limits ringing. When enabled it overrides Linear Color
Input so two low-pass filters are never stacked.

### Frequency-aware composition

The reduced network supplies the broad luminance decision while the full-resolution original can
retain its real high-frequency detail. Colour transfer remains independent. An optional motion and
low-frequency mismatch gate is available for per-game experiments and is disabled in the baseline.

### Runtime sampling laboratory

D18 exposes independent POINT/LINEAR A/B controls for the network answer and model Color input.
The Forwarder verifies exact instruction signatures and changes only the loaded Runtime image in
memory. Unknown layouts are rejected rather than patched blindly.

```cpp
dlssnr_call_set_sampler_modes(int linearResolve, int linearColorInput);
dlssnr_call_create(..., float scalingRatio);
dlssnr_call_evaluate(..., float scalingRatio);
```

### OptiScaler UI and configuration

```text
Internal network scaling
Network ratio
50% / 66.7% / 75% / 100%
Detail strength
Colour strength
Preserve original high frequencies
Motion-adaptive low-frequency transfer
Linear network output sampling
Linear model Color input
Custom Mitchell model Color prefilter
```

All controls are available in the OptiScaler overlay and are persisted in `OptiScaler.ini`.

## Differences from the original OptiScaler DLSSNR branch

| Area | Original branch | D18 |
|---|---|---|
| Reduced-cost NR | Physical `WorkingScale` | Adds full-contract Internal Network Scaling |
| Color / Output | Shrink with `WorkingScale` | Stay at display resolution in Internal mode |
| Network ratio | No independent effective data flow | Adjustable 0.5–1.0 exact 2D lattice |
| Model Color input | Runtime sampling | Optional phase-aligned Mitchell reconstruction |
| Network answer | Fixed Runtime POINT path | Independent POINT/LINEAR A/B control |
| High frequencies | Enlarged with the model result | Can be retained from the full-resolution original |
| Motion protection | None | Optional motion/mismatch-aware LF transfer |
| Runtime acceptance | Exact official-file allowlist | Layout-guarded official and community 310.8 variants |
| Deployment | Manual | Auditable install, backup, rollback and uninstall tools |

## Community installer

The source-visible installer lives in [`community/d18-installer`](community/d18-installer). It does
not contain, redistribute or download `nvngx_dlssnr.dll`. Users supply their own 310.8-based Runtime,
including community GPU-compatibility variants.

The installer does **not** require a single full-file SHA-256. Instead it records the input and output
hashes and verifies every byte range D18 needs to change. Each range must match either the known
unpatched layout or the complete D18 replacement. This permits unrelated compatibility edits while
refusing variants that alter D18's required code paths. The source file is never patched in place.

Entry points:

```text
community/d18-installer/Install-D18.bat
community/d18-installer/Uninstall-D18.bat
community/d18-installer/Build-D18Release.ps1
community/d18-installer/runtime_patch.json
```

If a managed D18 installation already exists in the selected game folder, running `Install-D18.bat`
again offers a safe replacement. After one confirmation it uses the exact-file uninstaller, retains
the previous timestamped backup, and then installs the current package. A manual uninstall is not
required before updating D18.

Advanced manual installation is documented in the Release README. The raw `payload` directory is not
itself drop-in: staged proxy/configuration files must be renamed, and a separately supplied Runtime
must already contain the D18 Runtime changes. The automatic installer is recommended for most users.

## Recommended D18 baseline

```ini
InternalScaling=true
InternalScalingRatio=0.5
CustomColorFilter=true
LinearResolve=false
LinearColorInput=false
PreserveHighFrequency=true
MotionAdaptive=false
TransferStrength=1.0
ColourStrength=1.0
```

OptiScaler Sharpness Override `0.80–0.90` is a useful starting range for final presentation tuning.
Sharpening cannot restore colour or semantic detail that the reduced network never received.

## Scope and safety

- Internal Network Scaling is currently D3D12-only.
- Native Vulkan from the 0.1.2 base remains available and uses the standard `WorkingScale` path.
- Runtime patching supports only 310.8-based files whose guarded D18 byte ranges remain compatible.
- The NVIDIA Runtime is not part of this repository and remains subject to NVIDIA's terms.
- Do not use injection mods in competitive or anti-cheat protected online games.
