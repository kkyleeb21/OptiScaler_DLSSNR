# D18 渲染诊断工具集：高分辨率与多遍收口

2026-09-14。统一入口 `python tools/D18/render-toolkit.py` 列出能力；每个命令后 `-- --help` 查看该分析器参数。入口默认不执行任何 GPU 工作。

## 日常采集与离线分析

```powershell
& tools/D18/collect-render-diagnostics.ps1 -GameDirectory '<本次测试游戏目录>'
python tools/D18/render-toolkit.py summary -- '<D18Diagnostics.ring>' --json '<输出.json>' --markdown '<输出.md>'
```

Layer Capture 只有显式给出 `-LayerCaptureDirectory` 才归档。collector 只读取已存在的诊断数据，不改 Summary、游戏配置、marker 或启动游戏；运行时文件快照不是原子采样。复制当前公共 collector 的其他日志 adapter，保持它们的原有边界；这不是 DX11.0 新成果合并。该入口使用渲染增强版 ring 解析器，原 `collect-diagnostics.ps1` / `summarize-diagnostics.py` 不被覆盖。

| 诊断问题 | 结构化数据 / 入口 | 证据边界 |
|---|---|---|
| 高分辨率、尺寸与分配失败 | ring v1 `frame_contract`/`nr_memory`/`allocation_failed`/`device_lost`；`summary` | CPU 契约与进程占用不能证明模型输出或峰值 |
| 遍数回退与历史重置 | C2 `nr_outcome`、`mp_*`；`summary` | requested/ready/evaluated/composed 分开；汇总溢出不是成功或失败帧 |
| 显存与资源准入 | C3 内存事件；`resources` | 进程占用不等于模型权重占用；查询缺失不记零 |
| 队列与提交 | C4 `nr_queue_history`，fence target/completed | 过去 Execute 只用于队列选择，不等于当前 GPU 完成 |
| 色彩与逐遍像素 | C5 编码事件、C7 capture manifest；`layers`/`compare-layers` | 完整性、有限值、逐阶段哈希与视觉结论分开；3–4 遍像素采集仍未覆盖 |
| 历史反馈与 Reset 边界 | C8–C11 CSV/manifest；`feedback` | 受控宿主结果，不是运动游戏修复；研究已挂起 |
| 实际 runtime 状态 | C12 `runtime-state`，C13 `runtime-bindings` | requested/effective Reset、观察覆盖和像素无扰动检查分开 |
| 计数干预 | C14 `counter-isolation` | 显式干预，不冒充只读观察；未构成可交付修复 |
| runtime 静态调查 | `audit-runtime` / `audit-state-access` | 只读分析候选引用，不执行/补丁 runtime |

## 研究能力保留

`gpu-feedback` 和 `gpu-resources` 必须显式 `--allow-gpu`，仍检查 runner 自身要求的 runtime/forwarder 哈希、固定尺寸、帧数和超时。不要附加到游戏进程；不要自动运行。C6–C14 探针源码/失败记录由完整候选与报告保留，研究 host 源码另收录在本地交接工具快照 `research-source` 中。RVA 与实验假设不能直接移入通用产品代码。

共享实例的原始历史反馈路线已暂停，发布版有效行为固定为独立历史。诊断版保留已知会闪烁的共享开关；各遍参数一致只是必要条件，不能保证稳定。未来如重启研究，先完整识别并隔离各遍时序状态；不再继续 Reset 变体。C10 语义不同，未进入主线。

## 发布版/诊断版及 API 边界

两者均为优化的 Release 编译。`D18DiagnosticBuild=0/1` 区分可请求的重型像素采集、model bypass 与共享历史研究；Summary/Trace 和正常高分辨率/独立多遍保留。DLL 纯导出身份由 `verify-render-profiles.py` 静态验证，不加载 DLL。

DX12 高分辨率/独立多遍是本次产品范围。DX11/Vulkan 保留既有功能；原生 capture/bypass 在诊断版可请求，但当前适配器本身的限制仍有效。旧 `D24VulkanNR.enabled` 是实际 Vulkan NR 功能启用条件，不能当作纯诊断 marker 禁掉。本轮没有声称所有历史 marker 都已隔离，荒野阶段/回放/debug-layer 的最终构建隔离须在另一任务交接后完成。

最终统一集成时，将增强 ring 字段合并到双方最新公共解析器，保留另一分支新增字段和测试，再收敛入口名字。现在提供独立入口和完整快照，避免覆盖并行工作。
