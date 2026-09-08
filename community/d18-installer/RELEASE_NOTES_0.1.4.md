# D18 0.1.4

## 中文

- 增加 English / 简体中文界面切换、NR 选项用途短提示，语言可按游戏保存。
- 增加按游戏保存、可热切换的 DX11 NR 细节闪烁修正开关与生效条件提示。
- 增加 DX11 / Vulkan 的 DLSS Neural Rendering 支持。
- 增加 OptiScaler 插件帧生成 Override 控制，输入、输出和启用状态可统一配置后重启一次；区分请求倍率与实际观测状态。
- 重整 NR 调节界面，按清晰度、颜色、人物与风格分类，补充当前状态和选项启用条件提示。
- 增加色彩保留、可逆高光编码与模型颜色变化传递选项，完善网络比例和人物参数调节。
- 安装器增加代理 DLL 名称与图形 API 选择，升级保留所选代理名称和其他配置，并对齐所选原生 API 必需的 DLSS 设置。
- 完善跨 API 诊断、资源分配失败回退与运行状态提示。

- 完善旧配置迁移、已安装 Runtime 自动识别与升级失败恢复。

### SR / FG 运行文件

- **DLSS SR**：运行文件需自备。如有需要，将 `nvngx_dlss.dll` 放在游戏主程序旁；游戏已有的 DLSS 文件可保留。
- **OptiScaler 插件 DLSS FG（含 DX11 路线）**：在游戏主程序旁新建 `streamline` 文件夹，放入同一配套版本的 `sl.interposer.dll`、`sl.common.dll`、`sl.dlss_g.dll`、`sl.reflex.dll`、`sl.pcl.dll` 和 `nvngx_dlssg.dll`。这些 NVIDIA 运行文件不随 D18 包提供。
- 补齐文件后重启游戏，选择支持的帧生成路线并开启；若 UI 提示需要重启，保存设置后重启。游戏原生 FG 使用游戏自带文件，请保留根目录原有 DLL。

### 测试与游戏提示

- 插件 **2× 帧生成已在已测游戏中验证可用**。**DX11 插件 FG 兼容性因游戏而异，仍在测试中**；更高倍率多帧生成可能出现拖影、局部画面不对齐等问题，建议优先使用 2×。
- **明日方舟：终末地**：测试中需要将代理 DLL 命名为 **d3d12.dll**，安装器会推荐该名称。
- **博德之门 3**：安装后启动器可能弹出“数据不匹配”；本次测试中不影响游戏和 D18 使用。

## English

- Adds English / Simplified Chinese UI switching, per-game language settings and short explanations for NR controls.
- Adds a per-game DX11 NR detail-flicker correction toggle with live switching and availability messages.
- Adds DLSS Neural Rendering support for DX11 and Vulkan.
- Adds OptiScaler frame-generation override controls with a single-restart setup flow and separate requested/observed multiplier status.
- Reorganizes NR controls by clarity, colour, characters and style, with clearer status and availability hints.
- Adds colour preservation, reversible highlight encoding and model colour-change transfer options, with improved network-ratio and character controls.
- Adds proxy DLL and graphics API selection to the installer; upgrades retain the selected proxy name and other settings while aligning the DLSS settings required by the selected native API.
- Improves diagnostics across APIs, allocation-failure fallback and runtime status messages.

- Improves existing-configuration migration, installed-runtime discovery and recovery of the previous installation after a failed upgrade.

### SR / FG runtime files

- **DLSS SR**: supply the runtime yourself. If needed, place `nvngx_dlss.dll` beside the game executable; existing game-provided DLSS files can be retained.
- **OptiScaler plugin DLSS FG (including the DX11 route)**: create a `streamline` folder beside the game executable and add `sl.interposer.dll`, `sl.common.dll`, `sl.dlss_g.dll`, `sl.reflex.dll`, `sl.pcl.dll` and `nvngx_dlssg.dll` from one matching package. These NVIDIA runtime files are not included with D18.
- Restart after adding files, then select and enable a supported FG route. If the UI requests another restart, save settings and restart. Game-native FG uses the game's own files; preserve existing root DLLs.

### Testing and game notes

- Plugin **2× frame generation has worked in tested games**. **DX11 plugin FG compatibility varies by game and remains under testing**. Higher multipliers may exhibit ghosting or localized image misalignment; 2× is recommended.
- **Arknights: Endfield**: testing required the proxy name **d3d12.dll**. The installer recommends this name.
- **Baldur’s Gate 3**: the launcher may show a “Data mismatch” warning after installation. It did not affect the game or D18 in our testing.
