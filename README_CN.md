# OptiScaler DLSSNR D18

[English](README.md) · [下载 0.1.3](https://github.com/kkyleeb21/OptiScaler_DLSSNR/releases/tag/dlssnr-d18-v0.1.3) · [安装器说明](community/d18-installer/README_CN.md)

## D18 0.1.3

统一安装器新增 RE 引擎支持，需要 REFramework。安装器仅从 [REFramework-nightly](https://github.com/praydog/REFramework-nightly) 选择适配版本；无适配记录时可选择最新 nightly 或自行安装。REF 菜单 PgDn，D18 菜单 Insert，配置统一到游戏根目录。详见 [0.1.3 更新说明](community/d18-installer/RELEASE_NOTES_0.1.3.md)。

## 简介

D18 是基于 [OptiScaler DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR) 的社区分支，
在 DLSS SR 后运行 NVIDIA Neural Rendering（Feature 18），通过调节内部网络分辨率降低 GPU 开销。
SR 原图与最终输出保持显示分辨率。

4K 输出下，**50% 指宽、高各减半**：内部网络为 1920×1080，Color/Output 仍为 3840×2160。
较低比例以模型细节和稳定性换取速度，不改变游戏的 DLSS SR 质量档位。

D18 内部缩放及新增诊断、重建选项面向 DX12。显卡支持取决于所用 310.8 Runtime 与驱动。

## 功能与选项

下表对应**通用 0.1.3 D18 界面**。旧版和游戏专用包的选项可能不同。
点击 **Save Settings** 保存至 `OptiScaler.ini`。修改网络比例、Runtime 采样器或模型参数会
重建 Feature 18 并重置历史，可能短暂停顿。

### Neural Rendering

| 选项 | 调节内容 |
| --- | --- |
| Enable Neural Rendering | 开关 NR，不影响 DLSS SR 或 FG 开关。 |
| Internal network scaling | 缩小内部网络，保持 Color/Output 全分辨率。关闭时使用完整网络；此 DX12 路径忽略旧 `WorkingScale`。 |
| Network ratio / 50%、66.7%、75%、100% | 同时缩放网络宽高，按 Runtime 的 16×8 网格对齐。范围 0.5–1.0。 |
| Detail strength | 合成时传递的 NR 改动强度。0 不传递改动；提高数值加强效果，不等于增加真实细节。 |
| Colour strength | 色彩贡献：0 保留游戏色相、允许亮度变化；1 传递模型色彩。 |
| Preserve original high frequencies | 低比例下保留 SR 原始细节，传递 NR 较低频的变化。 |
| Motion-adaptive low-frequency transfer | 在运动与低频失配同时出现时减弱 NR 传递；不是时域累积。 |
| Motion protection starts / reaches full | 运动保护开始与达到最大时的像素运动阈值。 |
| Mismatch protection starts / reaches full | 失配保护开始与达到最大时的低频差异阈值。 |
| Linear network output sampling | Runtime 输出重建从 POINT 切为 LINEAR；可缓解方块感，也可能变软。 |
| Linear model Color input | Runtime Color 输入改用 LINEAR；启用自定义预滤波时被覆盖。 |
| Custom model Color prefilter | 在网络网格上构造相位对齐、带防振铃约束的 Mitchell–Netravali 输入。需内部缩放，启用后强制 Runtime Color 使用 POINT。 |
| Retry NR | 失败后重试。先查看错误与日志；重试不会修复不兼容的 Runtime。 |

<details>
<summary>Model tuning：模型参数</summary>

这些是没有公开文档的 Runtime 参数。说明表示预期用途，不保证特定视觉效果，
也不同于上方的最终合成强度。

| 选项 | 调节内容 |
| --- | --- |
| Model preset | 传入 `DLSSNR.Hint.Render.Preset`。已检查的 310.8 构建中，界面各档均落到同一套权重；不是 SR 预设或已验证性能档。 |
| NR style | Runtime 配置：0 Standard、1 Natural、2 Cinematic。名称来自社区；Style 也可能改变后处理分支。 |
| Intensity | Runtime 内部 NR 强度，区别于 Detail strength；可能改变内部处理路径。 |
| Local structure | Runtime 局部结构强度。 |
| Local tone | Runtime 局部色调强度。 |
| Skin structure | 皮肤结构强度；-1 跟随 Local structure。 |
| Auto skin mask | 请求 Runtime 自动识别皮肤遮罩。 |

</details>

<details>
<summary>低比例实验选项</summary>

这些实验默认**关闭**，保留原 D18 合成与既有 RCAS/DA 锐化路径。
新增频带/引导传递需 DX12、内部比例小于 1，并开启
**Experimental low-ratio compose** 和 **Preserve original high frequencies**。

| 选项 | 调节内容与依赖 |
| --- | --- |
| Experimental low-ratio compose | 开启实验合成路径，包括比例自适应 RGB 频带分离。 |
| Enlargement：Classic / Matched residual | Classic 使用采样后的模型图；Matched residual 将模型改动转移到全分辨率代理图。用于实验低比例路径。 |
| Guided network reconstruction | 用全分辨率 SR 亮度引导网络格点间重建，限制改动跨越物体边缘。 |
| Area + gain-first reconstruction (50% A/B) | 对 2×2 足迹取均值，先计算格点增益再插值。需引导重建，且网络比例恰为 50%。 |
| Frequency radius | 以网络像素计的低通半径，调节实验低通路径的 SR/模型频带分界；不调节引导路径。 |
| Luma trust / Chroma trust | 分别调节实验合成中模型亮度/色彩改动的权重；提高权重可能放大不稳定。 |
| Catmull-Rom input kernel (A/B) | 将自定义预滤波的 Mitchell 核换为 Catmull-Rom。需内部缩放与 Custom model Color prefilter；不依赖实验合成总开关。 |

</details>

<details>
<summary>曝光、对比与诊断</summary>

| 选项 | 调节内容 |
| --- | --- |
| Use game exposure | 使用游戏可提供的曝光信息归一化线性 Color 输入。 |
| Paper white / Paper white (x exposure) | 调节输入白点缩放；启用游戏曝光时乘在曝光推导值上。会影响最终图像，不只影响预览。 |
| Highlight guard | 限制相对亮度变化；当前合成同时约束变亮与变暗。 |
| Debug view | Off、输入 Proxy、Raw model output、Difference ×20。代理图/模型图是 Runtime 可见图像，不是隐藏网络张量。 |
| Compare | 关闭、SR-before 与 composed-after 左右对比，或划线对比。 |
| Swap sides / Label the sides / Label size | 交换左右、显示标签、调节标签大小。 |
| Zoom / Wipe split | 左右对比放大倍率 / 划线位置。 |
| Diagnostic mode | Off：不写诊断环形记录；Summary：生命周期、失败、跳过；Trace：额外记录逐帧契约与处理事件。 |
| Capture 8 frames | 将 SR-before、composed-after、model-input、model-output 保存至代理 DLL 旁的 `dlssnr-capture`。需观测到 DX12 提交及 GPU fence 完成。 |

诊断写入游戏可执行文件旁的限容量 `D18Diagnostics.ring`，不自动上传或抓图。
手动捕获会占用读回内存并覆盖上一批；同批捕获期间保持场景与设置不变。
[分析工具与事件含义](community/d18-installer/COMMON_FEATURES.md#diagnostics-and-capture--诊断与捕获)。

</details>

<details>
<summary>DLSS SR、FG、锐化与界面</summary>

| 选项 | 调节内容 |
| --- | --- |
| Use native NVIDIA DLSS SR | 开启 OptiScaler 原生 DLSS 后端；未覆盖时由游戏提供质量与分辨率。 |
| Override render preset / Preset / Apply SR | 选择 DLSS SR 预设并重建后端应用；区别于 NR Model preset。 |
| Enable OptiScaler FG route | 开关可用的 OptiScaler 管理型 FG 路径；游戏原生 FG 可能只能在游戏设置中控制。 |
| MFG | 选择当前 FG 提供者支持的倍率。 |
| Provider debug overlay / Nukem debug view | 显示对应 FG 提供者支持的调试画面。 |
| Override game sharpness / Sharpness | 用指定强度覆盖游戏传入值。 |
| Enable OptiScaler sharpening (RCAS/DA) | 开启既有的超分后锐化；不是鬼武者专用的集成式 post-NR 锐化。 |
| RCAS / Depth Aware (RCAS) / Depth Aware (DAS) | 选择对比度自适应、深度感知 RCAS、或深度感知方向性亮度锐化。 |
| Contrast control / Contrast | 开启并调节 RCAS 对比度扩展。 |
| Clamp output | 限制深度感知锐化输出。 |
| Depth bias / Depth scale / Reset depth values | 校准深度边缘敏感度；Reset 恢复自动值。 |
| Enable Motion Adaptive Sharpness | 随运动调节锐化。 |
| Motion sharpness / Motion threshold / Motion range | 最大强度增减、开始阈值与渐变范围；负强度减弱运动中的锐化。 |
| MAS debug view / DA + MAS debug view | 显示运动/深度感知锐化的调试视图。 |
| UI hotkey / NR hotkey | 菜单与 NR 独立热键，默认 Insert / 未绑定。点击后按单键；Escape 取消、Backspace 解绑、R 恢复默认。 |
| UI scale / Auto scale | 手动/自动界面缩放；拖右下角调整窗口大小，面板支持滚轮。 |
| Save Settings / Close | 保存配置 / 关闭菜单，不关闭 NR。 |

状态卡显示观测到的 SR/FG/NR 活动。**UNOBSERVED** 表示未可靠观测，不等于关闭。
合成记录写入成功也不单独证明 GPU 已执行完成。

</details>

## 与原版的区别

这里的原版指 **D18 所基于的 OptiScaler DLSSNR 基线**，不代表后续每个上游版本。

| 项目 | 原基线 | D18 |
| --- | --- | --- |
| 降低 NR 开销 | `WorkingScale` 物理缩放暂存图 | 独立二维内部网络缩放，Color/Output 保持全分辨率 |
| 采样 | 既有 Runtime 输入/输出路径 | 独立 POINT/LINEAR 开关，可选网格对齐输入预滤波 |
| 合成 | 既有 NR 合成 | 增加 SR 高频保留、运动/失配保护与可选重建实验 |
| UI 与诊断 | 通用 OptiScaler 控件 | 集中 SR/FG/NR 面板、可编辑热键、事件诊断与四流捕获 |
| 安装 | 上游安装流程 | 带字段校验的 Runtime 补丁、文件清单、备份、受管理升级与卸载 |

D18 修改集成、采样和合成，不提供重新训练的权重。
感谢 [OptiScaler](https://github.com/optiscaler/OptiScaler)、
[Dagherbou / OptiScaler DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR)
及提供基础色彩合成工作的 [RenoDX](https://github.com/clshortfuse/renodx)。
代码采用 [GPL-3.0](LICENSE)，安装包附[第三方声明](community/d18-installer/THIRD_PARTY_NOTICES.md)。

## 安装、升级与卸载

1. 完全退出游戏，解压完整 [D18 0.1.3 ZIP](https://github.com/kkyleeb21/OptiScaler_DLSSNR/releases/tag/dlssnr-d18-v0.1.3)。
2. 准备**适配显卡与驱动、基于 310.8 的 `nvngx_dlssnr.dll`**，放在游戏可执行文件旁。
   安装包不含此文件；不要与随包提供的 `nvngx.dll_dlssnr.dll` 转发器混淆。
   下载文件若使用描述性名称，需要改名。
3. 运行 `Install-D18.bat`，选择游戏可执行文件目录与代理名，通用默认值为 `dxgi.dll`。
   首次安装会询问 UI 键，回车保留 Insert；也可按提示另选 Runtime 文件。
4. 进游戏启用 DLSS SR 路径，打开 D18，确认 **NR Active**。
   以 100% 作为画质参考，再比较低比例。通用包默认 50% 加自定义预滤波，是性能起点，不是通用画质推荐。

**鬼武者：**使用专用安装器与指定鬼武者 REFramework 构建。专用包管理 `d3d12.dll`
和 `_storage_` 镜像，不应换成通用代理/配置；前置要求以该包 README 为准。

**升级：**重新运行安装器。受管理替换会先验证新文件，保留备份与已有完整 INI；
升级不会重置个人参数。

**卸载：**退出游戏，运行 `Uninstall-D18.bat`，选择相同目录。
恢复备份文件、移除安装器新增文件；安装后修改过的文件另行保留。
卸载完成前保留 `D18_Backups` 与 `.dlssnr-d18-install.json`。

**Runtime 检查：**`VERIFIED` 表示识别到参考文件；
`UNVERIFIED_COMPATIBLE` 表示补丁位置通过检查；
`ALREADY_PATCHED` 表示所需改动已存在；`CONFLICT` 会停止打补丁。
显卡/驱动兼容性与补丁兼容性是两项检查。D18 保留布局兼容的其他修改。

报 `nvngx_dlssnr.dll was not found` 时检查文件名与位置；
报 `the model would not initialise` 时提供显卡、驱动、Runtime 哈希，以及日志中
`DLSS-NR create failed: init ... create ...` 一行。
[手动安装与恢复细节](community/d18-installer/README_CN.md)。

## Findings：研究发现

以下结论限定于已检查的 310.8 Runtime 与实测 D18 路径，不推广到全部驱动或实现。

- **权重：**已检查的预设注册表只有一个 `WEIGHTS_HT` 条目；其他界面预设回退到 preset 1。
  没有通过预设选择发现独立的性能/画质网络。
- **开销：**一次受控赛博朋克测试中，NR 增加约 7.50 ms GPU busy，其中 Feature 18 约占 7.48 ms。
  因此优化转向实际内部网络尺寸；不能仅凭 ratio 推算端到端加速比。
- **分支与尺寸：**`Style`/`Intensity` 可能改变原生后处理分支。
  有效尺寸、历史/暂存寻址与 dispatch 必须一致；外部全分辨率资源可与较小内部网络共存。
- **重建：**POINT 放大可能暴露重复格点。频带分离可保留 SR 细节，
  但单独换插值或更锐的输入核，未能消除低比例画质差距。
- **画质：**50% 测试中，艾尔登法环植被有运动柔化，鬼武者存在人脸/材质细节与色彩、色调差异。
  引导/gain-first 重建及输入核实验体现了取舍，因此保持可选。频带能量更高不等于恢复真实纹理或正确色彩。
- **证据边界：**四流捕获可区分合成与 Runtime 可见模型边界，不能拆开隐藏推理与 Runtime 重采样。
  能量比接近 1、API 返回成功或未观测到队列，都不足以证明视觉正确或 GPU 执行完成。
