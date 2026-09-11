The root folder contains D18Install.exe, D18Uninstall.exe and the install/uninstall guide. Supporting files are in the D18 subfolder.

Candidate: 0.1.7. See [RELEASE_NOTES_EN.md](RELEASE_NOTES_EN.md) for current scope.

## GUI preview

Run `D18Install.exe` for the bilingual wizard; switch language at the upper right. Select a game EXE (UE bootstrap redirection supported), API and proxy name, dependencies, then an NR file. Check before installing. `Install-D18.bat` remains the CLI entry.

SR and plugin FG default to keeping existing files. Select downloads or local files as needed. Plugin FG downloads include official Streamline 2.14.1; installing files does not enable FG. Every selectable FG version has not been game-tested with this bundle. Explicit SR installs align the D18 SR library path. REFramework supports existing, official downloads or local files; missing manual dependencies are reported. Missing VC++ offers the official installer; system graphics dependencies are reported.

Logs and cache default to `%LOCALAPPDATA%\D18`. The existing backup, upgrade and rollback transaction is shared. Detailed backend logs retain their original English; UI and common outcomes are localized. Unpublished preview; NVIDIA SR/FG/NR files are not bundled.

# DLSSNR D18 Community Installer

> 0.1.6 is cumulative from public 0.1.4a, including the private 0.1.5 integration and subsequent validated fixes. DX12 Classic remains the default, Hybrid is optional, and Vulkan FG remains experimental and disabled by default.

An experimental, source-visible installer for the D18 OptiScaler DLSS Neural Rendering research build.

The current package version is `0.1.6`; the canonical archive name is `DLSSNR_D18_0.1.6.zip`.

It keeps game Color and final Output at display resolution while running a 310.8-based NVIDIA network on an independently sized lattice. At 3840x2160 and ratio 0.5, the exact network size is 1920x1080. A Mitchell-Netravali prefilter prepares model Color input, while the final composition can preserve high-frequency detail from the original frame.

## NVIDIA Runtime is not included

This project does not contain, redistribute or download NVIDIA's `nvngx_dlssnr.dll`. Supply your own 310.8 Runtime. The verified reference input is:

```text
SHA-256 E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
size    165840496 bytes
```

The installer verifies and applies the complete D18 patch to a local copy, records both hashes and leaves the supplied file unchanged. Every patch region must contain known original bytes or the complete D18 replacement; an unknown layout stops installation.

**Runtime validation checks D18 patch sites.** The installer and DX11 addon no longer fingerprint whole host-code regions, vtable data or read-only sections. They check installer patch sites plus the seven-byte native patch instruction; basic file validity checks remain. Passing does not guarantee every community modification works. Prefer the original runtime or a compatible RenoDX Discord community version.

DX12/Vulkan installation uses the common patch-region checks. Community edits outside the D18 regions can be retained, but this does not establish gameplay compatibility. Fully D18-patched inputs can be installed again without repeating the patch.

`runtime_patch_source` provides readable patch specifications, RVA notes and disassembly. Release builds verify that they reproduce `runtime_patch.json`. The locally modified output no longer has a valid NVIDIA Authenticode signature.

**Where Winds Meet:** Select the directory containing the `yysls.exe` your game actually launches: `Engine\Binaries\Win64r` or `Engine\Binaries\Win64rh`.

## Install

1. Close the game.
2. Extract the complete GitHub Release ZIP.
3. Double-click `Install-D18.bat`.
4. Select the game executable directory, proxy name and the API used to launch the game (DX12 / DX11 / Vulkan). Upgrades retain the proxy choice; select the API matching your next launch.
5. The installer discovers the game directory's `nvngx_dlssnr.dll` or native-backend `D24Runtime.dll` and revalidates compatibility. If none is found, it asks you to select your own 310.8-based Runtime.
6. Optional: to avoid selecting the Runtime manually, create `runtime_input` beside `Install-D18.bat` and place `nvngx_dlssnr.dll` inside it before starting the installer.
7. Enable the game's DLSS SR path, open D18 and verify NR is active. Use 100% as a quality reference before comparing lower network ratios.

The common package starts at 50% network ratio with the custom prefilter and Sharpness Override
`0.85`. These are shipped settings, not a universal quality recommendation. Lower ratios can change
detail, colour and motion stability; sharpening does not establish equivalence to full-resolution NR.
[Feature guide](COMMON_FEATURES.md).

Run `Uninstall-D18.bat` to restore every overwritten file from a timestamped backup. Files changed after installation are preserved separately instead of being silently discarded.

If this game folder already has a managed D18 installation, run `Install-D18.bat` again. It will offer
to safely replace the existing installation, keep its timestamped backup, and install the current
package; a separate uninstall is not required.

Before an existing installation is removed, the installer builds and verifies the new patched Runtime,
validates every payload source and destination, and checks available disk space. A failed preflight
leaves the working installation untouched.

## Manual payload installation (advanced)

`payload` is not a drop-in folder. Use the installer for Runtime preparation, filename mapping, configuration preservation and an exact uninstall manifest.

1. Close the game and back up every file that would be overwritten.
2. Copy `payload/OptiScaler.dll` into the game executable directory and rename it to your selected proxy, usually `dxgi.dll`; Endfield uses the verified `d3d12.dll` name.
3. **On a fresh install**, copy `payload/OptiScaler.ini.d18` as `OptiScaler.ini`; preserve the existing configuration on upgrades. Copy `payload/nvngx.dll_dlssnr.dll`, `payload/OptiScaler` and `payload/Licenses`.
4. Add the files for the API actually selected when launching the game. Supply your own Runtime with the complete D18 patch; an original Runtime or a GPU-compatibility edit alone is insufficient.

