# D18 0.1.2

- UI wheel scrolling fixes; editable UI/NR hotkeys with Save Settings. Fresh installations ask for the UI key; upgrades preserve your INI.
- Optional Off / Summary / Trace diagnostics with live status codes, bounded local records and bundled offline analysis tools.
- Four-stream DX12 capture with submission/fence-checked readback for safer comparisons.
- Optional low-ratio composition, SR-guided reconstruction, 50% gain-first reconstruction and Catmull-Rom input A/B; live frequency, luma and chroma controls.

Experiments and diagnostics are off by default. Original composition and RCAS/DA remain the baseline;
Color/Output stay full resolution. Catmull-Rom requires Custom model Color prefilter. No new model weights.

User smoke-tested in Wuthering Waves; offline regression checks passed. No new RE Engine, DX11,
Vulkan or AMD compatibility claim. Onimusha ONLY remains a separate package.
Generic installer defaults to `dxgi.dll` and offers other proxy names; Onimusha uses `d3d12.dll`.
NVIDIA Runtime is not included; supply your own compatible 310.8-based Runtime.

## 中文摘要

- 修复 UI 滚轮；菜单可修改 UI／NR 热键并保存，首次安装可选 UI 键，升级保留配置。
- 新增默认关闭的轻量诊断、实时状态码、本地记录及离线汇总工具。
- 四流捕获使用实际提交与 fence 完成检查，便于安全比较合成和模型边界。
- 保留可选低 ratio 合成、SR 引导重建、50% gain-first、Catmull-Rom 输入核，以及实时频带／亮度／色度控制。

实验默认关闭，不承诺还原 100% 画质；保留原版合成／锐化，全分辨率 Color／Output，不改模型权重。
鸣潮用户测试无问题，离线回归通过；不扩大跨游戏兼容声明。鬼武者专用包独立保留。
通用包默认 `dxgi.dll`，鬼武者包使用 `d3d12.dll`；本包不包含 NVIDIA Runtime。
