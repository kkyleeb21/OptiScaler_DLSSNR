# OptiScaler DLSSNR D18

> 0.1.6 是相对公开版 0.1.4a 的累计更新，包含 0.1.5 私测整合及后续全部已验收修复。DX12 Classic 仍为默认，Hybrid 可选；Vulkan FG 仍实验且默认关闭。

D18 0.1.6：支持 DX11 / DX12 / Vulkan 的 DLSS Neural Rendering，提供中英文界面、NR 调节与 OptiScaler 帧生成 Override。

[English](README.md) · [下载 0.1.6](https://github.com/kkyleeb21/OptiScaler_DLSSNR/releases/tag/dlssnr-d18-v0.1.6) · [完整安装说明](community/d18-installer/README_CN.md) · [更新说明](community/d18-installer/RELEASE_NOTES_0.1.6.md)

**燕云十六声：** 请确认实际启动的 `yysls.exe` 所在目录，可能是 `Engine\Binaries\Win64r` 或 `Engine\Binaries\Win64rh`，选择正确目录安装。

## 安装

1. 退出游戏，下载并完整解压 0.1.6 ZIP，运行 Install-D18.bat。
2. 选择游戏主程序目录、代理 DLL 名称和实际启动 API。终末地使用安装器推荐的 d3d12.dll。
3. 自备兼容的 NVIDIA 310.8 NR Runtime：可放到安装器旁 runtime_input/nvngx_dlssnr.dll，或按提示选择。已安装的 nvngx_dlssnr.dll / D24Runtime.dll 会自动识别并验证。DX11 要求指定的完整 Runtime 哈希，见安装说明。
4. 在游戏中启用 DLSS SR，按 Insert 打开 D18，确认 NR 状态。原生 DX11/Vulkan 新装默认 PgUp 切换 NR；升级保留已有热键。

升级保留代理名称和其他配置；选择原生 DX11/Vulkan 时，安装器提示并对齐该 API 的 Upscaler=dlss 与 [DLSS] Enabled=true。升级前备份上一版，失败时尝试恢复并校验。不要直接将 payload 原样拖入游戏目录。

## 功能

- NR 开关、状态、网络比例置顶；其余选项按清晰度、颜色、人物与风格分类，显示用途与启用条件。
- 网络内部缩放保留 Color/Output 输出尺寸；通用包默认 50% 比例和 0.85 锐化覆盖，可自行调整。
- 增加色彩保留、可逆高光编码及仅传递模型颜色变化选项；各后端按支持情况启用。
- DX11 细节闪烁修正可热切换并保存；仁王 2 默认开启，其他游戏默认关闭。
- 顶部 Language / 语言切换 English / 简体中文，保存设置后按游戏保留。字体使用 Windows 系统字体。
- OptiScaler FG 输入、输出和启用状态统一设置后按提示重启一次；界面区分请求倍率与实际观测状态。
- 提供跨 API 诊断与分阶段采样、NR 分配失败降级提示。

[功能说明](community/d18-installer/COMMON_FEATURES.md)

## SR / FG 文件与游戏提示

NVIDIA NR/SR/FG 运行文件不随本包提供。SR 如需补充，将 nvngx_dlss.dll 放在游戏主程序旁。OptiScaler DLSS FG（含 DX11 路线）需在主程序旁 streamline 目录放入同一配套版本的 sl.interposer.dll、sl.common.dll、sl.dlss_g.dll、sl.reflex.dll、sl.pcl.dll、nvngx_dlssg.dll。保留游戏原有的运行文件；补充文件后重启游戏。

插件 2× FG 已在已测游戏中验证；DX11 插件 FG 兼容性因游戏而异，更高倍率可能存在拖影或局部不对齐，建议优先 2×。博德之门 3 启动器可能提示“数据不匹配”，本次测试中不影响使用。

RE 引擎适配使用 REFramework，见完整安装说明。不要在竞技或反作弊保护的在线游戏中使用注入 Mod。

## 源码与历史基线

[构建说明](BUILD_D18.md) · [0.1.3a 历史基线](https://github.com/kkyleeb21/OptiScaler_DLSSNR/releases/tag/dlssnr-d18-v0.1.3)

项目基于 OptiScaler，源码遵循 GPL-3.0；第三方组件遵循各自许可。NVIDIA Runtime 由用户自行提供。
