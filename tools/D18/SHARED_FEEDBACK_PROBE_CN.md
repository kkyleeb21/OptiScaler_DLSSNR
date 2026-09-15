# 真实 NR 共享状态宿主诊断入口

本工具组属于显式、离线诊断，不随游戏或 `collect-diagnostics.ps1` 自动启动。它使用真实 GPU 和用户本地 runtime，在独立目录输出小尺寸固定输入对照；不是游戏像素重放。常规诊断保持默认关闭、有界。

## 可复用步骤

1. `prepare-feedback-probe.py --baseline <完整候选目录> --probe-source <已核验的 C6 probe-shared-history-runtime.cpp> --output <不存在的新候选目录>`：复制完整源码并生成两遍宿主，不覆盖旧候选。模板替换有精确匹配断言；基线改变必须重新审查。
2. 可选运行 `prepare-anchored-probe.py <新候选>` 与 `prepare-fixed-input-probe.py <新候选>`，分别生成三遍隔离和固定输入资源对照源码。
3. `<新候选>/run-multipass-checks.py probe-shared-feedback-runtime probe-anchored-history-runtime probe-fixed-input-runtime` 编译需要的宿主；当前 runner 使用本机 MSVC/SDK 路径，迁移机器须调整并记录身份。
4. 确认游戏退出及 GPU 空闲后显式执行 `run-shared-feedback-probe.py --build <新候选> --forwarder <原 forwarder> --runtime <原 NR runtime> --driver <driver _nvngx.dll> --mode <模式> --passes <2或3> --style 2 --run <新的简单名称>`。runner 不查找/修改游戏，不负责判断进程是否退出，操作者须先检查。固定64帧/640×384，保存最后16帧；每进程90秒超时、GPU等待10秒，创建前要求2GiB预算余量，实际峰值不是由此保证。
5. `analyze-feedback-probe.py <同架构运行目录A> <B> <C> --output <新分析目录>`：检查完整性、输入哈希、运行约束和二进制身份；生成结构化 JSON 与每遍时序图。两遍和三遍、不同宿主可执行文件不能直接混作同一对照；跨宿主的精确输出哈希验证单独保存依据。

## 模式

| 模式 | 遍数 | 用途 |
|---|---|---|
| independent_batch | 2/3 | 每遍独立实例，实际传递上一遍输出 |
| shared_batch | 2/3 | 全部共用一个实例，实际传递上一遍输出 |
| shared_same_input | 2 | 共用实例，每遍都喂原图；只作诊断，不是递进叠层 |
| anchored_shared | 3 | 首遍独立，第二/三遍共用另一个实例；不等价于原共享模式 |
| independent_fixed | 2 | 独立实例，逐遍像素复制到固定输入资源 |
| shared_chain_fixed | 2 | 共享实例，逐遍像素复制到固定输入资源 |
| shared_same_input_fixed | 2 | 共享实例，每次将原图复制到同一输入资源 |

原 runtime/forwarder 采用固定 SHA256 校验，driver/exe 哈希写入 manifest。不要绕过身份检查使用不明版本。两遍约61.9MiB、三遍约91.9MiB像素/组，禁止复用已有运行目录。runtime 只复制到独立 `private-runtime`，不修改、不打包分发。

`d18-feedback-probe-v1` 清单区分运行退出、完整文件数与参数；分析器重新检查数据哈希和有限值。`d18-feedback-analysis-v1` 保留逐遍暗区标准差、暗区均值曲线、峰谷差与去趋势统计，不用平均值代替局部闪烁，也不将宿主 PASS 当作游戏画质验收。

本轮参考实例：[C8 工程报告](../../reports/D18_SHARED_FEEDBACK_C8_20260913/REVIEW_CN.md)。旧 C6 源码仍是可定位的夹具依赖，模板没有伪装为跨 API 通用实现；后续移植适配器必须分别验证参数 ABI、实际 GPU 提交与资源寿命。阶段事件/解析器以及诊断构建隔离继续在总体工具集中追踪。

## C9 固定第二输入与 Reset 隔离扩展

`prepare-feedback-isolation-probe.py --baseline <完整C8候选> --output <不存在的新候选>` 复制完整源码，生成 `probe-feedback-isolation-runtime`。`prepare-reset-isolation-probe.py <新候选>` 另外生成 `probe-reset-isolation-runtime`；用该候选的 `run-multipass-checks.py` 编译对应名称，不覆盖C8已验收宿主。

