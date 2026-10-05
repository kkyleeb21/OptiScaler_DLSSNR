# D18 0.3.1 — cleaner installer game list

- The installer now shows one entry per game, using the game name. Crash reporters, runtime tools, art books and other non-game programs are no longer listed.
- If NVIDIA App is installed, its game list takes priority, supplemented by Steam / Epic / GOG / EA installation records. Games that are not listed can still be added manually or by drag and drop.
- Core features are unchanged from 0.3.0. Existing 0.3.0 users do not need to upgrade for in-game features.

## New in 0.3.0

- **Redesigned in-game menu.** Fixed top and bottom bars, tabs (Basics / Advanced / SR·FG / Sharpening / Settings), and a separate status and diagnostics window that can be moved and resized. Black and white is the default theme, with other palettes and a custom accent available. New settings: menu brightness in HDR, high-contrast text, text size. The footer shows whether there are unsaved changes and whether saving succeeded.
- **New installer.** A single `D18Setup.exe` lists your games (Steam, Epic, GOG, EA, or add one manually / by drag and drop), installs, upgrades and uninstalls. The interface is black and white and follows the Windows light / dark setting. The main package is much smaller; FSR / XeSS libraries are now optional components that the installer downloads only when you tick them.
- **NR white point when the game supplies no exposure (experimental, DX12).** D18 can estimate the white point from the image before NR runs. It defaults on only for 007 First Light, where it fixes the colour cast and grey shadows; other games keep their current behaviour and can enable it under Advanced → Exposure and HDR input. The Paper white minimum is now 0.01.

**Update:** exit the game, extract `DLSSNR_D18_0.3.1_release.zip` and run `D18Setup.exe`. Existing settings are preserved. If you used D18 to provide FSR / XeSS, tick the optional components during the upgrade. NR starts disabled on fresh installs; turn it on in the menu. No NVIDIA NR / SR / FG runtimes are bundled; supply your compatible NR file in the installer.

**Known issues:** the installer is unsigned, so Windows may show an unknown-publisher warning. In games that use polled input, opening the menu immediately after entering the game may leave sliders undraggable while the mouse still turns the camera; close and reopen the menu. White-point estimation has only been exercised in one game and does not address temporal artefacts that the game shows with NR off.

`DLSSNR_D18_0.3.1_optional_components.zip` is fetched by the installer when needed; you do not have to download it yourself. `SHA256SUMS.txt` identifies the release files.

---

