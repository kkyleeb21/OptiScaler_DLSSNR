# OptiScaler DLSSNR D18

> 0.1.6 is cumulative from public 0.1.4a, including the private 0.1.5 integration and subsequent validated fixes. DX12 Classic remains the default, Hybrid is optional, and Vulkan FG remains experimental and disabled by default.

D18 0.1.6 supports DLSS Neural Rendering on DX11 / DX12 / Vulkan, with English/Chinese UI, NR controls and OptiScaler frame-generation override.

[中文](README_CN.md) · [Download 0.1.6](https://github.com/kkyleeb21/OptiScaler_DLSSNR/releases/tag/dlssnr-d18-v0.1.6) · [Installation guide](community/d18-installer/README.md) · [Release notes](community/d18-installer/RELEASE_NOTES_0.1.6.md)

**Where Winds Meet:** Select the directory containing the `yysls.exe` your game actually launches: `Engine\Binaries\Win64r` or `Engine\Binaries\Win64rh`.

## Installation

1. Close the game, download and fully extract the 0.1.6 ZIP, and run Install-D18.bat.
2. Select the game executable directory, proxy DLL name and actual launch API. For Endfield, use the recommended d3d12.dll.
3. Supply a compatible NVIDIA 310.8 NR Runtime in runtime_input/nvngx_dlssnr.dll beside the installer, or select it when prompted. Installed nvngx_dlssnr.dll / D24Runtime.dll files are discovered and revalidated. DX11 requires the full Runtime hash listed in the installation guide.
4. Enable DLSS SR in game, press Insert for D18, and check NR status. Fresh native DX11/Vulkan installs use PgUp for NR; upgrades retain existing bindings.

Upgrades retain the proxy name and other settings. Selecting native DX11/Vulkan announces and aligns that API's Upscaler=dlss and [DLSS] Enabled=true settings. The previous installation is backed up, with restoration and verification attempted if the upgrade fails. Do not drag the raw payload folder into the game directory.

## Features

- NR enable, status and network ratio remain at the top; other controls are grouped by clarity, colour, characters and style, with purpose and availability hints.
- Internal network scaling retains Color/Output dimensions. The common package defaults to 50% network ratio and 0.85 sharpness override; adjust as needed.
- Colour preservation, reversible highlight encoding and transfer of model colour changes, enabled where supported by the backend.
- Live, persistent DX11 detail-flicker correction; on by default for Nioh 2, off by default elsewhere.
- English / Simplified Chinese switching at the top of the panel; Save Settings persists the choice per game. Chinese fonts use Windows system fonts.
- Configure OptiScaler FG input, output and enabled state together, then restart once when requested. Requested and observed multiplier states are distinct.
- Cross-API diagnostics, staged captures and NR allocation-failure fallback messages.

[Feature guide](community/d18-installer/COMMON_FEATURES.md)

## SR / FG files and game notes

NVIDIA NR/SR/FG runtimes are not included. If needed, place nvngx_dlss.dll beside the game executable. OptiScaler DLSS FG (including the DX11 route) requires a streamline folder beside that executable containing sl.interposer.dll, sl.common.dll, sl.dlss_g.dll, sl.reflex.dll, sl.pcl.dll and nvngx_dlssg.dll from one matching package. Preserve the game's original runtimes and restart after adding files.

Plugin 2× FG has worked in tested games. DX11 plugin FG compatibility varies by game; higher multipliers can ghost or misalign, so start with 2×. Baldur's Gate 3 may show a launcher “Data mismatch” warning; it did not affect use in our testing.

RE Engine integration uses REFramework; see the installation guide. Do not use injection mods in competitive or anti-cheat-protected online games.

## Source and historical baseline

[Build instructions](BUILD_D18.md) · [0.1.3a baseline](https://github.com/kkyleeb21/OptiScaler_DLSSNR/releases/tag/dlssnr-d18-v0.1.3)

Based on OptiScaler and licensed under GPL-3.0. Third-party components retain their respective licenses. NVIDIA Runtime files are supplied by the user.
