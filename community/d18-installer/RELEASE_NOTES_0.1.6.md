# DLSSNR D18 0.1.6

## 中文

**本版是相对公开版 0.1.4a 的累计更新，包含 0.1.5 私测阶段的全部整合修复，以及之后完成的 HDR、资源安全和采集改进。** 0.1.5 未公开发布；使用 0.1.4a 的用户可直接升级到 0.1.6。

### 本版更新

- **DX11 稳定性与性能监控：** 整合 GPU 完成等待、共享资源复用、FG 呈现及 Reflex 标记路由修复，改善性能统计和状态显示的一致性。这里不承诺所有游戏获得固定幅度的帧率提升。
- **DX12 资源安全：** 提交观察器在 NR 记录工作前就绪；旧 NR 资源只在真实提交和对应 GPU fence 条件满足后释放。加强热重建、队列清理、分配失败回退，以及 Depth/MV 格式、尺寸和采样契约检查。
- **跨 API 资源交接：** 加固 DX11/Vulkan 转 DX12 路径中的可选 Exposure/Reactive 输入交接，并修复共享 NGX feature 注册表的查找和生命周期处理。这些检查按实际调用路径生效，无需给每个游戏写一份实现。
- **星空与 SR 过渡阶段：** 修复 SR 输出参数与实际输出契约不一致时 NR 只覆盖部分画面的问题；SR 本帧未实际产出结果时不再误执行 NR。保留子区域、padding、RR 和已有 RE 引擎保护。
- **Vulkan NR：** 整合完整的网络比例处理，保留全尺寸 Color/Output；增加有界、非阻塞的 NR GPU 耗时显示。该数值是 NR 阶段耗时，不是整机输入延迟。
- **实验性 Vulkan FG：** 整合 BG3 已测试的 FG/NR 配合、倍率切换和相关生命周期、输入及呈现修复。此路线默认关闭，需要用户自行提供匹配的运行库；缺少真实相机参数时存在近似处理，仍不能保证所有 Vulkan 游戏兼容。
- **可选 HDR 高光编码：** 共享 DX12 NR 路径新增 Classic / Hybrid 选择。**Classic 仍为默认**；Hybrid 在已测场景中减少模型输入高光触顶，最终颜色和观感可能不同。它是可选方案，不保证消除颗粒或对所有场景都更好。
- **界面与诊断：** Feature 18 / NR frame 状态更稳定；共享 DX12 NR 状态通过完整快照提供给 UI。修复 RR 来源显示和 Capture 提交观察，扩充同帧四流采集及元数据；检查文件写入结果，避免未完整保存却提示成功。新事件已接入共享离线汇总工具，诊断保持默认关闭并有界。

### 安装与升级

下载 **`DLSSNR_D18_0.1.6.zip`**，完整解压，退出游戏后运行 `Install-D18.bat`。升级保留已有设置和代理选择，并创建回退备份；无需为了升级先手动删除旧文件。

本包**不附带 NVIDIA NR/SR/RR/FG 或 Streamline 运行库**。请按安装说明自行准备兼容的 NR runtime；安装器验证并在本地应用 D18 补丁。保留游戏自带 DLSS/FG/Streamline 文件，不用其他游戏的文件覆盖它们。DX11 的完整 runtime 哈希要求见包内说明。`nvngx.dll_dlssnr.dll` 是本项目转发器，不是 NVIDIA NR runtime。

默认网络比例仍为 50%，Classic 默认不变。已有用户选择通过升级保留；先使用原配置，再按个人观感选择 Hybrid。Multipass、高于输入尺寸的 NR 超采样及新的 NVFP4 路线未在本版新增。

**燕云十六声：** 安装到实际启动的 `yysls.exe` 所在目录，可能为 `Engine\Binaries\Win64r` 或 `Engine\Binaries\Win64rh`。不要仅凭文件夹名称判断；同时向两个目录安装的兼容性尚未确认。

### 验证范围

本轮最终核心已在 Cyberpunk 2077 的 DX12 RR 路径完成 50%→100%→50% 切换、继续运行及完整 8 帧 Capture 检查；Classic/Hybrid 的阶段对比也已完成。星空、BG3 Vulkan 和 DX11 的此前验收及修复完整继承。离线检查覆盖资源策略、状态快照并发、WARP 采集写盘及安装器。

这些结果不代表全部游戏、显卡、驱动、长时间运行或 GPU 故障场景均已验证。最新核心没有重新遍历全部 API/游戏组合；Vulkan FG 继续按实验功能提供。较高 FG 倍率的兼容性和画面稳定性因游戏而异。

<details>
<summary>BG3 可选启动排障：Larian 启动器点“游玩”无响应</summary>

