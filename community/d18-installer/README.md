# DLSSNR D18 Community Installer

An experimental, source-visible installer for the D18 OptiScaler DLSS Neural Rendering research build.

It keeps game Color and final Output at display resolution while running the audited NVIDIA 310.8 network on an independently sized lattice. At 3840x2160 and ratio 0.5, the exact network size is 1920x1080. A Mitchell-Netravali prefilter prepares model Color input, while the final composition can preserve high-frequency detail from the original frame.

## NVIDIA Runtime is not included

This project does not contain, redistribute, or download `nvngx_dlssnr.dll`. Supply the exact official 310.8 file yourself:

```text
SHA-256 E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
size    165840496 bytes
```

The installer verifies the complete file, applies 1007 guarded replacement bytes to a private copy, and verifies the result:

```text
SHA-256 CCAC112995922D8BD2C5F2D0DCB7A6756B7806D3D868692ACB9AF64D4AEF7414
```

The source file is never patched in place. The locally produced file no longer has a valid NVIDIA Authenticode signature.

## Install

1. Close the game.
2. Extract the complete GitHub Release ZIP.
3. Create `runtime_input` beside `Install-D18.bat` and place your official `nvngx_dlssnr.dll` there, or provide its path when prompted.
4. Run `Install-D18.bat`.
5. Enter the directory containing the game executable and select an appropriate proxy name. `dxgi.dll` is the default.
6. Open the OptiScaler menu in game and verify Internal network scaling `0.500` and Custom Mitchell model Color prefilter are enabled.

The baseline enables Sharpness Override at `0.85`. Testing across Cyberpunk 2077, Elden Ring and Wuthering Waves found `0.80-0.90` subjectively closest to the full-resolution presentation, but it cannot restore color or semantic detail the reduced network never received.

Run `Uninstall-D18.bat` to restore every overwritten file from a timestamped backup. Files changed after installation are preserved separately instead of being silently discarded.

## Safety

- Experimental, D3D12-only internal network scaling tied to the audited 310.8 Runtime.
- Do not use injection mods in competitive or anti-cheat protected online games. Account penalties are possible.
- Existing ReShade or mod-loader proxy DLLs are backed up, but replacing one can break its chain. Select the proxy name deliberately.
- The installer touches only its explicit file manifest and retains the backup after uninstall.
- Elden Ring ERSS files are outside the manifest and are left untouched.

## Source and licensing

The installer and OptiScaler changes are distributed under the repository's GPL-3.0 license. Third-party notices travel with the Release payload. NVIDIA's Runtime remains subject to NVIDIA's terms and is not part of this project.
