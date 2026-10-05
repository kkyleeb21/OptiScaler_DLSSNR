# D18 0.3.0

- Redesigned in-game menu with fixed top and bottom bars, tabs, and separate status and diagnostics windows. Black and white is the default theme, with optional colour themes, HDR menu brightness, high-contrast text, text size and save-status feedback.
- New installer: a single `D18Setup.exe` with a game list handles both installation and removal, in a black-and-white interface that follows the Windows light / dark setting. The main package is lean; FSR / XeSS are supplied in a separate optional-components package.
- NR white-point estimation is available when the game supplies no exposure. It currently defaults on only for 007 First Light; other games can enable it under Advanced → Exposure and HDR inputs. DX12 only; experimental. The Paper white minimum is now 0.01.

Upgrades preserve existing settings. If you previously used D18 to supply FSR / XeSS, tick the optional components during upgrade; the installer downloads them from this release, or you can point it at the optional-components ZIP you downloaded yourself. NR starts disabled on fresh installs.

Known issues: the installer is unsigned, so Windows may show an unknown-publisher warning. Onimusha uses polling input; opening the menu immediately after entering the game may prevent slider dragging and allow the mouse to turn the camera. Close and reopen the menu to recover.
