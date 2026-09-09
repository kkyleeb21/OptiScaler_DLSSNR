# DLSSNR D18 社区一键安装器

这是 D18 OptiScaler DLSS Neural Rendering 实验版的可审计安装器。

当前安装包版本为 `0.1.4a`，统一压缩包名称为 `DLSSNR_D18_0.1.4a.zip`。

D18 保持游戏 Color 与最终 Output 为完整输出分辨率，只将基于 NVIDIA 310.8 的网络内部工作网格缩小。3840×2160、ratio 0.5 时，网络精确为 1920×1080。Custom Mitchell 负责模型 Color 输入的抗混叠预滤波，最终 compose 可保留原始画面的高频细节。

## 不包含 NVIDIA Runtime

仓库和 Release 不包含、下载或重新分发 NVIDIA `nvngx_dlssnr.dll`。用户需自行提供 310.8 Runtime。已验证的参考输入为：

```text
SHA-256 E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
大小    165840496 字节
```

安装器在本机副本上验证并应用完整 D18 补丁，记录输入和输出哈希，原文件保持不变。每个修改区域必须是已知的原始字节或完整 D18 替换字节；未知布局会停止安装。

**DX11 另有完整 Runtime 哈希要求。** 当前 addon 接受本安装器由上述参考输入生成的已验证版本，输出 SHA-256 为 `CCAC112995922D8BD2C5F2D0DCB7A6756B7806D3D868692ACB9AF64D4AEF7414`。社区兼容 Runtime 即使通过补丁区域检查，也不一定能用于 DX11；安装器会在替换现有安装前检查并说明。不要把“补丁字段兼容”理解为任意 GPU 或后端运行兼容。

DX12/Vulkan 安装采用共同补丁区域检查；不覆盖 D18 区域的社区修改可保留，但这不等于该 Runtime 已通过游戏实测。已经完整打过 D18 补丁的输入可重复安装。

`runtime_patch_source` 提供可读补丁规范、RVA 说明与反汇编；发布构建验证其生成结果与 `runtime_patch.json` 一致。本机生成的修改版 Runtime 不再保有原 NVIDIA Authenticode 有效签名。

**燕云十六声：** 请确认实际启动的 `yysls.exe` 所在目录，可能是 `Engine\Binaries\Win64r` 或 `Engine\Binaries\Win64rh`，选择正确目录安装。

## 安装

### Runtime 检查提示

以下分类描述补丁区域检查；DX11 还必须通过上文完整输出哈希检查，才会继续安装。

| 提示代码 | 含义与处理 |
| --- | --- |
| `VERIFIED` | 已识别经过验证的参考 Runtime，继续安装。仍检查全部补丁字段。 |
| `UNVERIFIED_COMPATIBLE` | 补丁字段兼容，但尚未验证运行。部分字段已打补丁时也可能属于此类。异常时提供日志与 Runtime 哈希，并核对显卡、驱动支持范围。 |
| `CONFLICT` | 关键字段冲突，停止补丁且不修改原文件。需另选同时适配显卡与 D18 布局的 Runtime；提示包含首个冲突偏移与输入 SHA-256。 |
| `ALREADY_PATCHED` | 全部目标字段已具备本版本 D18 所需的补丁，无需重复修改，继续安装其他组件。这不证明整个文件来自本项目或已验证。 |

输入、输出指纹和通过检查的分类会记录在安装状态文件中。没有针对未知文件的强制覆盖选项。
`nvngx_dlssnr.dll` 是用户准备的 Runtime；`nvngx.dll_dlssnr.dll` 是随 D18 提供的转发器，二者不能混用。

RE 引擎原生 SR/RR 适配需要配合 REFramework。

1. 完全退出游戏。
2. 解压完整 GitHub Release ZIP。
3. 直接双击 `Install-D18.bat`。
4. 选择游戏执行文件目录、代理名称和实际启动 API（DX12 / DX11 / Vulkan）；已有安装保留所选代理名，API 可按本次启动方式调整。
5. 安装器会识别该游戏目录中已有的 `nvngx_dlssnr.dll` 或原生后端的 `D24Runtime.dll`，并重新验证兼容性。如果没有找到，才会提示选择自己准备的 310.8 Runtime。
6. 可选：如果不想安装时手动选择 Runtime，可提前在 `Install-D18.bat` 旁创建 `runtime_input` 文件夹，并把 `nvngx_dlssnr.dll` 放进去。
7. 启用游戏的 DLSS SR 路径，打开 D18，确认 NR 正在运行。先以 100% 为画质参考，再比较较低网络比例。

