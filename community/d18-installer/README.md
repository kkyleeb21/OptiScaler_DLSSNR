# DLSSNR D18 Community Installer

An experimental, source-visible installer for the D18 OptiScaler DLSS Neural Rendering research build.

It keeps game Color and final Output at display resolution while running a 310.8-based NVIDIA network on an independently sized lattice. At 3840x2160 and ratio 0.5, the exact network size is 1920x1080. A Mitchell-Netravali prefilter prepares model Color input, while the final composition can preserve high-frequency detail from the original frame.

## NVIDIA Runtime is not included

This project does not contain, redistribute, or download `nvngx_dlssnr.dll`. Supply your own 310.8-based Runtime. The official file below is the reference build, but it is not the only accepted full-file hash:

```text
SHA-256 E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
size    165840496 bytes
```

Community compatibility builds for 20/30/40-series GPUs may change unrelated parts of the Runtime. D18 therefore records, but does not allowlist, the complete input SHA-256. Instead it verifies every byte range it needs to change. Each range must contain either the known unpatched 310.8 bytes or the complete D18 replacement.

This preserves unrelated community compatibility edits while refusing a DLL whose D18 code paths have an unknown layout. Already-D18-patched inputs are accepted idempotently. Input and output hashes are written to the installation state for traceability.

The source file is never patched in place. A locally modified output no longer has a valid NVIDIA Authenticode signature.

## Install

1. Close the game.
2. Extract the complete GitHub Release ZIP.
3. Create `runtime_input` beside `Install-D18.bat` and place your 310.8-based `nvngx_dlssnr.dll` there, or provide its path when prompted.
4. Run `Install-D18.bat`.
5. Enter the directory containing the game executable and select an appropriate proxy name. `dxgi.dll` is the default.
6. Open the OptiScaler menu in game and verify Internal network scaling `0.500` and Custom Mitchell model Color prefilter are enabled.

The baseline enables Sharpness Override at `0.85`. Testing across Cyberpunk 2077, Elden Ring and Wuthering Waves found `0.80-0.90` subjectively closest to the full-resolution presentation, but it cannot restore color or semantic detail the reduced network never received.

Run `Uninstall-D18.bat` to restore every overwritten file from a timestamped backup. Files changed after installation are preserved separately instead of being silently discarded.

## Safety

- Experimental, D3D12-only internal network scaling for the guarded 310.8 layout family.
- A community Runtime is accepted only when every D18-touched byte range remains compatible; this is not a claim that an arbitrary DLL is safe or supported.
- Do not use injection mods in competitive or anti-cheat protected online games. Account penalties are possible.
- Existing ReShade or mod-loader proxy DLLs are backed up, but replacing one can break its chain. Select the proxy name deliberately.
- The installer touches only its explicit file manifest and retains the backup after uninstall.
- Elden Ring ERSS files are outside the manifest and are left untouched.

## Source and licensing

The installer and OptiScaler changes are distributed under the repository's GPL-3.0 license. Third-party notices travel with the Release payload. NVIDIA's Runtime remains subject to NVIDIA's terms and is not part of this project.
