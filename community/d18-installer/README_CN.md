# DLSSNR D18 社区一键安装器

这是 D18 OptiScaler DLSS Neural Rendering 实验版的可审计安装器。

D18 保持游戏 Color 与最终 Output 为完整输出分辨率，只将 NVIDIA 310.8 网络的内部工作网格缩小。3840×2160、ratio 0.5 时，网络精确为 1920×1080。Custom Mitchell 负责模型 Color 输入的抗混叠预滤波，最终 compose 可保留原始画面的高频细节。

## 不包含 NVIDIA Runtime

仓库和 Release 均不会包含、下载或重新分发 `nvngx_dlssnr.dll`。用户必须自行提供完全匹配的官方 310.8 文件：

```text
SHA-256 E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
大小    165840496 字节
```

安装器会校验完整文件，在临时副本上应用 1007 字节的受保护差异，并验证结果：

```text
SHA-256 CCAC112995922D8BD2C5F2D0DCB7A6756B7806D3D868692ACB9AF64D4AEF7414
```

源文件不会被原地修改。本机生成的实验 Runtime 会失去原 NVIDIA Authenticode 有效签名。

## 安装

1. 完全退出游戏。
2. 解压完整 GitHub Release ZIP。
3. 在 `Install-D18.bat` 旁创建 `runtime_input` 文件夹，将官方 `nvngx_dlssnr.dll` 放入；也可以运行时手动指定路径。
4. 双击 `Install-D18.bat`。
5. 输入游戏可执行文件所在目录，并选择加载代理名；默认是 `dxgi.dll`。
6. 进游戏后确认 Internal network scaling 为 `0.500`，Custom Mitchell model Color prefilter 已启用。

发布基线将 Sharpness Override 设为 `0.85`。三游戏测试中 `0.80-0.90` 的主观清晰度最接近完整分辨率，但锐化无法恢复低分辨率网络从未获得的色彩与语义细节。

双击 `Uninstall-D18.bat` 可恢复安装前的所有被覆盖文件。安装后又被用户修改的文件会另行保存，不会静默丢弃。

## 安全边界

- Internal Scaling 目前仅用于 D3D12，并严格绑定已审计的 310.8 Runtime。
- 不要在竞技或反作弊保护的在线游戏中使用注入 Mod，存在启动失败或账号处罚风险。
- 安装器会备份现有 ReShade/Mod Loader 代理，但替换代理仍可能破坏原有链路，请谨慎选择文件名。
- 安装器只处理明确清单中的文件，卸载后仍保留时间戳备份。
- Elden Ring 的 ERSS 文件不在处理清单内，会完整保留。

## 开源边界

安装器与 OptiScaler 改动遵循仓库 GPL-3.0。Release 携带第三方许可说明；NVIDIA Runtime 不属于本项目，仍受 NVIDIA 自身条款约束。