[简体中文](README_CN.md) · [Download release](https://github.com/kkyleeb21/OptiScaler_DLSSNR/releases/tag/dlssnr-d18-v0.3.1) · [Nexus Mods page for 007 First Light](https://www.nexusmods.com/007firstlight/mods/228) (GitHub has the full documentation and issue tracker) · [0.3.1 source](https://github.com/kkyleeb21/OptiScaler_DLSSNR/tree/dlssnr-d18-v0.3.1) · [Release notes](https://github.com/kkyleeb21/OptiScaler_DLSSNR/blob/dlssnr-d18-v0.3.1/community/d18-installer/RELEASE_NOTES_EN.md)

D18 is a community project based on OptiScaler for NVIDIA Neural Rendering (Feature 18). Its main path is **full SR image → NR at a chosen network ratio → one final composition**. It retains the full-resolution SR base while allowing a lower internal NR resolution. A 50% ratio scales both width and height: a 3840×2160 SR image uses a nominal 1920×1080 network. This is not a guaranteed 50% frame-time saving; cost and quality depend on the scene, runtime, GPU and settings.

Version 0.1.8 adds optional high-resolution single-pass NR and 1–4 NR passes, per-pass model settings, a reorganized UI, native API fixes and dependency preparation. **A working menu is not proof of SR/NR/FG compatibility.** Existing DX12 and native DX11/Vulkan routes need valid game inputs; automatic SR/FG input discovery for arbitrary games is not implemented.

## Choose a package

| Package | Purpose |
| --- | --- |
| `DLSSNR_D18_0.3.1_release.zip` | Installer for normal use. |
| `DLSSNR_D18_0.3.1_optional_components.zip` | FSR / XeSS libraries. The installer downloads this when you tick the optional components; manual download is only needed offline. |

Only the release installer is publicly distributed; diagnostic builds are kept internally. Shared history remains available in release, marked experimental. Routine diagnostics are **off by default and bounded**. Diagnostic tools are consolidated under `tools/D18`; advanced Vulkan NR does not currently provide pixel Capture. NVIDIA NR/SR/FG runtimes are not bundled.

## Install, update and uninstall

1. **Exit the game.** Download and fully extract the release ZIP; run `D18Setup.exe` from the extracted root and pick the game from the list (or add it manually / by drag and drop). Do not copy the raw `payload` folder into the game directory.
2. Select the directory containing the actual game executable, a compatible proxy DLL name, and the **API the game actually launches with**: DX12, DX11 or Vulkan. Rerun setup with the matching API after changing the game's launch mode.
3. Supply a compatible **310.8-based `nvngx_dlssnr.dll`** using the local NR selector. Existing NR files can be discovered and revalidated. The installer checks the required patch locations and prepares a local copy; your original file is preserved. `nvngx.dll_dlssnr.dll` is the included forwarder, not the NVIDIA runtime. Passing layout validation does not establish hardware or game compatibility.
4. Under **DLSS files**, use **Check missing files**, then **Prepare selected now** if needed. SR, a matched plugin FG bundle, applicable REFramework and missing VC++ x64 can be prepared. Existing files are kept by default; the user must supply NR. VC++ installation may request Windows elevation. Then press **Install / upgrade D18**.
5. Start the game and open D18 with **Insert** on a fresh install, or your saved menu key. Establish the supported game SR/input route, then enable the D18 features you want and check their status. **Fresh installs leave NR, injected SR/FG, sharpening overrides, comparison and diagnostics off.** NR and optional features default to off. Enabling NR uses a 100% internal ratio, white point from exposure and automatic masking. The 100% ratio costs more than 50%; lower it in the menu if needed. Upgrades preserve settings and do not disable the game's own SR/FG.

**Loader notes:** Neverness to Everness Steam installs beside the actual `HTGame.exe` using `version.dll`. For Arknights: Endfield, use the recommended `d3d12.dll` proxy and select the actual launch API independently. RE Engine integration uses the shared installer with applicable REFramework preparation; follow its existing-file prompts instead of replacing an unrelated `dinput8.dll`.

Anti-cheat detection shows a notice and permits installation after user confirmation. It does not certify anti-cheat compatibility or account safety. A different proxy name does not guarantee compatibility.

For updates, exit the game and run the new installer against the managed installation. Preserve `OptiScaler.ini`, forwarding variants and the managed backup/state files. To uninstall, exit the game, run `D18Setup.exe`, open the same game and use **Uninstall D18**. `Install-D18.bat` / `Uninstall-D18.bat` remain alternate entry points.

After successful installation and hash verification, the completion page can clean the **current session's cache** by default; uncheck it if retaining downloads for another installation. Original user NR files, other sessions and rollback backups are preserved. Failed, busy or changed cache files are retained.

## Rendering controls

- **Neural rendering:** final detail/colour strength, highlight protection, detail reconstruction, colour transfer, model settings and per-pass controls. Input sampling, exposure/HDR and temporal controls retain their own availability hints.
- **Standard NR:** adjustable 50–100% network ratio. The project focuses on the full SR base with 50% NR; compare against 100% in the same scene. Reduced ratios can change texture, shape, colour and motion, so no preset is a universal quality guarantee.
- **Multipass:** 1–4 passes with independent ratios and model parameters. Every ratio is relative to full SR dimensions, not the previous pass's size. Model output feeds subsequent passes; final composition happens once. Start with 1–2 passes; 3–4 are advanced options with higher cost.
- **High-resolution single-pass:** optional 1.25× / 1.5× sizing, with aligned dimensions. It is a separate single-pass mode; more computation does not ensure a better image.
- **UI:** NR, SR, FG, sharpening and debug tabs. The overview contains status, live diagnostics, interface/hotkeys and original-image comparison. English / Simplified Chinese and per-game settings are supported.

The [feature reference](https://github.com/kkyleeb21/OptiScaler_DLSSNR/blob/dlssnr-d18-v0.3.1/community/d18-installer/COMMON_FEATURES.md) describes the underlying controls; UI availability depends on the selected backend and build.

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
- Games without a supported input handoff still need compatibility work. Automatic universal SR/FG override and NVFP4 model support are not additions in this release. [Compatibility research](https://github.com/kkyleeb21/OptiScaler_DLSSNR/blob/dlssnr-d18-v0.3.1/community/compatibility-research/README_CN.md) is tracked separately.

## Source, diagnostics and credits

Use the **`dlssnr-d18-v0.3.1` tag** for the release source after publication and [build instructions](https://github.com/kkyleeb21/OptiScaler_DLSSNR/blob/dlssnr-d18-v0.3.1/BUILD_D18.md); the repository's default research branch can contain historical source. 0.3.1 succeeds 0.3.0; earlier tags remain archived. `SHA256SUMS.txt` accompanies the public release ZIP.

For support, report the package/build, API, runtime hash, settings and observed symptom. Enable bounded Summary/diagnostics when needed; API success, log output and visual acceptance are distinct evidence. Share only the evidence you intend to disclose.

Based on OptiScaler and the OptiScaler DLSSNR work, with community contributions including Dagherbou, RenoDX and praydog/REFramework; NVIDIA provides DLSS/Neural Rendering technology. This is an independent community project, not endorsed by NVIDIA or game developers. Project code is GPL-3.0; third-party components retain their own licenses. NVIDIA runtimes remain subject to NVIDIA's terms.