公共runner增加两遍专用模式 `feedback_live_independent`、`feedback_live_shared`、`feedback_frozen_independent`、`feedback_frozen_shared`、`feedback_live_split_params`。这些模式都必须显式传 `--frozen-input <640x384紧密RGBA16F文件>`，记录哈希并验证GPU上传/读回一致；即使live模式不使用固定图作为第二输入，仍分配/上传它以保持对照。每组34文件/63.75MiB；其余尺寸、帧数、超时保持原界限。

可选显式 `--reset-policy initial|every_call` 切换到单独Reset宿主。`initial`为原初始化行为；`every_call`仅为状态定位对照，会改变时序行为，不能把静态输出稳定视作游戏可用修复。没有此参数就使用原输入/参数隔离宿主，不改变游戏默认Reset策略。

旧清单 `static_input` 表示基底输入固定；live模式的第二遍输入来自当前首遍输出，仍可能变化。分析器按固定输入哈希、实际宿主二进制和其他运行约束配对，将policy单独标注；不同可执行文件的精确输出哈希对照保存在专项报告，不能越过身份检查混画为同组实验。新旧清单兼容，常规collector不自动调用这些GPU探针。

完整证据与限制见 [C9报告](../../reports/D18_SHARED_ISOLATION_C9_20260913/REVIEW_CN.md)。C9的 `shared-tools-snapshot` 保留本次实际脚本；后续升级在公共目录进行，原始源码/可执行文件/像素/清单不覆盖。

## C10 首遍时序保留与辅助实例重置

`prepare-temporal-anchor-probe.py --baseline <完整C9候选> --output <新候选>` 新增三遍宿主 `probe-temporal-anchor-runtime`；可选 `prepare-anchor-allocation-probe.py <新候选>` 再生成 `probe-temporal-anchor-allocation-runtime`，按候选编译入口构建对应名称。

共用runner显式 `--passes 3 --anchor-input constant|step48 --frozen-input <B图>`，模式可用 `independent_batch`、`shared_batch`、`anchored_shared`、`anchored_reset_aux`。最后一种仅在此宿主可用：首遍保持实例0的跨帧状态，后续使用实例1并每次Reset。不是当前游戏共享历史的同义选项。

默认创建3个实例以保持分配对照；显式 `--anchor-allocation parity|active` 使用单独分配宿主，active只创建该模式需要的1/2/3个实例，并记录created/active数量。它没有减少逐遍NR调用次数。日志local_usage是特定采样时刻的进程本地占用，不是峰值/游戏显存。

step48在第48帧把基底从A换为B，不额外Reset；清单 `input_schedule`与 `static_input`区分两种输入序列。分析器拒绝混合序列配对，step数据标为 `step_response`，不能把输入响应当作闪烁评分。每组三遍64帧、保存末16帧+A/B输入，50文件93.75MiB；常规collector不自动运行。源/脚本/日志及精确比较见 [C10报告](../../reports/D18_TEMPORAL_ANCHOR_C10_20260914/REVIEW_CN.md)。

## C11 Reset 边界与 runtime 静态状态调查

`prepare-reset-boundary-probe.py --baseline <包含C9宿主的最新完整候选> --output <新候选>` 只新增 `probe-reset-boundary-runtime.cpp`；使用候选的编译入口编译同名宿主。已有 runner 显式增加 `--reset-boundary --reset-policy initial|every_call|first_pass|second_pass`，仍需 `feedback_*` 两遍模式和 `--frozen-input`。first/second 表示每帧在哪一遍前请求 Reset；所有策略保留首次必要初始化。未指定 boundary 时旧 Reset 宿主只接受 initial/every_call，旧行为不变。

每组 128 条 BOUNDARY 日志包含 frame/pass/feature/params/requested_reset/frozen；是请求观测，**不是 runtime 最终有效 Reset**。尺寸640×384、64帧、末16帧和两输入34文件/63.75MiB，2GiB准入余量、10秒fence/90秒进程超时保持。`analyze-feedback-probe.py` 沿用原哈希/条件校验；不自动追加游戏采样。

静态入口：`audit-nr-state-runtime.py <runtime> --output <新目录> [--key <精确ASCII键>] [--function 0xRVA] [--call-target 0xRVA]`。只读文件，不加载或修改DLL；依赖公共 `tools/D24/ngx_audit.py` 的PE解析与现有vendored capstone。v2输出 `candidate_xrefs`、`functions.range_kind`、`rip_data`、runtime/反汇编哈希；最多32个范围、每范围256KiB、输入512MiB。候选字节引用需核对真实指令，无unwind条目时128字节fallback可能包含邻接函数；不能当成完整函数、完整API搜索或运行时命中证据。

