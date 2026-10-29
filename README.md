# DLSSNR D18 0.5.1 installation

Run `D18Setup.exe` from the main package. Select a game from the list or choose its executable manually. The same program installs, upgrades and removes D18, and can run as a standalone EXE. Exit the game first, supply your own compatible NR file and review its classification; unverified compatible files require an additional acknowledgement.

`DLSSNR_D18_0.5.1_release.zip` is the lean main package. To use FSR / XeSS supplied by D18, select optional components and choose `DLSSNR_D18_0.5.1_optional_components.zip`. The installer downloads this ZIP from the release when needed; you can also choose a local copy. NVIDIA runtime files are not bundled.

Upgrades preserve settings. Users previously relying on D18 for FSR / XeSS must select optional components during upgrade. NR starts disabled on fresh installs; existing fresh defaults remain. Older configurations missing new keys use core defaults.

Insert opens the in-game menu; the REFramework loading route uses the selected menu key. Status and diagnostics have separate windows. Advanced → Exposure and HDR inputs includes experimental DX12 white-point estimation, enabled by default only for 007 First Light.

The installer is unsigned and Windows may show an unknown publisher. In Onimusha, opening the menu immediately after entering the game may prevent slider dragging and allow mouse camera movement; close and reopen the menu.

Automation: `D18Setup.exe --offline --data-root <work> --request <request.json> --result <result.json>`. Actions: Check / Install / Uninstall / ValidateNr / Discover / Meta / AuditDependencies. Discovery tests must supply scanRoots pointing to fixture directories. Exit codes: 0 success, 1 failure, 2 invalid arguments. Removal needs only a game folder.

Low-level CLI remains available: `Install-D18.ps1 -GameDir <folder> -RuntimePath <NR> -NativeApi None|DX11|Vulkan -ProxyName dxgi.dll -REFramework Manual -Yes`; `Uninstall-D18.ps1 -GameDir <folder> -Yes`.

Read RELEASE_NOTES_EN.md, RUNTIME_COMPATIBILITY.md, LICENSE and THIRD_PARTY_NOTICES.md. File version 0.5.1.0. File checks do not establish in-game compatibility or visual acceptance.

## Colour grade

The Colour grade section on Basics can disable the style's built-in grade or replace it with custom exposure, contrast, saturation and the additional controls under More. It can be combined with any model style, and three preset slots save both style and grade. Custom grading is off by default, preserving the style's original grade. Requires NR runtime 310.8 or 310.9.16 on DX12 / Vulkan.

## Brighten limit / darken limit

Advanced → Composition provides separate controls for how much NR brightens and darkens the image. Separate darkening is off by default (MaxDarken=auto follows the brighten limit), preserving previous behavior. Native DX11 does not yet support a separate darken limit. This limit can act twice during composition, so the actual darkest value may fall below the displayed percentage.

## Pause NR while the game turns frame generation off

This switch on Advanced pauses NR while the game turns its built-in DLSS frame generation off and resumes automatically when it returns. It is off by default and only applies to DX12 games using their built-in DLSS frame generation. In menus, dialogue or cutscenes, this avoids running NR at the full real frame rate after the game removes its frame-rate cap.

## NR runtime versions

DX12 / Vulkan support NR 310.8 and 310.9.16, including colour grading. Native DX11 currently supports only 310.8; 310.9.16 is refused with a message. The installer patches a local copy of your supplied file; D18 does not bundle the runtime. On 310.9.16, Preset A · Standard is the default, and Preset B · Lightweight FP8 is an optional faster model. Color input sampling defaults to Follow Runtime, with Force point and Force linear options. 310.8 options are unchanged. A known community compatibility build is classified as experimental, unsigned and of unknown origin; it is neither recommended nor distributed, and has not been tested on RTX 20/30/40.
