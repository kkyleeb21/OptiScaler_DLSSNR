# C4 SR 队列历史修补

原先只在列表第一次出现在 SR seam 后才建立专用提交历史。即使该列表更早已真实 Execute，raw observed 也只作诊断，第一次识别仍会跳过。C4 在现有实际 Execute 返回后的 observer 中，新增最多 512 个最近 direct command list 的 COM pin、queue pin、canonical device identity、提交时刻和多队列标记。

首次 SR 识别可以将相同仍被 pin 的接口对象历史提升至既有 256 项 SR map，提升前再次核对设备身份，仍使用原 freshness / owner / ambiguity 策略。LRU 淘汰只使证据失效，不能放行未知列表；已提升历史由 SR map 持有，后续实际提交仍更新歧义状态。Shutdown 只清理新增 recent map，现有 SR pins/pending tickets/NR lifetime 机制不变。

常见已缓存、同 queue 的观察只更新时间，不每次调用 GetDevice。新身份/队列变化才重新校验 device。Off 不记录诊断，但正确性所需历史观察继续；这不是通过跳过 fence 提速。

## 诊断

原 C2 nr_outcome 的通用 submission_queue_pending 改为具体拒绝原因：queue_unknown_list、queue_ambiguous、queue_history_stale、queue_clock_invalid、queue_device_mismatch、queue_observer_unavailable、queue_sr_list_limit、queue_list_not_direct。均沿用既有一秒最多 16 签名聚合与计数，默认 Off。旧日志旧原因仍能解析。

新增 nr_queue_history，reason=promoted_pinned_execute，仅成功提升时记录（每个 SR map 身份一次，受既有 256 项上限约束）。queue/commandList 为实际历史对象地址，fence 字段为 0；这是选择队列的历史证据，不是本次 GPU 完成。相应 JSON/Markdown 在同目录共享 summarize-diagnostics.py/collect-diagnostics.ps1 中整合。

历史 Execute 不会替新建 ticket 设置 submitted，也不会解除未完成 fence；不能用 display queue、raw 地址、时间流逝、单纯提交或推测同设备来冒充 GPU 完成。

## 边界

从未实际观察的列表仍会跳过。缓存被淘汰后也可能重新等待；SR map 的原有 256 上限保留。队列历史无法保证游戏未来绝不换队列，后续观察到多个队列仍拒绝。此修补仅消除一类“已有可信过去提交但丢失继承”的等待，不承诺消除全部闪烁或修复模型共享光影。

本次 WARP 只执行空 command list 及 fence 等待门控，不加载 NR 模型；真实模型动态画质及异环行为仍待用户短时验证。UI、guide、history 算法、合成和 C3 三项优化均保持。
