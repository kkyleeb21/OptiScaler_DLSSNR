# D18 0.1.1 RE Engine Patch — 鬼武者 only / Onimusha ONLY（预览版）

仅适用于《鬼武者：剑之道》（Onimusha: Way of the Sword，`OnimushaWotS.exe`）。不是通用 RE Engine 补丁，请勿安装到生化危机等其他游戏。

## 功能

- 保留游戏原生 DLSS SR，在其输出后执行 D18 NR；Color / Output 保持全分辨率，只缩放网络内部工作尺寸。
- 修复本机测试中原生 FG 切换后 NR 停止的问题；已测试 FG 开、关和多次切换。
- 恢复 D18 菜单控件，支持游戏未暂停时的菜单鼠标点击与拖动。Insert 打开 D18。
- 安装器检查前置文件，备份被覆盖内容；支持升级及卸载恢复。不捆绑或下载 NVIDIA Runtime / REFramework。

## 安装

1. 完全退出游戏与 CrashReport。先安装 Nexus Mods 的**鬼武者专用 REFramework**，确认 `dinput8.dll` 位于 `OnimushaWotS.exe` 旁；建议先单独运行一次验证 REF 正常，再退出。
2. 自行准备原版或适配自己显卡的兼容版 DLSS5 Runtime，文件名必须为 **`nvngx_dlssnr.dll`**，放在同一目录。兼容版的 20/30/40/50 系支持范围由其提供者决定，本包不承诺全代际实测。
3. 解压本包，双击 `Install-D18.bat`，选择游戏目录。专用包自动使用 **`d3d12.dll`**，不要自行改名。若发现其他代理 DLL，安装器会停止，请先备份并处理已有加载链。
4. 进游戏启用原生 DLSS SR。新安装默认 NR 开启、ratio **100%**；已有配置保留个人 NR 参数，只更新必要兼容项。
5. Insert 打开 D18。安装器默认询问是否将 REF 菜单键改为 **PgDn**（回车同意，可选择跳过）；“启动时隐藏 REF 菜单并记忆菜单状态”是独立选项，默认不启用。只修改所选字段，并备份原配置，不替换 REF 根目录 DLL 或脚本。卸载仅恢复仍等于安装值的字段，保留用户后续修改。

无人值守：`-Yes` 默认修改热键、不隐藏菜单；`-SkipRefHotkey` 保留热键，`-HideRefMenu` 单独启用隐藏。若在 REF 菜单打开时退出，记忆状态可能让它下次再次打开；这不是永久禁止 UI 绘制。

**别混淆两个文件：** `nvngx_dlssnr.dll` 是用户提供的模型 Runtime；`nvngx.dll_dlssnr.dll` 是本包提供的 D18 转发器。

Runtime 检查：`VERIFIED` 表示已识别参考版本；`UNVERIFIED_COMPATIBLE` 表示未经验证但未发现补丁字段冲突，可能可用；`CONFLICT` 表示关键字段冲突，停止且不修改原文件；`ALREADY_PATCHED` 表示全部所需补丁已存在，不重复修改。不用全文件白名单拦截未知兼容版，但“可安装”不等于“运行兼容已验证”。

已验证参考原始 Runtime SHA-256：`E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`；其 D18 补丁后指纹：`CCAC112995922D8BD2C5F2D0DCB7A6756B7806D3D868692ACB9AF64D4AEF7414`。未知版出问题时，请先换用参考版复现。

安装器同步必要文件到根目录与 `_storage_`，并备份相应旧文件；卸载请运行 `Uninstall-D18.bat`。保留安装状态文件及 `D18_Backups`，否则无法完整自动恢复。

## 局限性

- 目前仅有鬼武者本机功能验证，不代表其他 RE 游戏、所有显卡或驱动均已确认兼容。安装器仅检查 REF 前置文件存在，不保证任意同名 DLL 就是正确 REF。
- **50% ratio 有明显细节、色彩及明暗偏差**，建议先用 100%；画质与性能专项仍在研究。
- FG 面板显示 **UNOBSERVED** 表示未观测游戏原生 FG 状态，不代表 FG 关闭或失败。
- 菜单鼠标点击/拖动可用；滚轮与文字输入尚不完整。原生 SR 的部分控制项是只读状态。
- 可能出现 GPU 枚举提示或 GPU 信息不准确；现有测试中未阻止 NR 执行。
- 不包含原生 Vulkan D18，也不宣称 DX11 / Vulkan、路径追踪 / RR 或其他游戏兼容。
- 移除了测试机器的 SDK 绝对路径依赖，改用包内私有 DXC 反射组件。此发布候选重新构建后仍需一次实机冒烟验证，不把此前测试冒充新包验收。

DXC 来自微软官方 [v1.8.2407 发布包](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2407)，随附许可证保留于 `Licenses`。本项目为非官方社区实验补丁。
