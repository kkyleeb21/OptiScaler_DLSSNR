# D18 0.1.1 RE Engine Patch — 鬼武者 only / Onimusha ONLY（预览版）

**DLL 文件名：本 Mod 使用 `d3d12.dll`，安装器会同步到游戏根目录及 `_storage_`。
请勿自行改名，也不要让 ReShade 或其他 Mod 覆盖这两处同名文件。不同 DLL 文件名并不保证联用兼容。**

2026-09-06 收尾更新源码：诊断、菜单滚动、热键入口、首次安装 UI 键选择和低比例合成实验。
本页描述本分支源码；旧 RC3 tag 和 GitHub FlickerFix 安装包不会因源码推送自动更新。

仅适用于《鬼武者：剑之道》（Onimusha: Way of the Sword，`OnimushaWotS.exe`）。不是通用 RE Engine 补丁，请勿安装到生化危机等其他游戏。

## 功能

- 保留游戏原生 DLSS SR，在其输出后执行 D18 NR；Color / Output 保持全分辨率，只缩放网络内部工作尺寸。
- 修复本机测试中原生 FG 切换后 NR 停止的问题；已测试 FG 开、关和多次切换。
- 恢复 D18 菜单控件，支持点击、拖动与滚动；本机用户已确认滚动和诊断入口出现。
- 左栏 Hotkeys 可修改 UI hotkey / NR hotkey，点击 Save Settings 保存；首次安装可选择 UI 键。
- 保留低比例输入核和重建实验，默认关闭新增实验，不宣称 50% 与 100% 画质等同。
- 安装器检查前置文件，备份被覆盖内容；支持升级及卸载恢复。不捆绑或下载 NVIDIA Runtime / REFramework。

## 安装

1. 完全退出游戏与 CrashReport。先安装 Nexus Mods 的**鬼武者专用 REFramework**，确认 `dinput8.dll` 位于 `OnimushaWotS.exe` 旁；建议先单独运行一次验证 REF 正常，再退出。
2. 自行准备原版或适配自己显卡的兼容版 DLSS5 Runtime，文件名必须为 **`nvngx_dlssnr.dll`**，放在同一目录。兼容版的 20/30/40/50 系支持范围由其提供者决定，本包不承诺全代际实测。
3. 解压本包，双击 `Install-D18.bat`，选择游戏目录。专用包自动使用 **`d3d12.dll`**，不要自行改名。若发现其他代理 DLL，安装器会停止，请先备份并处理已有加载链。
4. 进游戏启用原生 DLSS SR。新安装默认 NR 开启、ratio **100%**；已有配置保留个人 NR 参数，只更新必要兼容项。
5. 首次安装且没有已有配置时，输入 UI 单键名称（如 F10、Home），回车默认 Insert；升级保留原值。
   使用该键打开 D18。安装器询问是否将 REF 菜单键改为 **PgDn**，与 UI 键分开设置；
   若新 UI 键选 PgDn，则跳过 REF 的自动 PgDn 重设。已有 REF 键仍需自行避免冲突。
   “启动时隐藏 REF 菜单并记忆菜单状态”是独立选项，默认不启用。只修改所选字段并备份原配置，
   不替换 REF 根目录 DLL 或脚本。卸载仅恢复仍等于安装值的 REF 字段，保留用户后续修改。

无人值守：`-Yes` 首次 UI 默认 Insert，`-UiToggleKey F10` 可指定；已有配置忽略该参数并保留原值。
`-Yes` 默认将 REF 键改为 PgDn、不隐藏菜单；`-SkipRefHotkey` 保留 REF 键，`-HideRefMenu` 单独启用隐藏。
若在 REF 菜单打开时退出，记忆状态可能让它下次再次打开；这不是永久禁止 UI 绘制。

## 哪些功能会显著影响效果

