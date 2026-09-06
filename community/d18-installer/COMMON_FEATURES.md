# D18 common features / 通用功能（2026-09-06）

Port baseline: original `10b6a711`, including its `43476a84` retirement changes.
Source features adapted from Onimusha `6bead2c5`; not a whole-branch cherry-pick.
No Onimusha NGX-only routing, whitelist, polling-only input, queue-age policy,
REFramework storage mirroring or integrated post-NR sharpening is imported.

## Controls with a significant effect / 显著影响项

| Control | Effect / 作用 | Default / 条件 |
|---|---|---|
| UI / NR hotkey | Change menu / NR toggle keys live; save for next launch / 实时修改并保存 | Existing bindings preserved |
| Diagnostics | Summary: lifecycle/failures; Trace: frame contracts / 元数据而非图像 | Off |
| Experimental low-ratio compose | Opt into ratio-aware RGB low-frequency transfer / 新合成总开关 | Off; DX12, ratio < 1, Preserve high frequencies |
| Guided reconstruction | Use full-resolution SR to guide network-cell reconstruction / SR 引导重建 | Off; experimental compose required |
| Area + gain-first | Average exact 2×2 physical footprints, then reconstruct per-cell gain / 先算增益再插值 | Off; guided + exact 50% only |
| Frequency radius | Changes the low-pass band split / 改变频带分界 | 2 network pixels; experimental low-pass mode |
| Luma / Chroma trust | Weight brightness / colour contribution independently / 亮度与色度权重 | 1 / 1; experimental compose |
| Catmull-Rom input | Sharper input kernel; possible ringing/temporal trade-offs / 输入核 A/B | Off; Custom prefilter + internal scaling required |
| Custom model Color prefilter | Mitchell when Catmull is off; overrides linear Color sampler / 避免两次低通 | Existing configuration |
| RCAS / DA | Original OptiScaler sharpening route / 原版锐化路径 | Existing configuration; no second compose sharpener |

The experiments do not recover guaranteed 100%-ratio model detail or colour. Onimusha A/B results
showed trade-offs, not a universally better preset. Keep the experiments off for an original-rendering
baseline. Physical WorkingScale is intentionally disabled; the old INI key is retained but ignored
by the D18 DX12 backend. Original 100% arithmetic is retained in source; GPU bitwise equality has
not been measured across games or drivers.

实验功能不承诺还原 100% 的模型纹理、色彩或时域稳定性。不是把鬼武者测试结果当成跨游戏验收。
DX11／Vulkan 桥接、RE Engine 扩展与 AMD 支持仍属后续兼容工作。

## Diagnostics and capture / 诊断与捕获

- Off does not open the diagnostics file. Enabling diagnostics lazily creates/maps a
  `D18Diagnostics.ring` beside the game executable: 4096 metadata records, schema 1.
  Initial setup can perform file I/O; normal writes are bounded memory-mapped records.
  OS paging is possible; no zero-overhead claim is made.
- Summary records feature creation, failures and skips; Trace adds per-frame contract/evaluate/
  composition-recorded events. A successful compose record means CPU command recording,
  not proof of GPU completion. Unknown queue/fence fields stay unknown.
- The latest ring belongs to one process session and overwrites old slots. Copy it after exiting
  the game for a stable offline snapshot. No automatic upload or image capture.
- Explicit `Capture 8 frames` / `dlssnr-capture.trigger` records four streams into
  `dlssnr-capture` beside the proxy. Before/after share the game domain; model streams are the
  Runtime-visible proxy domain, not raw network tensors. Their physical dimensions can remain 4K
  even with a 1080p internal network.
- Only the existing post-ExecuteCommandLists observer may signal capture completion fences.
  If a game's submission route is not observed, capture stays pending rather than reading unsafe
  memory. No new rendering hook or speculative queue fallback is installed for capture.
- At most 8 four-stream frames are allocated; this can consume substantial readback memory.
  Copy out a completed capture before requesting another. Scene/ratio changes within a batch
  invalidate controlled comparisons. Resolution/format changes stop recording the current contract.
- Native Vulkan is not connected to this diagnostics/capture path. The shared SPIR-V shader was
  rebuilt for the constant-buffer layout; that is not a Vulkan compatibility validation.

同域捕获仅能分开观察合成与 Runtime 可见边界，不能单独把滤波、内部重采样和网络推理损失拆开。
1080p 域能量比接近 1 不等于真实纹理相同，也不证明损失完全来自重建。

## Offline checks

From the repository root:

```powershell
python -m unittest discover -s tools/D18 -p 'test_*.py'
powershell -NoProfile -ExecutionPolicy Bypass -File community/d18-installer/tests/Test-UiToggleKey.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File community/d18-installer/tests/Test-UiKeyInstall.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File community/d18-installer/tests/Test-RuntimeClassification.ps1
python tools/D18/summarize-diagnostics.py PATH/TO/D18Diagnostics.ring --json summary.json --markdown summary.md
python tools/D18/analyze-capture-frequency.py --help
python tools/D18/compare-capture-colour.py --help
```

Frequency/colour tools need NumPy and SciPy; diagnostics summary uses Python's standard library.
The installer integration test uses synthetic payload/Runtime bytes, not game or NVIDIA binaries.
`tools/D18/test-capture-submission.cpp` is a real two-queue DX12/WARP test
(MSVC C++20; link d3d12.lib and dxgi.lib).

Validation here: Release/x64 build; DXIL and SPIR-V shader compilation; 32 Python tests; PowerShell
key parsing, fresh install, full-INI upgrade, uninstall and default-key checks; 9 Runtime-classification
fixtures; DX12/WARP submission/fence checks. The user subsequently reported a successful Wuthering
Waves smoke test of the exact core shipped in 0.1.3. Individual capture paths, restart persistence,
performance, image quality and cross-game behaviour are not thereby fully validated.
These features ship in D18 0.1.3; older packages/tags and the separate Onimusha package remain unchanged.

## REFramework installation

Known RE games use a matched nightly when available. Otherwise choose Latest (unverified for that game) or Manual. The only download source is https://github.com/praydog/REFramework-nightly . Only non-VR dinput8.dll is extracted after official SHA256 verification. Existing REF is retained; unrecognized loaders require confirmation. REF uses PgDn; D18 defaults to Insert. Both layouts read/write the game-root OptiScaler.ini.

Options: `-REFramework Auto|Recommended|Latest|Existing|Manual`; `-REEngine` selects the RE installation layout for an unknown executable without claiming rendering compatibility. Normal installations do not download REF.