通用包默认 50% 网络比例、自定义预滤波与 Sharpness Override `0.85`。
这些是随包设置，不是通用画质推荐。低比例可能改变细节、色彩与运动稳定性，
锐化不代表恢复了全分辨率 NR 的画质。[功能说明](COMMON_FEATURES.md)。

双击 `Uninstall-D18.bat` 可恢复安装前的所有被覆盖文件。安装后又被用户修改的文件会另行保存，不会静默丢弃。

若游戏目录中已经存在受管理的 D18 安装，再次运行 `Install-D18.bat` 即可。安装器会询问
是否安全覆盖，保留上一版的时间戳备份并安装当前版本，无需先单独卸载。

覆盖旧版前，安装器会先生成并验证新的修改版 Runtime、核验全部 payload 来源与目标路径，
并检查可用磁盘空间。任何预检失败都不会改动当前可用的安装。

## 手动复制 payload（高级用户）

`payload` 不能原样拖入游戏目录。普通用户建议使用安装器，它会准备 Runtime、映射文件名、保留配置并记录卸载清单。

1. 退出游戏，备份所有将被覆盖的同名文件。
2. 将 `payload/OptiScaler.dll` 复制到游戏执行文件目录，改为所选代理名称；通常为 `dxgi.dll`，终末地使用已验证的 `d3d12.dll`。
3. **首次安装**将 `payload/OptiScaler.ini.d18` 复制为 `OptiScaler.ini`；升级保留原配置。复制 `payload/nvngx.dll_dlssnr.dll`、`payload/OptiScaler` 和 `payload/Licenses`。
4. 根据实际启动 API 补齐以下文件。表中的 Runtime 均由用户自行提供并完成完整 D18 补丁；不能用原始 Runtime 或仅做了显卡兼容修改的文件代替。

| 游戏启动 API | Runtime 在游戏目录中的名称 | 额外文件 |
| --- | --- | --- |
| DX12 | `nvngx_dlssnr.dll` | 无原生 addon |
| DX11 | `D24Runtime.dll` | `payload/D24Native.dll` |
| Vulkan | `D24Runtime.dll` | `payload/D24VulkanNR.enabled` |

DX11 Runtime 必须满足上文的完整输出哈希要求。原生 DX11/Vulkan 在 `[Upscalers]` 分别设置 `Dx11Upscaler=dlss` 或 `VulkanUpscaler=dlss`，在 `[DLSS]` 设置 `Enabled=true`。`nvngx.dll_dlssnr.dll` 是转发器，与 NVIDIA Runtime 不可混用。保留游戏自带的 SR/RR/FG 和 Streamline 文件，不用其他游戏的文件覆盖它们。

## 安全边界

- Internal Scaling 支持 DX11、DX12 与 Vulkan。原生 DX11/Vulkan 配套 runtime 使用已验证的 310.8 完整补丁版本。
- 社区 Runtime 只有在 D18 所涉及的全部字节范围保持兼容时才会被接受；这不代表任意来源 DLL 都是安全或受支持的。
- 不要在竞技或反作弊保护的在线游戏中使用注入 Mod，存在启动失败或账号处罚风险。
- 安装器会备份现有 ReShade/Mod Loader 代理，但替换代理仍可能破坏原有链路，请谨慎选择文件名。
- 安装器只处理明确清单中的文件，卸载后仍保留时间戳备份。
- Elden Ring 的 ERSS 文件不在处理清单内，会完整保留。

## 开源边界

安装器与 OptiScaler 改动遵循仓库 GPL-3.0。Release 携带第三方许可说明；NVIDIA Runtime 不属于本项目，仍受 NVIDIA 自身条款约束。
## 0.1.4a 新功能

