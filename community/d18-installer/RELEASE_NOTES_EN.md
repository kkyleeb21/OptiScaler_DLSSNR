# D18 0.2.0 — deferred startup and clearer frame-generation status

- Adds **deferred startup** to fix the launch crash after the 007 First Light 1.3.0 update. Automatic mode applies only to this game; `[Hooks] DeferredStartup` accepts `auto`, `true` or `false` for manual control. Deferred startup currently supports only the `dxgi.dll` and `d3d12.dll` loading names. Its behaviour in other games has not been verified.
- Makes **frame-generation status** more accurate. When D18 has not observed generated-frame presentation, the panel shows the game's requested DLSSG multiplier and says the actual frame count has not been observed. Once a presentation count is available, the existing counter remains. Frame-generation behaviour and status colours are unchanged.
- Core, native backend and runtime checker are rebuilt for 0.2.0. Existing NR, SH0 and experimental V8 behaviour, activation conditions and limits remain; NR and optional features default to off. Enabling NR uses a 100% internal ratio, white point from exposure and automatic masking. The 100% ratio costs more than 50%; lower it in the menu if needed.

**Update:** exit the game, fully extract the ZIP and run `D18Install.exe`. Existing settings and original-runtime backups are preserved. `[Hooks] DeferredStartup` can be added to `OptiScaler.ini`; an absent key uses automatic mode. No NVIDIA NR/SR/FG runtimes are bundled. Supply your compatible NR runtime through the installer.

**Existing limits remain:** shared history and alternative reconstruction are experimental; multipass/high-resolution modes cost more and do not guarantee better quality. V8 retains its supported 4K/50% single-pass DX12/Vulkan inputs and is unavailable in DX11. API/input/FG compatibility depends on the active path. A menu or successful API call alone does not establish gameplay compatibility.

SHA256SUMS.txt identifies the exact release ZIP.