| Game API | Runtime filename in the game directory | Additional file |
| --- | --- | --- |
| DX12 | `nvngx_dlssnr.dll` | No native addon |
| DX11 | `D24Runtime.dll` | `payload/D24Native.dll` |
| Vulkan | `D24Runtime.dll` | `payload/D24VulkanNR.enabled` |

DX11 requires the patch-site checks stated above. For native DX11/Vulkan set `Dx11Upscaler=dlss` or `VulkanUpscaler=dlss` respectively under `[Upscalers]`, and `Enabled=true` under `[DLSS]`. `nvngx.dll_dlssnr.dll` is the forwarder, not NVIDIA's Runtime. Preserve the game's own SR/RR/FG and Streamline files; do not replace them with files from another game.

## Safety

- Internal network scaling supports DX11, DX12 and Vulkan. Native DX11/Vulkan use the verified complete 310.8 runtime patch.
- A community Runtime is accepted only when every D18-touched byte range remains compatible; this is not a claim that an arbitrary DLL is safe or supported.
- Do not use injection mods in competitive or anti-cheat protected online games. Account penalties are possible.
- Existing ReShade or mod-loader proxy DLLs are backed up, but replacing one can break its chain. Select the proxy name deliberately.
- The installer touches only its explicit file manifest and retains the backup after uninstall.
- Elden Ring ERSS files are outside the manifest and are left untouched.

## Source and licensing

The installer and OptiScaler changes are distributed under the repository's GPL-3.0 license. Third-party notices travel with the Release payload. NVIDIA's Runtime remains subject to NVIDIA's terms and is not part of this project.
## Controls and upgrades

Fresh installation defaults to Insert. Change it in game, or specify `-UiToggleKey F10`.
Other settings are preserved. Selecting native DX11/Vulkan announces and aligns its Upscaler=dlss and [DLSS] Enabled=true settings. `-UiToggleKey` does not override an existing binding.
Change UI/NR bindings in **Hotkeys**, then **Save Settings**.

This generic installer does not impose the Onimusha `d3d12.dll` / REFramework profile.
This package includes the new diagnostics and optional reconstruction controls, with experiments
off by default. [Release notes](RELEASE_NOTES_0.1.6.md) · [Feature guide](COMMON_FEATURES.md).

## REFramework installation

Known RE games use a matched nightly when available. Otherwise choose Latest (unverified for that game) or Manual. The only download source is https://github.com/praydog/REFramework-nightly . Only non-VR dinput8.dll is extracted after official SHA256 verification. Existing REF is retained; unrecognized loaders require confirmation. REF uses PgDn; D18 defaults to Insert. Both layouts read/write the game-root OptiScaler.ini.

Options: `-REFramework Auto|Recommended|Latest|Existing|Manual`; `-REEngine` selects the RE installation layout for an unknown executable without claiming rendering compatibility. Normal installations do not download REF.

## 0.1.6: DX11 / Vulkan installation and game notes

Select the graphics API used by the game and a proxy DLL name during installation. Upgrades retain prior choices and other settings while aligning the two required DLSS settings for the selected native API.
Command-line options: `-NativeApi DX11`, `-NativeApi Vulkan`, or `-NativeApi None` (DX12), and `-ProxyName dxgi.dll|winmm.dll|version.dll|dbghelp.dll|d3d12.dll`.
After changing the game's launch API, rerun the installer and select the matching API, or specify NativeApi explicitly.
Native DX11/Vulkan installs the locally patched runtime as `D24Runtime.dll` with its addon/activation file. Use the installer for these backends.
The DX11 installer and addon share patch-site checks. DX12/Vulkan retain their existing patch-region policy.

- Endfield: use `d3d12.dll`, as recommended by the installer. Match the API selected in the launcher.
- Baldur's Gate 3: the launcher may show “Data mismatch”; this did not affect the game or D18 in our testing.
- Plugin 2× FG passed testing. Higher multipliers may still have bugs; prefer 2×.
- Fresh installs use Insert for the menu; native DX11/Vulkan use PgUp for NR. Save custom bindings in the UI.

[0.1.6 release notes](RELEASE_NOTES_0.1.6.md).

## English and Simplified Chinese UI

Use **Language / 语言** at the top of D18 to switch between **English / 简体中文** immediately. **Save Settings** retains the choice for this game: `[Menu] D18Language=0` for English or `1` for Simplified Chinese. Both languages share the same controls and availability rules.

Chinese fonts are loaded from Windows, not redistributed. If a suitable font is unavailable, the panel stays in English and explains why. Technical names, filenames and raw diagnostic logs remain unchanged. The NR detail-flicker correction under Characters and style switches live; it defaults on for Nioh 2 and off for other games.

## Upgrade recovery and unattended installation

An upgrade verifies and snapshots the previous managed deployment before uninstalling it. If uninstall or copying fails, the installer attempts to restore and hash-check the previous files and settings. Recovery snapshots remain under D18_Backups/upgrade-recovery-* in the game directory; if recovery cannot write files, the error includes that path.
With `-Yes`, a missing Runtime produces an actionable `-RuntimePath` error instead of prompting. Changing APIs rechecks the required files and configuration.
