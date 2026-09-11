# D18 图形安装器与 DX11 兼容性更新

## 新增中英文 GUI 安装器

双击 `D18-Setup.cmd`，按向导完成安装；右上角可随时切换中文/英文。原命令行安装和卸载入口继续保留。

- **选择游戏并定位目录**：支持浏览游戏 EXE、从已发现的游戏列表或正在运行的游戏进程选择。可识别部分 UE 启动程序并定位实际游戏 EXE；其他情况仍可手动选择。
- **选择图形 API 与注入文件名**：在向导中选择 DX11、DX12 或 Vulkan，再选择 dxgi.dll、winmm.dll 等受支持的代理名。
- **选择 DLSS NR 文件**：指定自己的 NR Runtime，安装前完成现有布局校验，并展示目标目录与安装内容供核对。
- **按需准备 SR / FG**：默认保留已有文件，也可选择版本下载或使用本地文件。插件 FG 下载会同时准备配套 Streamline 文件；选择安装 SR 时同步对齐库路径。安装文件不会自动启用 FG。
- **准备 REFramework 与依赖**：识别已知 RE 引擎游戏，也支持手动指定；可使用已有 REF、官方下载或本地文件。缺少 VC++ x64 运行库时提供微软官方安装入口，其他系统图形依赖缺项会提示。
- **明确显示安装结果**：后台预检与安装、日志入口、绿色“安装完成”页面及“完成”按钮；REF 待准备、卸载完成分别显示对应结果。
- **沿用原安装事务**：升级保留配置并备份，安装失败回滚，卸载恢复受管理的原文件。GUI 继续使用共用后端，不增加 SR/FG 版本白名单。

## DX11 启动兼容性与状态显示

改善部分 DX11 游戏启动兼容性，修复强制提升功能等级造成的初始化错误。两类设备创建入口保留游戏请求的功能等级和默认列表，按实际等级保存、恢复 UAV 状态。

DX11 兼容性提示：较老 DX11 游戏仍可能存在兼容性差异。本次修复不代表所有 DX11 游戏均已支持，也不要求游戏必须使用功能等级 11.1。建议优先在已有受支持的超分输入接入、并有实测记录的游戏中使用；SR、FG、NR 是否可用取决于各自的游戏输入、运行库及设备能力。能启动或打开菜单不代表三项功能已接入，不能只凭 DX11.0 / DX11.1 判断。

DX11 帧生成已知问题：部分游戏开启 FG / 多帧生成后，可能出现拖影、果冻感（画面扭曲）或局部画面错位。此类画质问题属于后续优化项，本次启动兼容性修复尚未解决；不代表所有 DX11 游戏都会出现。遇到时建议先降低帧生成倍率，仍有问题则关闭 FG。

运行面板分开显示呈现 API、SR 输入 API 和 SR/FG/NR 状态。尚未观察到 SR 输入时隐藏不适用管线的计数，启用或应用 SR 设置不代表自动接入游戏输入。中英文同步更新。

包内不附带 NVIDIA SR/FG/NR Runtime。下载列表中的所有 SR/FG 组合尚未逐一实机验证。

DLSS NR 校验更新：仅对 D18 要修改的位置检查冲突，其他位置的代码变化不再作为拦截理由；已正确应用 D18 补丁的文件也可通过。仍会拒绝无效或错误架构的文件。通过校验不保证所有社区修改版兼容，推荐使用原版 DLSS NR Runtime 或 RenoDX Discord 社区提供的兼容版本；来源名称本身不代表已验证。

启动 `D18-Setup.cmd` 使用图形安装器。

---

# D18 graphical installer and DX11 compatibility update

## New bilingual GUI installer

Run `D18-Setup.cmd` and follow the installation wizard. Switch between English and Chinese at the upper right. The existing command-line install and uninstall entry points remain available.

- **Select a game and locate its folder**: browse for an EXE, choose a discovered game or select a running game process. Supported UE bootstrap executables can redirect to the actual game EXE; manual selection remains available for other cases.
- **Choose the graphics API and proxy filename**: select DX11, DX12 or Vulkan, then choose a supported proxy name such as dxgi.dll or winmm.dll.
- **Select the DLSS NR file**: provide your NR runtime. The installer performs the existing layout checks and shows the target folder and installation summary before proceeding.
- **Prepare SR / FG as needed**: keep existing files by default, select a version to download or use local files. Plugin FG downloads also prepare the Streamline files; explicit SR installation aligns the library path. Installing files does not automatically enable FG.
- **Prepare REFramework and dependencies**: known RE Engine games are detected, with a manual option available. Use existing REF files, official downloads or a local file. Missing VC++ x64 offers the official Microsoft installer; missing system graphics dependencies are reported.
- **See a clear result**: background checks and installation, access to logs, a green Installation complete page and a Finish button. Pending REF preparation and completed uninstallation have their own result messages.
- **Retain the existing installation transaction**: upgrades preserve settings and create backups, failed installations roll back, and uninstall restores managed original files. The GUI uses the shared backend without adding an SR/FG version allowlist.

## DX11 startup compatibility and status display

Improves startup compatibility in some DX11 games by fixing initialization errors caused by forced feature-level elevation. Both device creation entry points preserve requested feature levels and default-list semantics; UAV state handling follows the actual feature level.

DX11 compatibility note: older DX11 games may still have compatibility differences. This fix does not establish support for every DX11 game or require games to use feature level 11.1. Prefer games with a supported upscaling input integration and documented testing. SR, FG and NR availability depends on their respective game inputs, runtimes and device capabilities. Successful startup or an accessible menu does not establish feature integration; DX11.0 / DX11.1 alone is not a compatibility test.

Known DX11 frame-generation issue: enabling FG / Multi Frame Generation in some games may cause ghosting, jelly-like distortion or localized image misalignment. These image-quality issues remain a future optimization item and are not resolved by this startup compatibility fix; they do not affect every DX11 game. If encountered, try a lower frame-generation multiplier, or disable FG if the issue persists.

The dashboard separates presentation API, SR input API and independent SR/FG/NR status. Pipeline counters are hidden when SR input has not been observed. Enabling or applying SR settings does not create game input integration. English and Chinese messages are updated together.

NVIDIA SR/FG/NR runtimes are not bundled. Not every SR/FG combination in the download list has been tested in-game.

DLSS NR validation update: conflict checks are limited to locations D18 modifies. Code changes elsewhere no longer cause rejection, and correctly D18-patched files are also accepted. Invalid files or the wrong architecture are still rejected. Passing validation does not guarantee compatibility with every community modification. We recommend the original DLSS NR runtime or a compatible version provided by the RenoDX Discord community; the source name alone does not establish verification.

Run `D18-Setup.cmd` to open the graphical installer.