首次安装默认 Insert；可在游戏内修改，也可用 `-UiToggleKey F10` 指定。
升级保留其他配置；选择原生 DX11/Vulkan 时，会提示并对齐对应 Upscaler=dlss 和 [DLSS] Enabled=true。已有配置时 `-UiToggleKey` 不覆盖原值。
游戏内可在 **Hotkeys** 修改 UI／NR 键，再点 **Save Settings**。

原版安装器不强制采用鬼武者的 `d3d12.dll`／REFramework 配置。
本包已包含新诊断与可选重建实验，实验选项默认关闭。
[简短更新说明](RELEASE_NOTES_0.1.4a.md) · [功能说明](COMMON_FEATURES.md)。

## REFramework 安装

RE 游戏安装会自动选择有适配记录的官方 nightly；无记录时选择最新 nightly（未经该游戏验证）或自行安装。仅从 https://github.com/praydog/REFramework-nightly 下载非 VR 的 dinput8.dll，校验官方 SHA256。网络失败不会改动当前安装，可选择手动安装。现有 REF 保留，未知 dinput8.dll 需确认身份，不会静默替换其他加载器。

REF 菜单键设为 PgDn，其余设置和插件保留。D18 菜单默认 Insert。两种环境均读写游戏根目录 OptiScaler.ini；不要再修改 _storage_ 中的旧 INI。卸载按清单恢复安装前文件，保留其他插件。

高级参数：`-REFramework Auto|Recommended|Latest|Existing|Manual`，未知游戏可加 `-REEngine` 选择 RE 安装布局；这不保证 D18 已支持该游戏的渲染路径。普通安装不联网下载 REF。

## 0.1.4a：DX11 / Vulkan 安装与游戏提示

安装时选择实际运行的图形 API，再选择代理 DLL 名称。升级保留此前选择与其他配置，同时对齐所选原生 API 必需的两项 DLSS 设置。
命令行可用 `-NativeApi DX11`、`-NativeApi Vulkan` 或 `-NativeApi None`（DX12），以及 `-ProxyName dxgi.dll|winmm.dll|version.dll|dbghelp.dll|d3d12.dll`。
切换游戏启动 API 后，重新运行安装器选择相同 API；命令行可明确指定 NativeApi。
原生 DX11/Vulkan 安装把本机生成的 runtime 命名为 `D24Runtime.dll`，并部署配套 addon/激活文件；建议使用安装器而非手动复制。
DX11 addon 目前校验已验证 runtime 的完整哈希，安装器会提前检查；原有 DX12 社区 runtime 的字节范围兼容策略继续保留。

- 终末地：代理使用 `d3d12.dll`，安装器自动推荐；API 选择应与启动器一致。
- 博德之门 3：启动器可能提示“数据不匹配”，本次测试中不影响游戏和 D18 使用。
- 插件 2× FG 已测试正常，更高倍率多帧生成仍可能存在潜在 bug，建议优先 2×。
- 新装默认 Insert 打开 D18；原生 DX11/Vulkan 的 PgUp 切换 NR。热键可在界面中保存修改。

[0.1.4a 更新说明](RELEASE_NOTES_0.1.4a.md)。

## 中英文界面

D18 面板顶部的 **Language / 语言** 可切换 **English / 简体中文**，立即生效，无需重启游戏。点击 **Save Settings / 保存设置** 后为当前游戏保留；配置项为 `[Menu] D18Language=0`（英文）或 `1`（简体中文）。两种语言使用同一套控件与启用条件。

中文字体从 Windows 系统字体加载，不随包分发；找不到可用字体时保留英文并显示原因。技术名、文件名和原始诊断日志保留原文，便于排查。NR → 人物与风格中的细节闪烁修正开关可热切换；仁王 2 默认开启，其他游戏默认关闭。

## 升级恢复与自动安装

升级会先校验并备份上一版受管理部署；卸载或复制中途失败时，尝试恢复并校验上一版文件和设置。恢复快照保留在游戏目录 D18_Backups/upgrade-recovery-*；磁盘仍不可写等情况会报告具体恢复路径。
`-Yes` 找不到 Runtime 时会明确报错并提示 `-RuntimePath`，不会继续询问文件。切换 API 会重新检查对应文件与所需配置。