仅当日志明确显示游戏启动前的 Mod 检查因 `steam_manifest.xml` 被占用而失败：保持 Steam 已运行并登录拥有游戏的账号，在 BG3 `bin` 目录新增 `steam_appid.txt`，内容仅为 `1086940`，以该目录为工作目录启动 `bg3.exe`（Vulkan）。已有同名文件应先保留；删除本次新增文件即可撤回。

这是此前本地验证的 BG3 专用启动方式，不修复游戏内 FG 错误，也不作为通用安装动作。安装包不包含 `steam_appid.txt`。

</details>

---

## English

**This is a cumulative update from public release 0.1.4a. It includes the full integration from the private 0.1.5 test build, followed by the HDR, resource-safety and capture improvements.** Version 0.1.5 was not publicly released; 0.1.4a users can upgrade directly to 0.1.6.

### Changes

- **DX11 stability and monitoring:** integrated GPU-completion handling, shared-resource reuse, FG presentation and Reflex marker-routing fixes, with more consistent performance and status reporting. No fixed FPS improvement is promised.
- **DX12 resource safety:** submission observation is established before recording NR work. Old NR resources are released only after actual submission and the corresponding GPU fence conditions. Hot rebuilds, queue cleanup, allocation fallback and Depth/MV format, size and sample contracts are hardened.
- **Cross-API handoff:** safer optional Exposure/Reactive inputs in DX11/Vulkan-to-DX12 bridges, plus shared NGX feature-registry lookup and lifecycle fixes. These changes follow the applicable code path without requiring separate implementations for each game.
- **Starfield and SR transitions:** fixed partial NR coverage when reported SR output parameters differ from the actual output contract. NR no longer runs when SR did not produce an output for the current evaluation. Subrect, padding, RR and existing RE Engine safeguards remain.
- **Vulkan NR:** integrated complete internal-ratio handling while preserving full-size Color/Output, with bounded, nonblocking NR GPU timing. This measures the NR stage, not end-to-end PC latency.
- **Experimental Vulkan FG:** integrated the FG/NR combination and multiplier switching tested in BG3, with lifecycle, input and presentation fixes. Disabled by default and dependent on matching user-supplied runtimes. Camera inputs can be approximated when unavailable; broad Vulkan compatibility is not established.
- **Optional HDR highlight encoding:** Classic / Hybrid selection on the shared DX12 NR path. **Classic remains the default.** Hybrid reduced model-input highlight saturation in the measured scene, but can change colour and appearance. It is optional and is not a universal grain-removal or quality improvement.
- **UI and diagnostics:** steadier Feature 18 / NR frame indicators and complete snapshots for shared DX12 NR UI status. Fixed RR source reporting and capture submission observation, expanded paired four-stream capture metadata, and checked file writes so incomplete captures cannot report success. New events are covered by the shared offline tools; diagnostics remain default-off and bounded.

### Install or upgrade

Download **`DLSSNR_D18_0.1.6.zip`**, extract it completely, close the game and run `Install-D18.bat`. Upgrades retain existing settings and proxy selection, with rollback backups. Do not copy the `payload` directory directly into a game.

**NVIDIA NR/SR/RR/FG and Streamline runtimes are not included.** Supply a compatible NR runtime as described in the installer documentation; the installer validates and patches it locally. Preserve the game's existing DLSS/FG/Streamline files. DX11 requires the documented full runtime hash. The bundled `nvngx.dll_dlssnr.dll` is the project's forwarder, not the NVIDIA NR runtime.

The default network ratio remains 50% and Classic remains the default. Existing preferences are preserved on upgrade. Multipass, NR supersampling above input resolution and a new NVFP4 path are not added in this release.

**Where Winds Meet:** use the directory of the `yysls.exe` that actually launches, which may be `Engine\Binaries\Win64r` or `Engine\Binaries\Win64rh`. Installing into both directories has not been established as compatible.

### Validation scope

The final core was exercised in Cyberpunk 2077 DX12 RR with 50%→100%→50% switching, continued rendering and a complete eight-frame capture. Earlier Classic/Hybrid comparisons and the Starfield, BG3 Vulkan and DX11 validation stages are retained. Offline checks cover resource policies, concurrent status snapshots, WARP capture writes and installer behavior.

This is not validation of every game, GPU, driver, long-duration session or GPU fault. The final core has not been retested across every API/game combination. Vulkan FG remains experimental; higher FG multipliers can vary in compatibility and visual stability.

<details>
<summary>Optional BG3 launcher workaround</summary>

Only when the launcher log identifies a locked `steam_manifest.xml` during the pre-launch mod check: keep Steam running and signed in to the owning account, add `bin/steam_appid.txt` containing only `1086940`, and launch `bg3.exe` for Vulkan with `bin` as the working directory. Preserve any existing file; remove only the newly added file to revert. This previously tested launcher workaround does not fix in-game FG errors. It is not a universal installer action, and the package does not include `steam_appid.txt`.

</details>