五组边界结果与具体限制见 [C11报告](../../reports/D18_SHARED_REPAIR_BOUNDARY_C11_20260914/REVIEW_CN.md)。后续有效Reset观测及per-API适配仍待完成，游戏生产路径未改动。

## C12 有效 Reset / 内部字段观察

`prepare-runtime-state-observer.py --baseline <完整C11后继> --output <新候选>` 复制完整源码，只新增 `nr-state-debugger.cpp`；原边界宿主EXE保持哈希复用。用候选编译入口构建 `nr-state-debugger`。共享runner显式 `--reset-boundary --observe-runtime-state --observer-phase controls|state` 启用；controls观察控制变化前后Reset与重置虚函数，state观察实际清零字段及边界值。参数仍需完整runtime/forwarder/driver/输入身份校验，禁止直接用于其他runtime版本。

观察器只创建指定 `probe-reset-boundary-runtime.exe` 子进程，使用其主线程硬件执行断点，不附加游戏、不写代码或数据页。观察器改变调度时序，必须保留同宿主像素精确对照。最多768命中、64状态指针、每条256字节模型头、60秒期限；创建期重置独立记为call=-1，不能合入帧期统计。日志schema v1/v2、manifest中观察器/宿主/trace身份均保留；有缺失、损坏或异常则不视为完整。

`summarize-nr-runtime-state.py <run...> --reference <对应基线run，可重复> --output <新json>` 检查每组128次requested/pre-control/effective/after-gate配对、重置体命中、字段边界、哈希及输出精确一致；trace输入限1MiB。该首版摘要针对共享两遍/单活跃状态夹具，不能套到多实例或未审计状态布局上。

`audit-nr-state-access.py <runtime> --offset 0xC8 --with-offset 0x178 --output <新json>` 查找unwind片段中的成员位移候选，辅助现有静态反汇编工具；相同位移不证明同一对象，缺少unwind的叶函数可能漏掉。C12找到了0x616E0的字段清零及0x60D2F片段的输入门控/递增操作，证据和限制见 [C12报告](../../reports/D18_STATE_OBSERVE_C12_20260914/REVIEW_CN.md)。后续三个辅助输入的类型/写入者、完整history集合和跨API适配仍待定位，不能只改计数器就声称修复。


## C13 输入来源 / C14 计数器隔离（2026-09-14）

只读路径：原 runner 使用 `--reset-boundary --observe-runtime-state --observer-phase bindings|resources`。bindings 观测四输入、门控、GPU 参数块；resources 观测 history/MV 来源与特定历史复制调用。公共 `summarize-nr-runtime-bindings.py <runs...> --reference <逐组无观测对照，可重复> --output <新json>` 要求相同 host/runtime/driver/模式，核验128次配对及每组34文件完全一致。最终 resources 点为 0x21BB0/0x3F490/0x2292D，加上共同控制入口；只适用已固定的 runtime 和当前两遍夹具，路径没有覆盖会明确失败。历史 C13 的 history-ready 变体仅有复制标记，不是复制调用证据。

干预路径：`prepare-nr-counter-isolation.py --baseline <含C13只读observer的完整候选> --output <新候选>`；编译该候选 `run-multipass-checks.py nr-counter-bank-debugger`。在上述 bindings runner 完整参数后显式增加 `--counter-bank --counter-policy per_pass|second_cold_start`，固定 shared/style2/initial Reset。独立 EXE 只在自己创建的 boundary host 写一个 CPU u64 计数，最多128次；没有代码/资源指针/磁盘runtime修改。**这是 runtime 数据干预，不是只读观测，更不是游戏修复。**

`summarize-nr-counter-isolation.py <干预runs...> --baseline <原共享run> --independent <独立run> --output <新json>` 核验写入值、前后门控、GPU 参数及身份。`analyze-feedback-probe.py` 继续给出统一像素统计，并保留干预策略。切勿只凭暗区平均值改善宣称局部闪烁解决。C12 摘要与 C13/C14 adapter 按 schema/phase 隔离，拒绝错用。

全部入口默认不运行；主线程/60秒/768命中/1MiB trace 及宿主既有帧数、fence、GPU预算保持。无游戏附加，不改 Summary/配置。C13 六组观测各34文件完全一致；C14两组干预均仍有波动，不能作为部署版本。详见 [C13](../../reports/D18_INPUT_BINDING_C13_20260914/REVIEW_CN.md)、[C14](../../reports/D18_COUNTER_ISOLATION_C14_20260914/REVIEW_CN.md)。
