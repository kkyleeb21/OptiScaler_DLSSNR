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
