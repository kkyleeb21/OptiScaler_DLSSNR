# D18 0.5.1

- Fixes DLSS file discovery in some Unreal Engine games that store the files in engine plugin folders instead of beside the game EXE. Previously, missing `nvngx_dlss.dll` / `nvngx_dlssd.dll` silently switched DLSS and ray reconstruction to FSR 2, causing grainy ray-traced shadows and leaving DLSS frame generation unavailable. D18 now checks these plugin folders.
- If a file is still missing, the menu status page names it and shows the FSR version in use.
- `NvngxDlssPath` now accepts a folder or file; folders without a trailing slash are handled correctly. It affects only the DLSS super resolution file.
- When the installer refuses an NR file, it explains whether the version is unsupported, the file is not NR, it has been modified, or its patch state is incomplete, and lists supported versions.
- The installer accepts older patch manifests that omit descriptive fields.

Update: exit the game and use Upgrade in the same directory; settings are preserved. Select optional components if you use FSR / XeSS supplied by D18; a local ZIP can be used.

Known issues: the installer is unsigned. In games using polled input, opening the menu immediately after entering may leave sliders undraggable while the mouse turns the camera; close and reopen the menu. White-point estimation remains experimental and does not address temporal artefacts already present with NR off. Native DX11 supports only 310.8; 310.9.16 is refused with a message and NR is disabled. Native DX11 still lacks colour grading and a separate darken limit. Colour grading requires DX12 / Vulkan with 310.8 or 310.9.16. NR pause applies only to DX12 games using built-in DLSS frame generation.

---

# D18 0.5.0

- Supports NVIDIA NR runtime 310.9.16, released on 2026-10-16, while retaining 310.8 support. The installer patches your supplied file locally; D18 does not bundle the runtime.
- On 310.9.16, choose Preset A · Standard (default, the same model as 310.8) or Preset B · Lightweight FP8 (NVIDIA's new faster model). These names match NR presets A / B in NVIDIA driver settings. The lightweight model takes about 40% less NR GPU time, with some differences in colour and structure. 310.8 options are unchanged.
- Network ratio on 310.9.16 uses the runtime's own scaling: at 50%, NR time is roughly halved. Allocation remains at full resolution, so peak VRAM does not decrease.
- Color input sampling on 310.9.16 follows the runtime by default, which already uses linear sampling when scaling down. Force point and Force linear remain available.
- The installer recognises a known community 310.9.16 compatibility build intended for RTX 20/30/40, labelled experimental, unsigned and of unknown origin. It is not recommended or distributed, and has not been tested on those GPU generations.
- Includes all fixes from the unpublished 0.4.1: grade updates are synchronised with NR evaluation; failed protection restoration keeps retrying; invalid white/black point combinations show a message and grading is not applied below local tone 0.05. NR pause is disabled with multiple FG viewports, and retired experimental keys cannot override the current switch. V8 displays effective limits. The installer refuses drive roots, system directories, user folders themselves and folders without a top-level EXE; existing installations can still be removed.

Update: exit the game and use Upgrade in the same directory; settings are preserved. Select optional components if you use FSR / XeSS supplied by D18; a local ZIP can be used.

Known issues: the installer is unsigned. In games using polled input, opening the menu immediately after entering may leave sliders undraggable while the mouse turns the camera; close and reopen the menu. White-point estimation remains experimental and does not address temporal artefacts already present with NR off. Native DX11 supports only 310.8; 310.9.16 is refused with a message and NR is disabled. Native DX11 still lacks colour grading and a separate darken limit. Colour grading requires DX12 / Vulkan with 310.8 or 310.9.16. NR pause applies only to DX12 games using built-in DLSS frame generation.

---

# D18 0.4.1

This is a maintenance release with the same features as 0.4.0. An update is recommended if you use its colour grade, separate darken limit or NR pause features.

- **Colour grade:** grade updates and NR evaluation can no longer interleave. Failed memory-protection restoration keeps retrying and is reported accurately. At low local tone, white / black point combinations that cannot be applied show a message instead of being written; grading is not applied below local tone 0.05.
- **NR pause:** the feature is automatically disabled with a message when multiple frame-generation viewports are detected. The retired experimental configuration key no longer overrides the current switch.
- **V8 mode:** the menu shows the brighten / darken limits that actually take effect.
- **Installer:** refuses drive roots, system directories, user folders themselves, and folders without a top-level EXE, with a reason. Uninstalling an existing installation is unaffected.

Use Upgrade in the same game directory to update; your settings are preserved. Known issues below still apply; native DX11 does not yet support colour grading or a separate darken limit.

---

# D18 0.4.0

- **Colour grade**: on Basics. NR's three styles combine model differences with their own exposure, contrast and saturation adjustments; you can now disable that grade or replace it with your own exposure, contrast, saturation, and black/white point, gamma, colour bias and tonal curves under More. Combine it with any style; three preset slots save both style and grade. Requires NR runtime 310.8 on DX12 / Vulkan.
- **Brighten limit / darken limit**: Advanced → Composition. The former highlight protection is split into two controls, so you can limit how much NR darkens the image, for example to reduce character darkening, without changing its brightening. The darken limit acts twice during composition, so the actual darkest value may fall below the displayed percentage.
- **Pause NR while the game turns frame generation off**: on Advanced. Some games turn their built-in DLSS frame generation off and remove the frame-rate cap in menus, dialogue or cutscenes; NR then runs at the full real frame rate, increasing power use. Enable this to pause NR during those periods and resume automatically when frame generation returns. Only applies to DX12 games using their built-in DLSS frame generation.

All three features are off by default; when left off, behavior matches 0.3.1. Alternate-frame NR and related experiments are not included. To update, use Upgrade in the same game directory; existing settings are preserved.

The known issues listed below for 0.3.1 / 0.3.0 still apply. Native DX11 games do not yet support colour grading or a separate darken limit.

---

# D18 0.3.1

- The installer now shows one entry per game, using the game name. Crash reporters, runtime tools, art books and other non-game programs are no longer listed.
- If NVIDIA App is installed, its game list takes priority, supplemented by Steam / Epic / GOG / EA installation records. Games that are not listed can still be added manually or by drag and drop.
- Core features are unchanged from 0.3.0. Existing 0.3.0 users do not need to upgrade for in-game features. Updates preserve your settings; the known issues from 0.3.0 still apply.

---

# D18 0.3.0

- Redesigned in-game menu with fixed top and bottom bars, tabs, and separate status and diagnostics windows. Black and white is the default theme, with optional colour themes, HDR menu brightness, high-contrast text, text size and save-status feedback.
- New installer: a single `D18Setup.exe` with a game list handles both installation and removal, in a black-and-white interface that follows the Windows light / dark setting. The main package is lean; FSR / XeSS are supplied in a separate optional-components package.
- NR white-point estimation is available when the game supplies no exposure. It currently defaults on only for 007 First Light; other games can enable it under Advanced → Exposure and HDR inputs. DX12 only; experimental. The Paper white minimum is now 0.01.

Upgrades preserve existing settings. If you previously used D18 to supply FSR / XeSS, tick the optional components during upgrade; the installer downloads them from this release, or you can point it at the optional-components ZIP you downloaded yourself. NR starts disabled on fresh installs.

Known issues: the installer is unsigned, so Windows may show an unknown-publisher warning. Onimusha uses polling input; opening the menu immediately after entering the game may prevent slider dragging and allow the mouse to turn the camera. Close and reopen the menu to recover.
