# DLSSNR D18 社区一键安装器

这是 D18 OptiScaler DLSS Neural Rendering 实验版的可审计安装器。

当前安装包版本为 `0.1.0`，统一压缩包名称为 `DLSSNR_D18_0.1.0.zip`。

D18 保持游戏 Color 与最终 Output 为完整输出分辨率，只将基于 NVIDIA 310.8 的网络内部工作网格缩小。3840×2160、ratio 0.5 时，网络精确为 1920×1080。Custom Mitchell 负责模型 Color 输入的抗混叠预滤波，最终 compose 可保留原始画面的高频细节。

## 不包含 NVIDIA Runtime

仓库和 Release 均不会包含、下载或重新分发 `nvngx_dlssnr.dll`。用户需要自行提供基于 310.8 的 Runtime。下列官方文件是参考构建，但不再是唯一允许的完整文件哈希：

```text
SHA-256 E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
大小    165840496 字节
```

社区中面向 20/30/40 系 GPU 的兼容版可能修改 Runtime 的其他位置。因此，安装器会记录完整输入 SHA-256，但不再用它建立白名单。安装器只验证 D18 实际需要修改的每一个字节范围；这些位置必须处于已知的 310.8 原始布局，或已经包含完整 D18 替换。

这样既能保留与 D18 无关的社区兼容补丁，也会拒绝 D18 关键代码路径布局未知的 DLL。已经完成 D18 补丁的输入可重复安装；输入和输出哈希都会写入安装状态，便于追溯。

`runtime_patch_source` 提供可读的十六进制补丁规范、RVA 设计说明和带注释反汇编。发布构建会
验证这些源码能够精确生成安装器实际使用的 `runtime_patch.json`。

源文件不会被原地修改。本机生成的修改版 Runtime 会失去原 NVIDIA Authenticode 有效签名。

## 安装

1. 完全退出游戏。
2. 解压完整 GitHub Release ZIP。
3. 直接双击 `Install-D18.bat`。
4. 输入游戏可执行文件所在目录，并选择加载代理名；默认是 `dxgi.dll`。
5. 安装器会自动使用该游戏目录中已有且兼容的 `nvngx_dlssnr.dll`。如果没有找到，才会提示选择自己准备的 310.8 Runtime。
6. 可选：如果不想安装时手动选择 Runtime，可提前在 `Install-D18.bat` 旁创建 `runtime_input` 文件夹，并把 `nvngx_dlssnr.dll` 放进去。
7. 进游戏后确认 Internal network scaling 为 `0.500`，Custom Mitchell model Color prefilter 已启用。

发布基线将 Sharpness Override 设为 `0.85`。三游戏测试中 `0.80-0.90` 的主观清晰度最接近完整分辨率，但锐化无法恢复低分辨率网络从未获得的色彩与语义细节。

双击 `Uninstall-D18.bat` 可恢复安装前的所有被覆盖文件。安装后又被用户修改的文件会另行保存，不会静默丢弃。

若游戏目录中已经存在受管理的 D18 安装，再次运行 `Install-D18.bat` 即可。安装器会询问
是否安全覆盖，保留上一版的时间戳备份并安装当前版本，无需先单独卸载。

覆盖旧版前，安装器会先生成并验证新的修改版 Runtime、核验全部 payload 来源与目标路径，
并检查可用磁盘空间。任何预检失败都不会改动当前可用的安装。

## 手动复制 payload（高级用户）

`payload` 文件夹有意设计成**不能原样直接拖入游戏目录**。Release 不能重新分发 NVIDIA
Runtime；代理与配置文件也采用中性暂存名，方便安装器按用户选择安全部署。

已经拥有“兼容 D18 且完成 D18 补丁”的 310.8 Runtime 的高级用户，可以按以下步骤手动安装：

1. 备份游戏可执行文件目录中所有可能同名的现有文件；
2. 将 `payload/OptiScaler.dll` 复制过去，并改成游戏所需的代理名，通常是 `dxgi.dll`；
3. 将 `payload/OptiScaler.ini.d18` 复制为 `OptiScaler.ini`；
4. 原样复制 `payload/nvngx.dll_dlssnr.dll`、`payload/OptiScaler` 和 `payload/Licenses`；
5. 自行放入已经完成 D18 补丁的 `nvngx_dlssnr.dll`；Release 不包含此文件。

第 5 步不能使用普通 Runtime，单纯针对旧 GPU 的兼容版也不一定包含 D18 修改。仍推荐普通
用户使用自动安装器：它会在本机验证并修改用户提供的 Runtime、创建可恢复备份，并记录可
精确卸载的文件清单。

## 安全边界

- Internal Scaling 目前仅用于 D3D12，并绑定经过字节范围守卫的 310.8 布局家族。
- 社区 Runtime 只有在 D18 所涉及的全部字节范围保持兼容时才会被接受；这不代表任意来源 DLL 都是安全或受支持的。
- 不要在竞技或反作弊保护的在线游戏中使用注入 Mod，存在启动失败或账号处罚风险。
- 安装器会备份现有 ReShade/Mod Loader 代理，但替换代理仍可能破坏原有链路，请谨慎选择文件名。
- 安装器只处理明确清单中的文件，卸载后仍保留时间戳备份。
- Elden Ring 的 ERSS 文件不在处理清单内，会完整保留。

## 开源边界

安装器与 OptiScaler 改动遵循仓库 GPL-3.0。Release 携带第三方许可说明；NVIDIA Runtime 不属于本项目，仍受 NVIDIA 自身条款约束。