| UI 功能 | 作用与使用边界 |
|---|---|
| Enable Neural Rendering / NR hotkey | 开关 NR，便于和游戏原生 SR 对照；UI hotkey 只开关菜单，不开关 NR。 |
| Network ratio | 首次默认 100%。50% 降低网络内部尺寸，通常减轻推理负担，但细节、色彩、明暗可能改变；Color/Output 仍全分辨率。实际开销按游戏和 GPU 测量。 |
| Detail strength | 控制模型编辑参与强度，不是普通锐化。提高可能增强明暗/滤镜感，也可能使人物变软或增加运动漂移；不建议把 2.0 当通用最佳值。 |
| Colour strength | 控制模型颜色参与程度，不负责恢复丢失纹理；本机调节没有解决人物模糊。 |
| Preserve original high frequencies | 低 ratio 时以全分辨率 SR 保留细节，再传递模型低频编辑；可能减弱模型的中尺度塑形。 |
| Guided network reconstruction | 实验：用 SR 亮度引导网络格点重建。部分本机观察中轮廓改善，但不是保证恢复 100% 颜色；关闭使用 ratio-aware 低通。默认关闭。 |
| Area + gain-first reconstruction | 实验：仅精确 50%，先平均 2x2 格点、求 gain 后插值。需要 Guided 和高频保留开启。本轮中频能量提高，但短序列时域变化也提高；默认关闭，不视作稳定性修复。 |
| Custom model Color prefilter | 启用自定义输入代理，默认核 Mitchell；关闭由 Runtime 处理。启用时强制模型 Color POINT，避免叠加自定义低通与 LINEAR 输入。只在内部低比例时生效。 |
| Catmull-Rom input kernel (A/B) | 必须同时开启 Custom prefilter；否则无效。更换输入代理核，保留防振铃限制。本轮模型边界能量提高，但最终画质无明确优势；默认关闭。 |
| Linear model Color input / output sampling | 更换 Runtime 输入/输出采样方式，可能改变清晰度、锯齿和纹理；输入 LINEAR 会被启用的自定义代理覆盖。不要同时改变多个采样选项来做 A/B。 |
| Frequency radius / Luma trust / Chroma trust | 低 ratio 合成校准。半径影响非 guided 低通路径；trust 调节对应编辑力度。Luma trust 较高可能增强塑形，也有轻微漂移反馈。不是模型质量升级。 |
| Motion-adaptive low-frequency transfer | 运动/输入差异较大时降低编辑传递；本机未观察到明显改善，不能承诺解决脸部运动模糊。 |
| Sharpening | 鬼武者 native-SR 路径采用现有 compose 内的有界全分辨率高频残差，效果较轻，不是完整 RCAS/DA 独立 pass，也不能补回模型未保留的纹理。 |
| Model tuning（Style / Intensity / Local / Skin） | 改变模型参数，部分修改可能触发 feature 重建；不是所有参数在该 Runtime 都有明显可见效果。Skin 参数未证实是低 ratio 磨皮的根因。 |
| Diagnostics Off / Summary / Trace | 默认 Off。用于定位跳过/失败及输入契约，不是画质开关；状态记录不等于 GPU 完成证明。Trace 更详细，也增加记录开销。 |
| Capture 8 frames | 显式保存图像数据用于离线分析；4K 四流约 1 GB/次，可能造成卡顿，重复捕获覆盖工作捕获槽，请先归档。普通游戏不需要启用。 |

建议先用 100% 默认参数；选择 50% 后按自身画面需求决定是否使用 guided。实验选项是保留的
对照工具，不是必须全开的增强包。当前实验结论见同目录 `ONIMUSHA_AB_RESULTS_CN.md`。

**别混淆两个文件：** `nvngx_dlssnr.dll` 是用户提供的模型 Runtime；`nvngx.dll_dlssnr.dll` 是本包提供的 D18 转发器。

Runtime 检查：`VERIFIED` 表示已识别参考版本；`UNVERIFIED_COMPATIBLE` 表示未经验证但未发现补丁字段冲突，可能可用；`CONFLICT` 表示关键字段冲突，停止且不修改原文件；`ALREADY_PATCHED` 表示全部所需补丁已存在，不重复修改。不用全文件白名单拦截未知兼容版，但“可安装”不等于“运行兼容已验证”。

已验证参考原始 Runtime SHA-256：`E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`；其 D18 补丁后指纹：`CCAC112995922D8BD2C5F2D0DCB7A6756B7806D3D868692ACB9AF64D4AEF7414`。未知版出问题时，请先换用参考版复现。

安装器同步必要文件到根目录与 `_storage_`，并备份相应旧文件；卸载请运行 `Uninstall-D18.bat`。保留安装状态文件及 `D18_Backups`，否则无法完整自动恢复。

## 局限性

- **ReShade 联用：已收到崩溃反馈，尚未确认根因或完成兼容验收。** 本包核心固定名为 d3d12.dll，
  不可让另一个加载器覆盖同名文件；没有同名文件也不代表钩子/加载顺序一定兼容。
  代码存在 ReShade64.dll 加载选项，但不将其列为鬼武者已验证的解决方案。反馈请附 ReShade 版本、
  DLL 文件名、崩溃阶段和 OptiScaler/ReShade 日志；不要只靠反复改名判断根因。

- 目前仅有鬼武者本机功能验证，不代表其他 RE 游戏、所有显卡或驱动均已确认兼容。安装器仅检查 REF 前置文件存在，不保证任意同名 DLL 就是正确 REF。
- **50% ratio 仍可能有细节、色彩及明暗偏差**，建议先用 100%；本轮重建和输入核 A/B 已收尾，未证明模型上限，也未达到与 100% 等同。
- FG 面板显示 **UNOBSERVED** 表示未观测游戏原生 FG 状态，不代表 FG 关闭或失败。
- 菜单点击、拖动和滚动已有本机用户反馈；热键 UI 的修改/保存重启/恢复默认尚待用户完整验收。原生 SR 的部分控制项是只读状态。
- 可能出现 GPU 枚举提示或 GPU 信息不准确；现有测试中未阻止 NR 执行。
- 不包含原生 Vulkan D18，也不宣称 DX11 / Vulkan、路径追踪 / RR 或其他游戏兼容。
- 本轮核心已构建并部署实机捕获，安装器首次键选择/升级保留/卸载通过隔离测试；这不等同于尚未制作的新发布 ZIP 已实机验收。

DXC 来自微软官方 [v1.8.2407 发布包](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2407)，随附许可证保留于 `Licenses`。本项目为非官方社区实验补丁。
