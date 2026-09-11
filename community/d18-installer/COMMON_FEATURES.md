# D18 0.1.4 功能 / Features

DX11、DX12、Vulkan 共用 D18 面板；开关、当前状态和网络比例位于顶部。选项按后端能力与当前条件启用，灰显时显示原因。
DX11, DX12 and Vulkan share the D18 panel. Enable, status and network ratio stay at the top. Controls follow backend capabilities and display their availability conditions.

| 分组 / Group | 用途 / Purpose |
| --- | --- |
| 网络比例 / Network ratio | 调整内部网络计算分辨率，保留 Color/Output 输出尺寸；减小比例可能改变细节与稳定性。 / Changes the internal network size while retaining Color/Output dimensions; smaller ratios can affect detail and stability. |
| 清晰度 / Clarity | 预滤波、采样与细节传递。 / Prefiltering, sampling and detail transfer. |
| 颜色 / Colour | 颜色强度、高光编码及仅传递模型颜色变化；按可用后端显示。 / Colour strength, highlight encoding and transfer of model colour changes, where available. |
| 人物与风格 / Characters and style | 人物与风格参数；DX11 细节闪烁修正在仁王 2 默认开启，其他游戏按需手动开启。 / Character/style parameters; DX11 detail-flicker correction defaults on for Nioh 2 and is opt-in elsewhere. |
| 对比与诊断 / Comparison and diagnostics | 旁路、对比与分阶段采样；采样入口按 API 区分。 / Bypass, comparison and staged captures with API-specific entry points. |

每个 NR 选项旁有用途短提示与进一步说明。中英文切换即时生效，保存设置后按游戏保留。新装 Insert 打开界面，原生 DX11/Vulkan 的 PgUp 切换 NR；升级保留已有热键。
NR controls include short hints and further explanations. Language changes apply immediately and persist per game after Save Settings. Fresh installs use Insert for the menu and PgUp for native DX11/Vulkan NR; upgrades retain existing bindings.

诊断默认关闭且有界。DX12 的 Capture 8 frames 与原生后端的分阶段捕获入口不同；缺少提交/完成观测不能算作成功采样。API 成功和计数器增长不能代替画质验收。
Diagnostics are default-off and bounded. DX12 Capture 8 frames and native staged captures have separate entry points. Missing submission/completion evidence is not a completed capture. API success and counters do not establish visual correctness.

OptiScaler DLSS FG 的输入、输出和启用状态可一次设置，保存后按提示统一重启。请求倍率与实际观测状态分开显示。插件 2× 已在测试游戏中可用；DX11 FG 因游戏而异，更高倍率可能出现拖影或不对齐。
Configure OptiScaler DLSS FG input, output and enable state together, save, then restart once when requested. Requested and observed multiplier states are separate. Plugin 2× has worked in tested games; DX11 FG varies by game and higher multipliers can ghost or misalign.

资源分配失败时会报告 NR 降级原因；显卡设备丢失仍可能需要重启游戏。
Allocation failures report the NR fallback reason; device loss may still require restarting the game.

[中文安装说明](README_CN.md) · [English installation](README.md) · [0.1.6 cumulative release notes](RELEASE_NOTES_0.1.6.md)

## 0.1.6 累计更新 / Cumulative update

包含 0.1.5 私测整合：DX11 等待/复用/FG/监控、DX12 退休维护、Vulkan 实验 FG/NR 比例与计时、星空输出契约与状态显示；并包含后续共享桥接/注册表/资源契约、可选 DX12 Hybrid、状态快照及可靠 Capture 写入。详见 [完整更新说明](RELEASE_NOTES_0.1.6.md)。

Includes the private 0.1.5 integration: DX11 completion/reuse/FG/monitoring, DX12 retirement maintenance, experimental Vulkan FG and NR ratio/timing, and Starfield output/status fixes. Also includes subsequent shared bridge/registry/resource contracts, optional DX12 Hybrid, status snapshots and checked capture writes. See the [full release notes](RELEASE_NOTES_0.1.6.md).

Classic 为默认；Hybrid 仅在支持的共享 DX12 HDR NR 路径可用。它可能改变颜色和高光观感，不保证消除颗粒。Vulkan FG 默认关闭，需要配套用户运行库；BG3 验证不等于所有 Vulkan 游戏兼容。

Classic is the default; Hybrid is available on the supported shared DX12 HDR NR path. It can change colour/highlight appearance and does not guarantee grain removal. Vulkan FG is disabled by default and requires matching user runtimes; BG3 testing does not establish compatibility with every Vulkan game.
