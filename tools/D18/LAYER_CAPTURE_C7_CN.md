# C7 两遍分阶段像素诊断

显式点击现有「采样 8 帧」按钮，DX12 标准两遍模式记录同帧 SR 基底、第一遍模型输出、第二遍模型输出、最终合成。没有增加菜单层级，不改遍数、history/Reset、NR 参数或最终合成算法。三/四遍保持仅元数据诊断，单遍沿用既有抓取。

五个 128×128 区域位于中心及四个象限中心，最多八帧。支持全尺寸 RGBA16F/RGBA32F：分别最多 20/40 MiB 读回，硬上限 128 MiB；一次启动最多三个请求。十秒未录完停止记录，不以超时当作 GPU 完成。实际 post-Execute 信号与 fence 完成前禁止映射/释放；未观测提交则保留有界资源，不继续接受新请求。

每帧各阶段独立偏移，第一遍在后续调用覆盖输出前复制；复制后恢复原资源状态。成功前缀、缺失阶段、来源/模式改变或 NR 记录间断都保留明确标记。写盘失败不生成 manifest.json；不因暗部自动重试。额外复制和写盘会影响时序；抓取期间没有闪烁不能单独证明问题已修复。

文件位于游戏代理旁 `D18LayerCaptures/<PID>-<tick>-<request>/`。manifest 包含阶段掩码、来源、attempt/frame、共享模式、两遍实际参数、Reset、guide 尺寸/格式/范围、曝光元数据和合成常量。SR/最终为游戏颜色域，模型输出为编码代理域；未抓 guide、模型内部 history 或曝光纹理像素，不支持精确跨运行重放。

总体工具入口已接通，仅明确选择的目录才复制像素：

```powershell
& E:\DLSSNR\tools\D18\collect-diagnostics.ps1 `
  -GameDirectory '<游戏代理目录>' `
  -BuildManifest '<本次候选的 build-manifest.json>' `
  -LayerCaptureDirectory '<游戏代理目录>\D18LayerCaptures\<已完成目录>'
```

`archive-layer-capture.py` 做有界复制并前后校验哈希；`analyze-layer-capture.py` 检查清单、像素长度/有限性和完整性，生成 `layer-summary.json/md`。普通日志采集保持原行为；不会启动 GPU 宿主、开启 Summary 或自动抓取。C2–C4 事件解析器与其他任务流的最终整体合并仍需独立核对，C6 runner 参数化也未因此完成。

验证包含 WARP 真复制/受阻 fence/逐像素读回（RGBA16F、RGBA32F）、同一资源四次覆盖、八帧五区域、部分帧、来源切换、请求上限、写盘失败，以及分析器拒绝伪完整/截断/错误连续性。七个生产 multipass 编排案例使用 identity/failure 模型，验证关闭抓取时最终像素与完整 SR 回退；不是实际 NR 模型或游戏画质验收。

下次仅异环两遍短测：保留同一场景和参数，独立历史稳定后抓一次，再切共享历史等待几秒、闪烁时抓一次。保持 Summary；无需四遍或重做已验收项目。记录 FG 状态、截图闪烁位置；若抓取时闪烁变化也记录。写盘完成后可退出游戏读取证据。
