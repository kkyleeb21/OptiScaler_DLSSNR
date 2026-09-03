# OptiScaler DLSSNR D18

> 基于 OptiScaler DLSSNR 的实验性 Internal Network Scaling 研究分支。

[English README](README.md) · [社区安装器](community/d18-installer/README_CN.md)

D18 面向 NVIDIA DLSS Neural Rendering（Feature 18）。它在保持游戏 Color、最终 Output 与合成链路处于完整显示分辨率的同时，让 DLSSNR 网络在独立的二维低分辨率网格上运行，从而降低 Neural Rendering 的计算成本，并尽量避免原有 `WorkingScale` 物理缩放路径带来的模糊、色彩偏移和边缘锯齿。

以 3840×2160 输出、Network Ratio `0.5` 为例：

```text
游戏 Color / 最终 Output：3840×2160
DLSSNR Network Lattice：  1920×1080
Depth / Motion Vectors： 保持游戏原始 Guide 契约
```

D18 是社区实验构建，不是 NVIDIA、OptiScaler 或原 OptiScaler_DLSSNR 项目的官方版本。

## D18 的主要功能

### 1. Internal Network Scaling

- Color 和最终 Output 保持显示分辨率；
- Network Ratio 可在 `0.5–1.0` 间连续调节；
- 提供 50%、66.7%、75% 和 100% 快捷档位；
- 网络宽高独立计算，并按 Runtime 的 16×8 网格要求对齐；
- Ratio 或采样契约改变时安全重建 Feature 18；
- 日志记录实际 Output、Network、Guides、Ratio 和采样模式；
- 保留原有 `WorkingScale`，便于直接 A/B。

### 2. Custom Mitchell Color Prefilter

新增 `DlssNrMode_ColorPrefilter` Shader Pass，根据实际 Network Ratio 构造相位对齐的 Mitchell–Netravali Color surrogate：

- 避免 Runtime POINT 隔点采样造成的输入锯齿；
- 比硬件双线性/2×2 Box 保留更多中频细节；
- 使用局部范围约束降低 Mitchell ringing；
- 支持非整数 Network Ratio；
- 启用时自动覆盖 Linear Color Input，避免重复低通。

### 3. Frequency-aware Composition

低分辨率网络主要贡献低频亮度与 Neural Rendering 裁决，完整分辨率原图可以保留真实高频细节；Colour Strength 保持独立。另提供可选的运动/低频失配保护入口，用于逐游戏实验，基线默认关闭。

### 4. Runtime Sampling Controls

D18 提供独立的 POINT/LINEAR A/B 开关：

- Linear network output sampling；
- Linear model Color input。

Forwarder 会验证准确的指令字节，只修改已经加载到内存中的 Runtime。未知布局会被拒绝，不会盲目写入。

新增或扩展的入口：

```cpp
dlssnr_call_set_sampler_modes(int linearResolve, int linearColorInput);
dlssnr_call_create(..., float scalingRatio);
dlssnr_call_evaluate(..., float scalingRatio);
```

### 5. OptiScaler UI Integration

```text
Internal network scaling
Network ratio
50% / 66.7% / 75% / 100%
Detail strength
Colour strength
Preserve original high frequencies
Motion-adaptive low-frequency transfer
Linear network output sampling
Linear model Color input
Custom Mitchell model Color prefilter
```

全部入口均可在 OptiScaler Overlay 中直接调整，并保存到 `OptiScaler.ini`。

## 与原 OptiScaler DLSSNR 的区别

| 模块 | 原 OptiScaler DLSSNR | D18 |
|---|---|---|
| 低成本运行 | `WorkingScale` 物理缩放 | 新增完整资源契约的 Internal Network Scaling |
| Color / Output | 随 `WorkingScale` 缩小 | Internal 模式保持显示分辨率 |
| Network Ratio | 无独立有效数据流 | 0.5–1.0 可调二维网格 |
| Color 输入 | Runtime 默认采样 | 新增 Mitchell 相位对齐预滤波 |
| Network Output | Runtime 固定 POINT | 提供 POINT/LINEAR A/B |
| 高频细节 | 随模型结果放大 | 可从完整分辨率原图保留 |
| 运动保护 | 无 | 可选运动/失配自适应低频转移 |
| Runtime 接受条件 | 官方文件固定哈希 | 支持布局兼容的官方及社区 310.8 修改版 |
| 部署 | 手工复制 | 可审计安装、备份、回滚与卸载工具 |

## D18 Community Installer

安装器源码位于 [`community/d18-installer`](community/d18-installer)，不会包含、下载或重新分发 `nvngx_dlssnr.dll`。用户需要自行提供基于 310.8 的 Runtime，也可以使用面向 20/30/40 系 GPU 的社区兼容修改版。

安装器不再要求输入文件匹配唯一的完整 SHA-256。它会：

- 记录输入与输出文件的 SHA-256；
- 检查 D18 实际需要修改的每一个字节范围；
- 接受该范围处于“原始布局”或“已完成 D18 替换”状态；
- 保留其他位置的社区兼容修改；
- 若其他 Mod 改动了 D18 所依赖的代码路径，则安全拒绝自动补丁；
- 始终生成私有副本，不原地修改用户源文件。

社区安装包版本从 `0.1.0` 开始，统一命名为 `DLSSNR_D18_<版本号>.zip`。

主要入口：

```text
community/d18-installer/Install-D18.bat
community/d18-installer/Uninstall-D18.bat
community/d18-installer/Build-D18Release.ps1
community/d18-installer/runtime_patch.json
```

若所选游戏目录已经存在受管理的 D18 安装，再次运行 `Install-D18.bat` 会询问是否安全覆盖。
确认一次后，安装器会调用精确文件卸载逻辑、保留上一版的时间戳备份，再安装当前版本；更新
D18 前不再需要手动运行卸载脚本。

Release README 中提供了面向高级用户的手动安装步骤。原始 `payload` 并不能直接原样拖入：
代理与配置暂存文件需要重命名，用户另行提供的 Runtime 也必须已经包含 D18 修改。普通用户
仍建议使用自动安装器。

## 推荐基线

```ini
InternalScaling=true
InternalScalingRatio=0.5
CustomColorFilter=true
LinearResolve=false
LinearColorInput=false
PreserveHighFrequency=true
MotionAdaptive=false
TransferStrength=1.0
ColourStrength=1.0
```

OptiScaler Sharpness Override 可从 `0.80–0.90` 开始测试。锐化无法恢复低分辨率网络从未获得的色彩与语义细节。

## 兼容性与安全边界

- Internal Network Scaling 当前仅支持 D3D12；
- 0.1.2 基线中的原生 Vulkan 路径仍然保留，但使用标准 `WorkingScale`；
- Runtime 必须基于 310.8，且 D18 所需的受保护字节范围保持布局兼容；
- NVIDIA Runtime 不属于本仓库，仍受 NVIDIA 自身条款约束；
- 不建议在竞技或反作弊保护的在线模式中使用注入式 Mod。
