# 原生 NR 四阶段与 guide 诊断

日常保持采样关闭。需要复现时沿用 D18 的 **Capture four-stage regions**，DX12 的 Capture 8 frames 按钮不是 DX11 入口。

## 用户一次采集

1. 保持引发问题的参数和视角，让采样区域覆盖皮肤/头发；勾选 Capture four-stage regions 后关闭 D18 面板，尽量不转镜头。
2. 保持约 15 秒，然后取消勾选。无需反复开关 NR 或切换 ratio。每次勾选只运行一轮，达到上限后自动停止写入；重新勾选才能开始下一轮。

DX11 每轮取四组、每组 8 个连续模型调用，目标开始时刻为 0/1.5/3/4.5 秒。最多 32 帧，15 秒截止；游戏低帧率、暂停或提前关闭可能使帧数不足。**这不是 5 秒逐帧录像。** 游戏目录的 D24CaptureStatus.txt 显示真实完成数/耗时，日志记录不完整帧和失败；颜色完成不等于 guide 完成。

采样帧会同步读回，可能使帧时间变长，不用这段测 FPS。单张区域最大 512×512；guide 按颜色区域映射到对应有效分辨率，原始深度按 API 要求完整读回再裁剪。单次深度 staging 上限 128 MiB，所有文件理论上限约 705 MiB/轮；通常 4K 输出、1080p guide、R11 颜色的磁盘占用更低。旧轮次保留独立文件名，不自动删除历史数据。

## 统一入口

在游戏退出后执行（运行中也可快照，但末帧可能不完整）：

```powershell
python E:\DLSSNR\tools\D24\collect-native-capture.py --game 'D:\SteamLibrary\steamapps\common\Nioh2' --out 'E:\DLSSNR\evidence\D24\Nioh2-next-capture'
```

输出目录必须是新的。工具只读取日志列出的采样文件，不混入其他历史帧；保存日志、INI、SHA256 清单，再依次调用现有公共摘要、颜色分析和时间序列工具。

- `runtime-summary.json`：入口/资源/错误、采样进度、帧耗时、调用序号和模型 epoch。
- `analysis.json`：SR/input/model/composed，曝光原始值及实际使用值，Depth/MV 原始/自有/模型执行后三组比较；格式缺失单独标明，不能当成功。
- `sequence.json`：同参数、同 epoch、连续帧的时间变化；发生 reset 的边界不做稳定性结论；额外列 MV 中位数、模型 scale 和 jitter 差分，不自动猜符号或单位。
- `packets/coverage.json`：实际颜色帧数、开始/完成/提前结束状态。
- `READOUT.md`：采集覆盖摘要，不能代替画质判断。

已知脸部区域可加 `--region face:192,195,165,195`；坐标相对于采样图，而非全屏。不同视角应先检查预览，不能套用旧 ROI。局部配准只由 SR 拟合，同一变换用于全部阶段，避免分别对齐模型输出而抹掉真实变化。它仍不能排除动画、遮挡和照明变化。

## 数据含义

DX11 NR 在“Characters and style”增加 `Reduce detail flicker (NR jitter correction)`。皮肤、头发等细节闪烁时可尝试；若拖影或不稳定加重则关闭。下一次NR帧生效，点击Save Settings保存到该游戏的 `[DlssNr] JitterCorrection`：true开启、false关闭、auto/缺省跟随游戏默认（仁王2开启，其他游戏关闭）。条件不符显示原因，不强制补偿。旧 `D24NrJitterCorrection.disabled` 标记仍优先禁用，移除后需重启。

共享 `runtime-summary.json` 的 `jitter_states` 保留选择值、实际请求、启用状态、原因码、补偿量与历史reset；Summary默认关闭，事件最多1800条，状态变化时优先记录。原因码按 `NativeControlAbi.h` 的 `JitterReason` 定义。主动捕获时 `motion_nr` / `motion_nr_after` 及 `jitter_checks` 区分预期补偿差值和模型写回异常，原始及合成MV继续单独核验。

`flags` 是 D24Process 收到的参数；旧核心只传部分位，不能用缺少某位认定游戏没启用。`game_params[DLSS.Feature.Create.Flags]` 记录本帧查询结果；查询失败表示未观测，另查 OptiScaler.log 的 SR 初始化记录。模型参数读回只能证明我们的参数容器，不证明闭源 runtime 内部每项都实际使用。

原始 guide 的 typeless 解码与当前适配器 SRV 类型一致；这是可验证的数值比较，不等于认证游戏原始向量语义。Depth/MV 写回检查能定位模型执行期间的变化；CPU 同帧调用序号不等于 Present frame ID，不据此认证 FG 的帧对齐。

共享 decoder 支持 FP16/FP32/R11 颜色、R32/R32G8X24/D24/D16 深度和 FP16/FP32 双通道 MV。新格式必须明确增加解码与数值测试；未知格式留缺口，禁止按 FP16 强行解析。
