# D18 0.1.8

A cumulative update from 0.1.7 with high-resolution single-pass NR, 1–4 NR passes, per-pass model controls, a reorganized tabbed UI, native DX11 integration fixes, format handling and dependency preparation. The full SR base is retained. Each ratio is relative to full SR dimensions; model results feed successive passes, followed by one final composition.

## Rendering and UI

- DX12 and existing native DX11/Vulkan NR routes with valid input handoffs support independent multipass, per-pass parameters and 1.25× / 1.5× high-resolution single-pass NR, with aligned dimensions. This does not automatically supply SR/FG inputs to arbitrary games without an upscaling interface.
- Neural rendering, SR, FG, sharpening and debugging tabs; the overview retains status, live diagnostics, hotkeys and original-image comparison. Model settings group mode, pass count, history and per-pass controls.
- Advanced NR checks resource formats and device read/write support, preserves the original-format SR base and uses private FP16 model intermediates. Some format/size admission failures can be retried after switching back to standard mode; device loss may still require a restart.
- Final high-frequency preservation covers all DX12 NR modes and advanced native DX11/Vulkan composition without extra model calls. Native ordinary single-pass behavior is retained.
- Fixes native SR enable/quality persistence and FG request/status handling. Native DX11 requires saving the initial plugin FG route and restarting once to prepare presentation, then allows in-game toggling. Replacing a normal swapchain with an FG swapchain during that first run is not implemented. The prepared route has interop overhead; select No FG and restart to release it.

## Installation and defaults

- Separate full release and diagnostic packages. Use release for normal play; diagnostic additionally permits explicit pixel capture, model bypass and input observation. Routine diagnostics are off by default and bounded; shared tools are under tools/D18.
- Fresh installations leave D18 NR, injected/native SR, plugin FG, sharpening overrides, comparison and diagnostics disabled. High-resolution NR, extra passes and shared history are opt-in. Upgrades preserve user settings and do not disable the game's own SR/FG.
- The dependency page can check and prepare missing SR, matched plugin FG companions, applicable REFramework and VC++ x64. Existing files are kept by default. Users must supply NR; NVIDIA SR/FG/NR runtimes are not bundled.
- After successful installation and hash verification, Finish cleans files owned by the current cache session by default, with an opt-out. Failed, busy or changed files are retained. Original user NR files, other sessions and rollback backups are never removed.
- Select the API actually used by the game; rerun setup with the corresponding API after changing it. For Neverness to Everness on Steam, install beside the actual HTGame.exe and select `version.dll`.
- Anti-cheat detection displays a notice and permits installation after user confirmation; it is not an assurance of anti-cheat compatibility or account safety.

## Known limitations

- Shared history is selectable in both profiles, experimental and off by default. It can flicker, produce blockwise lighting changes or unstable motion; independent history is recommended. Pass settings must match. With mismatched settings DX12 retains one pass, while native DX11/Vulkan retains the SR base and reports failure.
- More passes cost GPU time and VRAM and may strengthen contours, local contrast and material/lighting effects. Skin transitions can harden or shift toward darker, yellow, orange or red tones, including with independent history. More passes do not guarantee better quality; start with one or two and adjust model/final composition strength.
- High-frequency preservation retains fine texture but does not guarantee correction of model reshaping, skin shifts or tonal blocks. High-resolution single-pass NR can also change style, with uncertain quality gains and higher cost; return to standard single-pass if preferred.
- FG availability, HUD separation, motion and higher multipliers depend on valid inputs and the presentation route. DX11 may show ghosting, deformation or misalignment. Vulkan FG remains experimental. Native DX11 currently waits for each NR pass; advanced Vulkan NR has no pixel Capture support yet.
- Automatic discovery of colour/depth/motion inputs and SR/FG integration in games without a standard upscaling interface is not implemented. Compatibility research is separate. API success or a working menu is not proof of image quality or universal compatibility.
