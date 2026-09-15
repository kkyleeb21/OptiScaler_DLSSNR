# OptiScaler DLSSNR D18 — 0.1.8

## 0.1.8 revision 1 (R1)

- Unifies NR patch-site validation in the installer checker and native DX11 addon, removing the addon's stale unrelated host-code SHA allowlist while retaining patch-conflict and necessary ABI checks. Original user NR files are preserved. Admission is not proof of arbitrary runtime or game compatibility.
- Diagnostic collection accepts user-selected output directories, with a unique local-app-data folder by default. Existing evidence and game settings are preserved; legacy collectors and API summaries use a shared entry point.
- Public downloads now contain the release build only. Diagnostic builds are retained internally. Optional bounded Summary remains available and off by default. Shared history remains selectable, experimental and known to flicker.
- Removes internal historical deployment, cleanup, build and GPU experiment entry points from public archives; engineering source retains research tools. Rendering cores and GUI launchers are unchanged from 0.1.8; the addon and checker are rebuilt together.

To update an original 0.1.8 installation, exit the game and run this revised installer. User configuration and original NR files are preserved. REVISION.txt is 1; matching source tag: dlssnr-d18-v0.1.8-r1. The original tag is retained for provenance.

[简体中文](README_CN.md) · [Download release](https://github.com/kkyleeb21/OptiScaler_DLSSNR/releases/tag/dlssnr-d18-v0.1.8-r1) · [Nexus Mods](https://www.nexusmods.com/site/mods/2256) · [0.1.8 source](https://github.com/kkyleeb21/OptiScaler_DLSSNR/tree/dlssnr-d18-v0.1.8-r1) · [Release notes](https://github.com/kkyleeb21/OptiScaler_DLSSNR/blob/dlssnr-d18-v0.1.8-r1/community/d18-installer/RELEASE_NOTES_EN.md)

D18 is a community project based on OptiScaler for NVIDIA Neural Rendering (Feature 18). Its main path is **full SR image → NR at a chosen network ratio → one final composition**. It retains the full-resolution SR base while allowing a lower internal NR resolution. A 50% ratio scales both width and height: a 3840×2160 SR image uses a nominal 1920×1080 network. This is not a guaranteed 50% frame-time saving; cost and quality depend on the scene, runtime, GPU and settings.

Version 0.1.8 adds optional high-resolution single-pass NR and 1–4 NR passes, per-pass model settings, a reorganized UI, native API fixes and dependency preparation. **A working menu is not proof of SR/NR/FG compatibility.** Existing DX12 and native DX11/Vulkan routes need valid game inputs; automatic SR/FG input discovery for arbitrary games is not implemented.

## Choose a package

| Package | Purpose |
| --- | --- |
| `DLSSNR_D18_0.1.8_release.zip` | Complete installer for normal use. |

Only the release installer is publicly distributed; diagnostic builds are kept internally. Shared history remains available in release, marked experimental. Routine diagnostics are **off by default and bounded**. Diagnostic tools are consolidated under `tools/D18`; advanced Vulkan NR does not currently provide pixel Capture. NVIDIA NR/SR/FG runtimes are not bundled.

## Install, update and uninstall

1. **Exit the game.** Download and fully extract the release ZIP; run `D18Install.exe` from the extracted root. Do not copy the raw `payload` folder into the game directory.
2. Select the directory containing the actual game executable, a compatible proxy DLL name, and the **API the game actually launches with**: DX12, DX11 or Vulkan. Rerun setup with the matching API after changing the game's launch mode.
3. Supply a compatible **310.8-based `nvngx_dlssnr.dll`** using the local NR selector. Existing NR files can be discovered and revalidated. The installer checks the required patch locations and prepares a local copy; your original file is preserved. `nvngx.dll_dlssnr.dll` is the included forwarder, not the NVIDIA runtime. Passing layout validation does not establish hardware or game compatibility.
4. In the dependencies page, use **Check missing files**, then **Prepare missing files** if needed. SR, a matched plugin FG bundle, applicable REFramework and missing VC++ x64 can be prepared. Existing files are kept by default; the user must supply NR. VC++ installation may request Windows elevation. Wait for the required items to be prepared before **Save and continue**, then complete the final installation.
5. Start the game and open D18 with **Insert** on a fresh install, or your saved menu key. Establish the supported game SR/input route, then enable the D18 features you want and check their status. **Fresh installs leave D18 rendering, injected SR/FG, sharpening overrides, comparison and diagnostics off.** Upgrades preserve settings and do not disable the game's own SR/FG.

**Loader notes:** Neverness to Everness Steam installs beside the actual `HTGame.exe` using `version.dll`. For Arknights: Endfield, use the recommended `d3d12.dll` proxy and select the actual launch API independently. RE Engine integration uses the shared installer with applicable REFramework preparation; follow its existing-file prompts instead of replacing an unrelated `dinput8.dll`.

Anti-cheat detection shows a notice and permits installation after user confirmation. It does not certify anti-cheat compatibility or account safety. A different proxy name does not guarantee compatibility.

For updates, exit the game and run the new installer against the managed installation. Preserve `OptiScaler.ini`, forwarding variants and the managed backup/state files. To uninstall, exit the game and run `D18Uninstall.exe` for the same directory. `Install-D18.bat` / `Uninstall-D18.bat` remain alternate entry points.

After successful installation and hash verification, the completion page can clean the **current session's cache** by default; uncheck it if retaining downloads for another installation. Original user NR files, other sessions and rollback backups are preserved. Failed, busy or changed cache files are retained.

## Rendering controls

- **Neural rendering:** final detail/colour strength, highlight protection, detail reconstruction, colour transfer, model settings and per-pass controls. Input sampling, exposure/HDR and temporal controls retain their own availability hints.
- **Standard NR:** adjustable 50–100% network ratio. The project focuses on the full SR base with 50% NR; compare against 100% in the same scene. Reduced ratios can change texture, shape, colour and motion, so no preset is a universal quality guarantee.
- **Multipass:** 1–4 passes with independent ratios and model parameters. Every ratio is relative to full SR dimensions, not the previous pass's size. Model output feeds subsequent passes; final composition happens once. Start with 1–2 passes; 3–4 are advanced options with higher cost.
- **High-resolution single-pass:** optional 1.25× / 1.5× sizing, with aligned dimensions. It is a separate single-pass mode; more computation does not ensure a better image.
- **UI:** NR, SR, FG, sharpening and debug tabs. The overview contains status, live diagnostics, interface/hotkeys and original-image comparison. English / Simplified Chinese and per-game settings are supported.

The [feature reference](https://github.com/kkyleeb21/OptiScaler_DLSSNR/blob/dlssnr-d18-v0.1.8-r1/community/d18-installer/COMMON_FEATURES.md) describes the underlying controls; UI availability depends on the selected backend and build.

## SR / FG dependencies and limits

Plugin DLSS FG uses a `streamline` folder beside the game executable containing one matched bundle:

```text
sl.interposer.dll
sl.common.dll
sl.dlss_g.dll
sl.reflex.dll
sl.pcl.dll
nvngx_dlssg.dll
```

The installer can prepare this bundle and SR (`nvngx_dlss.dll`). Do not mix companion versions or overwrite the game's native SR/FG files with another game's files. **Having the DLLs alone does not supply colour, depth, motion vectors or a valid presentation route.**

Native DX11 plugin FG requires saving its initial route selection and restarting once to prepare presentation; it can then be toggled in game. Choose No FG and restart to release that route's interop overhead. FG may show ghosting, deformation or HUD misalignment; higher multipliers depend on the route. Vulkan FG remains experimental.

## Known visual and compatibility limits

- **Shared history is experimental, off by default and may flicker**, including blockwise lighting changes. Prefer independent history. Per-pass settings must match; mismatches retain one pass on DX12, while native DX11/Vulkan retains the SR base and reports failure.
- Multipass can strengthen contours, contrast and material/lighting effects, harden skin shading and push skin towards yellow/orange/red, **even with independent history**. More passes cost GPU time and VRAM and do not guarantee better quality. High-frequency preservation cannot guarantee correction of model reshaping or colour shifts.
- High-resolution NR also has uncertain visual benefits and higher cost. Return to standard single-pass if preferred. Native DX11 currently waits for each NR pass.
- Some size/format admission failures can recover after returning to standard mode; device loss may require restarting the game.
- Games without a supported input handoff still need compatibility work. Automatic universal SR/FG override and NVFP4 model support are not additions in this release. [Compatibility research](https://github.com/kkyleeb21/OptiScaler_DLSSNR/blob/dlssnr-d18-v0.1.8-r1/community/compatibility-research/README_CN.md) is tracked separately.

## Source, diagnostics and credits

Use the **`dlssnr-d18-v0.1.8-r1` tag** for the published source and [build instructions](https://github.com/kkyleeb21/OptiScaler_DLSSNR/blob/dlssnr-d18-v0.1.8-r1/BUILD_D18.md); the repository's default research branch can contain historical source. Revision 1 replaces the original 0.1.8 package; the original tag remains archived. `SHA256SUMS.txt` accompanies the public release ZIP.

For support, report the package/build, API, runtime hash, settings and observed symptom. Enable bounded Summary/diagnostics when needed; API success, log output and visual acceptance are distinct evidence. Share only the evidence you intend to disclose.

Based on OptiScaler and the OptiScaler DLSSNR work, with community contributions including Dagherbou, RenoDX and praydog/REFramework; NVIDIA provides DLSS/Neural Rendering technology. This is an independent community project, not endorsed by NVIDIA or game developers. Project code is GPL-3.0; third-party components retain their own licenses. NVIDIA runtimes remain subject to NVIDIA's terms.
