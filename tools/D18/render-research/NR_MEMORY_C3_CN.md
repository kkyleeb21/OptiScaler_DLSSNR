# C3 显存与初始化优化诊断

共享收集入口仍为本目录 collect-diagnostics.ps1，解析器必须与 C3 同版。常规诊断默认 Off，用户自行选择 Summary。NR 模型、runtime、guide、history 策略和最终合成算法不变。

## nr_memory（原 schema 1）

生命周期阶段 before / created / gpu_ready 表示额外实例创建前、Create 返回后、创建 ticket 实际完成后。admission 为准入失败；scratch / highres 是各自预算检查。只有必要预算查询在 Off 时保留，额外两次观测在 Off 时不查询。

每秒最多 16 个事件，跨 device 共享上限。超出部分在下一个事件触发的新窗口用 overflow;n=数量报告；关闭进程前未开启新窗口的尾部 suppression 可能未报告。固定 ring，不抓图、不等待 GPU、不修改预算余量或自动重试逻辑。

本事件的 queue 槽保存 adapter LUID，commandList 槽保存被 COM pin 的 canonical device identity；不是实际 queue 或 command list。fenceTarget / fenceCompleted 始终为 0，不能作为 GPU fence 事件解析。

flags bit 0 = 查询有效，bit 1 = 准入通过（仅 before/admission/scratch/highres 阶段有此语义），8–11 位 = 一基遍序号，0 为公共 scratch。result = 查询 HRESULT；缓存失败退避期间为 E_FAIL。featureGeneration 是附加实例代，首次准入被拒绝/公共 highres 未关联实例为 0。

reason 以 phase;b=预算;u=当前进程用量;p=拟新增表面字节;r=保留余量 编码，b/u/p/r 为无符号十六进制字节数。最大长度小于 96，不牺牲整数精度。查询失败解析为 null，不是 0。width/height 为工作 raster，networkWidth/networkHeight 为对应内部网络，ratio 保留请求契约。

共享 JSON 的 nr_memory 与 nr_memory_deltas 按 device/LUID/代/遍/尺寸配对；整进程用量差允许为负，不代表某一模型的独占分配，也不是显存峰值。没有 before 时不生成伪差值；没有事件明确是未观察。

创建时开了 Summary 才有完整配对；进入游戏后开启仍能收到 C2 nr_outcome，但已完成的创建不会补造历史预算事件。收集时同时附 build-manifest.json，关联固定 core/runtime/forwarder 身份。

## 缓存与零 MV

DX12 high-res/multipass 共享 adapter 缓存，按 canonical device 和 LUID 失效，Shutdown 释放 CPU COM 引用。优先已有 swapchain parent factory，缺失或不能匹配时沿用一次 cache-miss factory 创建路径以兼容无 swapchain 桥接。每次准入仍读新 Budget/CurrentUsage；查询失败不放行，查询适配器解析失败最多一秒一次重试（不新增模型自动重试）。

共享零运动纹理在新资源代首次使用时清零，记录初始化 ticket；只有实际提交且 fence 完成才跳过清零。未提交、仅提交未完成、device-removed sentinel、释放/尺寸/格式重建都不能借旧状态复用。纹理仍使用原格式与 subrect，MV scale 和模型参数不变。后续常规共享帧少一条清零 dispatch，descriptor 准入同样减少一个槽；模型调用数量不变。

C3 的 CPU 和 WARP 验证不证明实际 NR 模型画质、帧生成交接或异环动态稳定性。实际试用按两遍短时观察，无需四遍关闭 FG 长时间压力测试。
