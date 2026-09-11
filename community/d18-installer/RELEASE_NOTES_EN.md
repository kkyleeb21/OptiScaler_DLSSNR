The root folder contains D18Install.exe, D18Uninstall.exe and the install/uninstall guide. Supporting files are in the D18 subfolder.

# D18 graphical installer and DX11 compatibility update

## New bilingual GUI installer

Run `D18Install.exe` and follow the installation wizard. Switch between English and Chinese at the upper right. The existing command-line install and uninstall entry points remain available.

- **Select a game and locate its folder**: browse for an EXE, choose a discovered game or select a running game process. Supported UE bootstrap executables can redirect to the actual game EXE; manual selection remains available for other cases.
- **Choose the graphics API and proxy filename**: select DX11, DX12 or Vulkan, then choose a supported proxy name such as dxgi.dll or winmm.dll.
- **Select the DLSS NR file**: provide your NR runtime. The installer performs the existing layout checks and shows the target folder and installation summary before proceeding.
- **Prepare SR / FG as needed**: keep existing files by default, select a version to download or use local files. Plugin FG downloads also prepare the Streamline files; explicit SR installation aligns the library path. Installing files does not automatically enable FG.
- **Prepare REFramework and dependencies**: known RE Engine games are detected, with a manual option available. Use existing REF files, official downloads or a local file. Missing VC++ x64 offers the official Microsoft installer; missing system graphics dependencies are reported.
- **See a clear result**: background checks and installation, access to logs, a green Installation complete page and a Finish button. Pending REF preparation and completed uninstallation have their own result messages.
- **Retain the existing installation transaction**: upgrades preserve settings and create backups, failed installations roll back, and uninstall restores managed original files. The GUI uses the shared backend without adding an SR/FG version allowlist.

## DX11 startup compatibility and status display

Improves startup compatibility in some DX11 games by fixing initialization errors caused by forced feature-level elevation. Both device creation entry points preserve requested feature levels and default-list semantics; UAV state handling follows the actual feature level.

DX11 compatibility note: older DX11 games may still have compatibility differences. This fix does not establish support for every DX11 game or require games to use feature level 11.1. Prefer games with a supported upscaling input integration and documented testing. SR, FG and NR availability depends on their respective game inputs, runtimes and device capabilities. Successful startup or an accessible menu does not establish feature integration; DX11.0 / DX11.1 alone is not a compatibility test.

Known DX11 frame-generation issue: enabling FG / Multi Frame Generation in some games may cause ghosting, jelly-like distortion or localized image misalignment. These image-quality issues remain a future optimization item and are not resolved by this startup compatibility fix; they do not affect every DX11 game. If encountered, try a lower frame-generation multiplier, or disable FG if the issue persists.

The dashboard separates presentation API, SR input API and independent SR/FG/NR status. Pipeline counters are hidden when SR input has not been observed. Enabling or applying SR settings does not create game input integration. English and Chinese messages are updated together.

NVIDIA SR/FG/NR runtimes are not bundled. Not every SR/FG combination in the download list has been tested in-game.

DLSS NR validation update: conflict checks are limited to locations D18 modifies. Code changes elsewhere no longer cause rejection, and correctly D18-patched files are also accepted. Invalid files or the wrong architecture are still rejected. Passing validation does not guarantee compatibility with every community modification. We recommend the original DLSS NR runtime or a compatible version provided by the RenoDX Discord community; the source name alone does not establish verification.

Run `D18Install.exe` to open the graphical installer.
