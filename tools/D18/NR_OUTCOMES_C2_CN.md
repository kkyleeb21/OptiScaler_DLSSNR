# C2：Summary 中的 NR 实际记录结果

沿用 `Diagnostics=0/1/2`（Off/Summary/Trace）及原 UI 位置。默认仍关闭。

`nr_outcome` 表示到达 DX12 调度入口的一次 CPU 记录尝试，**不是已呈现帧或 GPU 完成证明**。
只有最终合成成功记录后 composed 才大于零；为零表示本次没有写入最终 NR 合成，保留 SR。
evaluated 表示成功返回的模型 Evaluate 数量，不包含失败调用。

Summary 固定保存每秒窗口最多 16 种结果签名并计数；高频两遍/SR 交替会得到两项计数，不靠限频丢弃其中一类。
第 17 种及更多签名计入 `summary_overflow`；不能把这些未知结果算作 SR 回退或成功。
开启/关闭诊断时、NR 关闭时、正常 Shutdown 时会结束已有汇总；进程被强制终止可能丢失尚未刷出的最后一个窗口。
Trace 记录单次尝试。既有约 1 MB ring 容量和 schema 1 不变。

新事件字段（仅适用于 nr_outcome，不改变其他事件）：

- frame：该窗口内此签名最后一次调度尝试序号，和旧 g_frames 不同，不能直接当成游戏帧号。
- width/height：取得输出描述时的输出分配大小；之前退出则可能为零。
- result：最终合成是否成功记录，1/0。
- flags：bit 0 有成功调用使用 Reset；bit 1 实际共享历史；bit 2 请求共享历史；bits 4–7 成功 Evaluate 的 Reset 遍掩码；bits 12–15 有效请求遍数，16–19 ready，20–23 evaluated，24–27 composed。
- reason：`原因;n=次数;s=NGX句柄ID;h=历史重置原因编号`。解析后变为独立 JSON 字段。原因代码最多 28 字符。
- h：0 none、1 first_use、2 source_changed、3 recording_gap、4 nr_disabled、5 source_released、6 renderer_reset、7 additional_pass_reset。
- s=0：调用者没有来源身份。来源使用 NGX 句柄 Id，而非每帧轮换的纹理地址。一个句柄内的多视图仍不能仅靠这个字段区分。

同一来源、同一 command list、同一未提交 ticket 的重复回调单独报告为 duplicate_recording_pending，不凭它判定跨帧断档。
这些重复回调仍计为调度尝试，因此统计数量不能直接除以屏幕 FPS。

`nr_skip` 在 Summary 中最多每秒一次；Trace 保持逐次。关闭 NR 不再被分析器误标为 runtime first_anomaly。
`mp_*` 事件仍保留；历史 C1 ring 无 nr_outcome 时明确表示未观察到，而不是零失败。

使用同目录 `collect-diagnostics.ps1` 收集。它调用旁边的同版 `summarize-diagnostics.py`，不再落回旧根目录工具。
查看 `nr_outcome_summary` 中的请求→合成分布、完整 SR 回退、降遍数、重置原因和溢出数；来源及共享模式详见 `nr_outcomes`。

这些记录可以缩小闪烁原因；无法单靠 Summary 证明共享模型内部 history 正确，或还原逐层像素。
