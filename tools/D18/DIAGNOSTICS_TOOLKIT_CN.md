# D18 共享诊断工具集

## C9 原生高级 NR（2026-09-15）

- `render-toolkit.py --allow-gpu gpu-native-instances -- --candidate <目录> --runtime-dir <目录> --output <证据>`：真实 DX11 FL11.0 实例隔离；可加 `--formats` 检查 7 种类型及模式切换，或 `--failure` 检查部分分配失败和恢复。
- `render-toolkit.py --allow-gpu gpu-vulkan-advanced -- --candidate <目录> --runtime-dir <目录> --output <证据> --formats`：真实 Vulkan 模型、生产帧链及合成 shader，6 种类型，独立/共享/高分辨率、分配失败保留原图、零合成强度及尺寸变化。宿主参数 fixture 不等于完整插件入口/游戏 hooks 验收。报告明确记录是否存在 validation layer。
- 运行库由调用者显式提供，工具只复制到证据目录，不下载或改写原文件。小尺寸、有限帧数、超时结束；不自动启动。
- `native-nr` 汇总 DX11 `native_advanced`；`vulkan-nr` 汇总 `D24VulkanDiagnostics.log`，区分录制、提交和完成。`collect-diagnostics.ps1` 已接入两者，Summary 默认关闭、有界，不自动开 Capture。
- `generate-native-advanced-shader.py` 与 `generate-vulkan-advanced-shader.py` 从指定候选共享 HLSL 生成两种原生着色器并记录来源哈希，供构建复现。


## 多遍风格归因（离线）

`render-toolkit.py layer-style -- <完整独立历史 layer-capture> --output <报告.json>` 复用现有抓取验证，比较同帧 pass1/pass2 的线性化颜色占比与区域空间对比度，不直接跨游戏域/代理域相减。最多处理既有八帧、五区域的有界采集，不启动游戏/GPU/model。红色通道占比不是感知色相；旧采集结果不能证明新版本当前皮肤失真的比例或天然必然性。此工具增量随 C9 同步到两种构建；冻结 C8 包不改写。

## C8 合成对照

`render-toolkit.py --allow-gpu gpu-neutral-compose -- --candidate <候选目录> --output <证据目录> --backend nvidia --all-mode-hf`：显式短时 shader 宿主，比较单遍与第二遍零改动的多遍/高分辨率最终合成。C8 用 `--all-mode-hf`，历史 C7 不传。人工解码已匹配生产 saturate；不加载真实 NR 模型、不代表游戏画质验证。HDR 的线性残差插值与先插值再解码仍有数值差异，结果单独记录，不能称全像素完全等价。格式矩阵现包含高频开关开启的生产多遍编排。

## 2026-09-14 C1 整合状态（优先于下方历史待办）

已合并渲染任务与 V37 诊断工具到总体 `tools/D18`，并快照到 `builds/D18_Integration_C1_20260914/toolkit`。`summarize-diagnostics.py` 现在统一解析普通 NR、C2–C4、多遍、高分辨率、Capture、分配失败和设备丢失；旧 `summarize-render-diagnostics.py` 仅作兼容入口，避免两个解析器继续分叉。V37 的 SR/NR/FG、分辨率和呈现暂态摘要仍通过同一个 `collect-diagnostics.ps1` 采集；各 API 的证据边界保持独立。

`python tools/D18/render-toolkit.py` 列出全部离线及宿主入口，增加 native-sr / native-nr / native-fg / native-scale / render-hang / command-lists / writer-journal。宿主 GPU 入口必须显式传 `--allow-gpu`。新增 `profile-policy` 是纯 CPU 检查；`gpu-native-fg` 支持 `--quiet`、`--diagnostic`、`--fl11-1`（候选 fixture 不支持时明确报错）。

发布版强制正常 SR 路径，保留 `D18WildlandsSR.enabled` 作为已有功能入口；旧阶段/子阶段、数值探针及 Debug marker 不再改变发布版行为。Summary 保留用户选择，并不自动启用像素或着色器归档。诊断版的计算/命令冲突归档另需在启动前显式提供 `D18ResearchCapture.enabled`，原有预算不变。研究开关不进入普通 UI。已有 addon 的有界初始化/错误日志继续保留，它与默认关闭的详细 Summary 分开。

两遍 Capture 和 C8–C14 研究入口与证据继续保留；共享历史仍是已知闪烁的研究项，不因工具归并而成为通过的功能。C6 原始宿主留作历史参考，后继共享研究使用已参数化的 C8–C14 公共入口，不自动重跑旧实验。

整合评审、来源/哈希与验证边界见 `reports/D18_INTEGRATION_C1_20260914/README_CN.md`。采集只读源目录，不启动游戏或自动修改 Summary；逐文件复制并非原子快照，旧会话文件也可能仍在目录中。


新增故障诊断应随修复一同归档到此工具集：统一采集入口、结构化事件、离线摘要和对应 API 适配器。不可仅留在临时游戏脚本或构建目录；未归并的部分必须明确记录。诊断保持默认关闭、有上限；缺少观察不等于不支持，CPU 返回不等于 GPU 完成，GPU 完成不等于画面正确。

## 统一采集

### 迭代收尾归并约定（2026-09-13）

用户再次确认：每轮新增诊断最终归入本工具集，逐步积累可复用的定位能力。候选目录里的同名 `tools/D18` 只是候选实现，不能据此宣布总体入口已接通。收尾逐项检查：采集/运行入口、版本与哈希清单、事件及离线摘要、API 覆盖、预算与默认关闭、故障样本及针对性验证；未接线或证据不完整须明确显示，不生成成功结论。

本任务流待归并清单（保持另一 DX11 任务流实现隔离）：

- C2–C4 multipass outcome、history/reset、显存和队列历史观察：候选已有事件与共享解析器；最终工具集须核对对应生产者/消费者版本，保留缺失观察与真实 GPU 完成的区别。
- C6 宿主与像素分析：当前位于 `builds/D18_SharedHistory_Probe_C6_20260913/core-source/tools/D18/`，包括 `probe-shared-history-runtime.cpp`、`analyze-shared-history-probe.py`、`SHARED_HISTORY_PROBE_C6_CN.md`；三组实际运行及原始证据已保存，见 [报告](../../reports/D18_SHARED_HISTORY_C6_HOST_PROBE_20260913_CN.md)。总体运行入口、候选外 runner 参数化及统一报告接入仍待完成；不自动执行 GPU 负载或复制私有 runtime。
- C7 游戏两遍 Capture：已在独立完整候选实现，并取得独立/共享两组完整实际游戏像素、GPU 完成及哈希证据；共享画质仍未通过。总体 `collect-diagnostics.ps1 -LayerCaptureDirectory <目录>` 已接入显式像素归档、前后哈希核对和分阶段报告；默认采集不复制像素。最多八帧、五个 128×128 区域，保留缺失阶段及来源/模式间断，不触发旧 Capture 的单遍限制。见 [C7 工具说明](../../builds/D18_Multipass_C7_20260913/core-source/tools/D18/LAYER_CAPTURE_C7_CN.md) 和下方夜雨成对分析。C8 已补显式宿主公共入口，C2–C4 解析器与跨 API 的总体合并仍单独跟踪。

最终在独立整合候选中合并双方工具及核心改动；以上登记是能力索引与收尾要求，不代表已完成代码归并或新增部署。

```powershell
& E:\DLSSNR\tools\D18\collect-diagnostics.ps1 -GameDirectory 'D:\SteamLibrary\steamapps\common\Wildlands'
```

输出保存在 `E:\DLSSNR\evidence\D18\时间戳`，包含原始日志、可用的摘要、collection.json 和 SHA256 清单。没有 D18Diagnostics.ring 时也可以独立采集 SR/UI/执行检查点和输入探针。工具只读游戏文件，不开启探针、不改游戏设置、不启动游戏。

最好采集游戏退出后的日志。运行中复制是独立文件快照，不保证同一时刻；尾行不完整时摘要可能报错，原始文件仍保留。空日志或无事件不能判为执行成功。每次使用新输出目录。

## 能力索引

C7 两遍成对分析：`python tools/D18/compare-layer-captures.py <A抓取目录> <B抓取目录> --output <新报告目录>`。复用已校验像素，报告参数差异、所有区域时序和明确阈值的共同稳定输入像素；不同时间的雨景/运动/曝光仍是混杂因素，不自动生成根因或画质通过结论。首个实机证据见 [异环夜雨对照](../../reports/D18_C7_NIGHT_RAIN_COMPARISON_20260913/REVIEW_CN.md)。

| 能力 | 数据 / 共享工具 | 状态及边界 |
|---|---|---|
| 通用 NR、分配、队列、围栏、输出矩形 | D18Diagnostics.ring / summarize-diagnostics.py | 已接入统一采集，保留提交与完成区别 |
| SR 初始化、评估、合成及阶段 GPU query | D18WildlandsSR.jsonl / summarize-input-sr.py | 已接入，当前事件适配器为荒野 DX11；不假装适用于所有游戏 |
| UI 切换、几何量、错误、诊断访问频率 | D18UiDiagnostics.jsonl / summarize-input-sr.py | 与 SR 摘要关联；计数不是游戏帧数 |
| CPU 检查点、线程、上下文、执行嵌套 | D18ExecutionTrace.jsonl / summarize-input-sr.py | 已接入；缺失检查点可能是采样预算耗尽 |
| 绘制覆盖拒绝与详细管线状态 | execution.coverage_snapshots | V6 已接入，所有拒绝位一起记录；详见下面协议 |
| 输入绑定、着色器身份、hook 覆盖、数值采样状态 | D18InputProbe.jsonl / summarize-input-probe.py | 已接入统一采集 |
| 数值二进制、着色器反汇编与输入关联 | summarize-input-values.py、disassemble-observed-shaders.py、join-shader-observations.py 等 | 已保留共享工具；二进制归档暂未自动纳入采集，按需使用，避免例行复制大文件 |
| query 未提交阶段与 Present 所有权验证 | test-wildlands-v5-state.py | 保留可重放 fixture；仍绑定 V5 提取路径，后续需参数化版本路径 |
| 裁剪/颜色掩码实际 DLL 验证 | prepare-wildlands-v5-contract-test.py、validate-wildlands-coverage-v6.py | 保留重现和检查脚本；版本化构建入口仍需归并为通用 host 参数 |

## V6 覆盖协议

沿用 `d18_execution` 结构，`sr_coverage_reason` 开始一组；随后 draw、raster、scissor、blend、depth、stencil_func 共七条。每进程最多八组，相邻至少一秒，服从 1024 条执行日志总预算；需 D18UiDiagnostics.enabled。这里只读状态，无纹理回读，不需要 F8。

拒绝位：1 主 Present 所有者缺失；2 Draw 形状；4 拓扑；8/16/32 GS/HS/DS；64 填充模式；128 裁剪；256 sample mask；512 混合/alpha coverage；1024 RGB 写掩码；2048 深度/模板。多个位可以同时出现。当前 Draw 参数字段针对非索引 Draw；其他入口的零值不能解释成真实 Draw(0,0)。

摘要保留原始记录和解码后的数量、起点、尺寸、拓扑、裁剪矩形、混合、写掩码及深度模板比较。未绑定显式光栅状态时 fill=0 代表未记录显式枚举。拒绝检查通过也不单独证明顶点覆盖全屏。

## 当前归并待办

V10 写回子阶段：7 只将原 RT1 复制到私有纹理、不发起重放 Draw；8 复制后重放到私有 R10G10B10A2 目标、不绑定游戏 RT1 作为该重放的输出。5 仍是重放到游戏目标。7/8 均不 prepare/NGX、不计作 SR 完成，沿用同一面板、切换排空和 per-stage GPU 事件，摘要 stage_timeline 已支持。私有离屏目标仅阶段8按需分配；本轮默认模式和原图状态不变。所有阶段仍共享上下文隔离、资源获取和部分初始化，不能凭一个阶段失败就断言某一条 GPU 指令是根因。

**高优先级工作方式（2026-09-13 用户要求）：后续 SR/FG/NR 类似故障优先使用阶段隔离面板。** 先根据证据排序可疑阶段，以依赖安全的组合缩小范围，再深入代码或添加细粒度子阶段；避免仅靠换 DLL 重复单变量崩溃测试。总待办已登记在 `reports/D24_CLOSEOUT_TODO_CN.md`，跨 API 归并仍保持未完成状态。

V9 面板：显式 `D18SrDiagnostics.panel.enabled`，当前由荒野 DX11 adapter 接线，优先于 V8 evaluate-only 标记；每次启动阶段 0、SR 关闭。共享阶段声明为 `SrDiagnosticStages.h`：0 仅观察、1 仅状态切换、2 输入准备、3 创建特性但不 evaluate、4 计算不写回、5 原图写回但不 NGX、6 完整 SR。0/1 仅 CPU 计数；2–6 有各阶段真实提交的 GPU 完成计数，5 不计入 SR compose。计数按阶段在本进程累计，不是用户停留时间或画面验收。

阶段请求在 Present 边界应用，等待已提交 query 排空后释放旧 feature/parameters，下一次调用按需重新创建，保留有界纹理资源；进入错误状态后拒绝继续切换，必须重启。降到阶段 0 不意味着卸载驱动/NGX 全局缓存；需要完全干净的基线时重启游戏。

事件：sr_diagnostic_mode_requested/applied、sr_diagnostic_cpu_completed/gpu_completed，另有 pipeline create、NGX init/allocate/create 子阶段检查点。共享摘要输出 stage_timeline 和 initialization_checkpoints，采集器同步保存诊断标记。事件服从既有 1024 条上限，UI 计数可继续更新；满额后缺失事件不能当作没有执行。

该初版采用七个依赖安全组合，不提供会跳过必要资源创建的任意开关。颜色/深度/MV 转换仍在一个 kernel 内，尚未拆成独立 GPU 子阶段；FG/NR 及其他 API 的面板接线列为高优先级后续，不宣称已完成。

V8 分段隔离：显式标记 `D18SrDiagnostics.evaluate-only`，当前由荒野 DX11 SR adapter 实现，默认不存在。启用后仍执行 prepare/NGX evaluate，但跳过向游戏 RT1 的合成写回；UI 明示诊断状态和 evaluation 计数，不能把该模式判为 SR 画面生效。事件 `diagnostic_evaluate_only_armed` / `diagnostic_evaluate_only_no_writeback`；摘要 `diagnostic_mode`、`gpu_stage_completion` 分别报告 prepare/evaluate/compose 的 GPU 完成数。槽位 issued=2，仅回收已提交的 query，不伪造 compose 或完整帧成功。后续 adapter 可沿用同一模式/字段，不得仅增加文件标记而没有接线。移除标记并重启恢复普通模式；游戏目录标记变更需纳入部署/回滚记录。

2026-09-13 V7 更新：新增共享 `dx11-draw-replay-host.cpp` 与 `test-dx11-draw-replay.py <build-directory>`，直接包含被测构建的 `Dx11DrawReplay.h`。五类几何回归验证非零 VB/IB 偏移、绘制起点、基顶点、常量缓冲范围、实例化、triangle strip 和范围外像素保留；工具不硬编码被测构建版本。SR 摘要新增 `replay_commands` / `replay_parameters`；覆盖位 4096 表示 stream output，8192 表示几何捕获不可用。V6 的位 2 指旧版 Draw 形状限制，V7 指不支持的绘制命令，不可跨版本误读。

V7 通用几何组件位于 `builds/D18_Wildlands_DrawReplayV7_20260913/core-source/OptiScaler/dlssnr/Dx11DrawReplay.h`。它替换合成阶段的固定全屏绘制，复用原 IA/VS/光栅状态和直接绘制参数。游戏的 SR 输入语义仍由 adapter 确认；间接绘制、DrawAuto、GS/HS/DS、stream output 和无法安全复用的状态继续回退。此候选尚待游戏验收，再同步到完整主线。

- 将仍位于实验 core-source/hooks 的运行时事件生产代码同步到最终接受的完整主线，连同版本、开关及预算校验；本轮不能提前将未验证 SR 接入发布主线。
- 将以上版本化 host/状态验证脚本参数化，保留历史故障样本作为回归输入。
- 为数值/着色器二进制归档增加显式可选的有界采集；目前不把日志副本称为完整二进制证据包。
- 完成荒野问题后进行用户指定的 DX11.0 / DX11.1 SR、FG、NR 一致性 review，并按 API adapter 接入新增诊断。

2026-09-13 验证：统一采集入口在 V6 scissor fixture（无 ring）成功生成 SR/UI/执行摘要，coverage reason=128；证据在 `E:\DLSSNR\evidence\D18\DiagnosticToolkitV6\scissor`。V6 核心诊断及实际 DLL 测试见 `reports/D18_WILDLANDS_COVERAGE_V6_20260913_CN.md`。

## V11 UI / SR 状态事务

共享 `hooks/D18ContextTransaction.h` 为 D18 UI、SR输入快照/执行、帧维护/退休提供非阻塞递归事务锁（保守地全局串行D18，未锁游戏API）。事件 `context_transaction_enter/busy/exit/state` 记录owner 1=UI、2=SR、3=维护、序号和saved/private状态身份。busy是跳过，不是设备错误。每次阶段切换最多24个采样事务、最多16轮；事务记录仍受全局上限约束。阶段独立ArmStage不消耗UI开关的6次窗口。执行日志V11上限4096条、共享解码仍限制1MiB；旧版上限1024，旧日志达到1024时仍可能截断，不能按新上限推断旧版完整。摘要新增context_transactions/transaction_busy_count，缺失配对不代表挂起。

隐藏UI仍可能做资源初始化和状态切换，本版协调其与SR的互斥，未删除隐藏帧处理。后续优化需拆分CPU帧处理/资源更新/Draw，不能仅按可见性跳过所有工作。高分辨率追踪、创建/纯Swap/Clear子阶段及游戏API并发覆盖仍为待完善项。

V11游戏反馈修正：阶段1仍崩溃，UI/维护可在启用SR前耗尽共享事务采样预算；context_transactions为空或没有SR owner不能证明无冲突。下一版必须按owner保留独立窗口，并在实际开始执行时开启，首次冲突另保留配额。证据及待修细节见 reports/D18_WILDLANDS_V11_STAGE1_CRASH_20260913_CN.md。

## V12 状态子阶段与采样修正

共享阶段9=EnsureContext（首次创建，随后复用），10=纯Swap/Restore，无ClearState；原1=带ClearState往返。三者均不提交SR复制/绘制/NGX/query，GPU计数为0。测试9必须新进程避免前序创建影响。10可以在9之后执行，出现错误后重启。

事务预算按owner 1(UI)/2(SR)/3(维护)分别保留24，busy各保留4；总Arm最多16轮和全局4096条/1MiB界限保持。SR在输入有效性检查通过且为非Observe阶段后调用StartSampling；只选阶段未启用不消耗其预算。共享解码新增context_state_checkpoints和9/10阶段名称。组件回归覆盖UI/维护耗尽预算后SR仍有24，以及未开始执行时不减预算；测试源保存在V12候选构建，后续可用同一组件复用。

## V13 显式私有状态复位候选

共享D18Dx11PrivateState.h只在D18私有状态激活时使用，逐项解绑OM/CS UAV、全部着色阶段SRV/CB/sampler/shader、IA/SO/RS、blend/depth/predication。UAV槽数按实际feature level选择11.0=8、11.1及以上=64，空initial-count指针保留资源计数。SR进入/退出及prepare→NGX、NGX→compose边界统一替代ClearState；UI旧路径不变以隔离变量。阶段1名称改为Explicit private-state reset，事件context_explicit_reset_scope_begin/restored，与历史ClearState事件区分。9/10保持原行为。适配器仍保留输入/覆盖/GPU完成与错误后回退契约。

## V14 手动快照与DX11 InfoQueue

UI/SR改用共享D18Dx11ManualState，移除旧context-state方案和9/10正常执行入口；1为手动快照/复位/恢复，事件context_manual_scope。历史日志9/10解码保留。D18Dx11Debug.enabled置于EXE目录，进程重启生效；默认不发布此marker。原flags/feature levels保持，debug组件缺失可退回原flags并记录。D18Dx11Debug.jsonl最多256条、队列描述最多16KiB，InfoQueue是否可用必须检查info_queue_status，未记录错误不能证明渲染正确。collect-diagnostics.ps1统一复制marker/log并调用summarize-dx11-debug.py；不清空游戏InfoQueue或改过滤器。代码整理/测试/部署/尚未完成债务见reports/D18_WILDLANDS_MANUAL_STATE_V14_20260913_CN.md。

V14现场修正：启动回归时debug仅create_requested无返回，16:12已撤掉游戏debug marker。采集器会复制旧进程遗留日志，必须核对各文件修改时间和当前启动时间；不能把sr-summary存在当作本轮SR执行证据。后续共同采集需要显式session身份/文件新鲜度标记。debug创建设备包装尚未游戏验收，勿默认用于其他游戏或发布。
# V15 preset 生命周期诊断（2026-09-13）

## V16：输入/输出尺寸分离与离屏验证

诊断marker `D18SrDiagnostics.upscale-output.enabled` 仅用于V16实验候选，强制关闭阶段面板选择并采用evaluate-only，不允许SR合成到游戏。主swapchain明确传递原生接口用于获取实际输出尺寸，输入仍来自已验证的时域资源/常量；公共 `SrResolutionContract` 拒绝下采样、明显宽高比不匹配及越界预算。它不是低分辨率后处理重接或可见SR功能。

`collect-diagnostics.ps1` 保存该marker，`summarize-input-sr.py` 输出 `resolution_contracts`，保留input、output及GPU阶段的不同含义。离屏模式正常情况下evaluate_done增长，而compose_done/completed/composed均为0。不能把0写回称作失败，也不能把evaluate成功称作图像已显示。marker必须排除在release包之外。

候选 `builds/D18_Wildlands_OffscreenUpscaleV16_20260913/test-sr.py draw6 offscreen-75 offscreen-67 offscreen-50` 使用实际DLL和GPU，覆盖不同输入比例、4K私有纹理、有限值读回、原游戏目标逐像素保持及UI/状态/UAV计数器恢复。私有纹理读回通过精确DLL map在独立测试进程内定位，绝不向实际游戏进程读取私有内存；此阻塞读回属于fixture，不进入运行时。正常路径仍不Flush/等待GPU。

## 分辨率接点离线追踪

`trace-resolution-chain.py <完整元数据jsonl> --index <disassembly/index.json> --seed-hash <目标shader指纹> --seed-slot 1 --output <派生结果.json>` 可复用到其他DX11探针profile。按同一帧/即时context的资源UID和声明槽连接候选，输出尺寸变化及view描述；不将尺寸增长当成主场景放大、执行证明或安全替换点。使用 `test-resolution-chain.py` 检查跨帧/覆写隔离。真实低比例采集优先复用现有F8元数据模式，按会话备份shader归档，避免为每个游戏重做诊断工具；后续仍需补子资源范围验证、完整覆盖和跨API adapter。

普通SR preset切换事件沿用SR日志及 `collect-diagnostics.ps1` 入口，新增 `sr_preset`（每进程最多64条），记录 requested、prepared_after_drain、created，以及hint、submitted/drained和时间。`summarize-input-sr.py` 输出 `preset_switches`、截断标记和未排空即prepared的异常列表；总SR日志记录上限由728扩至792，文件仍限制1MiB。旧日志保持兼容。created只说明带该hint的feature创建成功，不证明驱动/runtime实际采用该模型；缺少事件不等于切换完成。

实际DLL回归入口：`builds/D18_Wildlands_PresetV15_20260913/test-sr.py draw6 preset-cycle`。preset-cycle在4K真实GPU下请求多次切换、合并短时间重复请求并开关实际UI，检查GPU排空、状态/隐藏UAV计数恢复和feature退休；输出留在该独立候选的sr-test目录。这里保留可复用入口与模式约定，fixture向跨adapter公共runner迁移仍待完成，不将荒野fixture称为所有API验收。

## V17：私有后处理重放诊断（2026-09-13）

`D18SrDiagnostics.post-replay.enabled` 仅用于实验候选，强制离屏 SR；启动仍默认关闭，不写回游戏画面。共享 `Dx11ColorReplay.h` / `Dx11ReplaySurface.h` 提供受约束的原 Draw 重放和私有表面身份。profile 负责已反汇编验证的着色器语义，不能把此能力称为所有游戏自动兼容。

共享采集入口已保存此 marker，摘要器接受 `sr_post_replay`（最多96条，总上限888）。`replayed` 是私有 pass 数、`boundary` 是最终着色器访问数、`gpu_finished` 是私有 fence 完成数；三者都不证明完整后处理/可见画面正确。`query_slots_busy` 是有界跳过，在摘要 rejections 中列出，应与未知 writer/覆盖失败区分。缺失 Copy/Clear/compute 等修改观察仍是可见写回前的阻塞项。

复用入口：`builds/D18_Wildlands_PostReplayV17_20260913/test-replay.py` 测 FL11.0/11.1 混合、深度/颜色保持、拒绝条件与状态恢复；`test-sr.py draw6 offscreen-post` 测实际 DLL、原捕获最终 shader、4K私有读回及 UI。阻塞像素读回只在测试夹具；运行时不加入等待。`validate.py` 验证构建身份，`rollback-v16.ps1` 回滚此部署。共享 runner 迁移仍待办，候选完整保留；报告见 `reports/D18_WILDLANDS_POST_REPLAY_V17_20260913_CN.md`。

## V18：修改观察与逐帧统计

`sr_post_replay.accepted_frames` / `rejected_frames` 由共享摘要输出 observed_clean_frames / rejected_frames；旧日志字段缺失返回 null。accepted 仅为观察范围内的 CPU 帧关闭计数，与 boundary 次数和 GPU fence 完成独立。拒绝原因日志去重，不等于实际拒绝次数。

组合入口 `builds/D18_Wildlands_PostReplayV18_20260913/test-sr.py draw6 offscreen-post`：新增两次真实捕获 copy shader、目标复用及七类主动打断，保留像素、UI、状态和隐藏 UAV 计数检查。基础资源修改 hook 单独保存在 D18PostMutationHooks.inl，由诊断模式启用，仍不宣称扩展 API/全部依赖/全写入覆盖。共享跨 adapter runner 迁移与完整修改闭合仍待办。详细证据和后续批次见 `reports/D18_WILDLANDS_POST_REPLAY_V18_20260913_CN.md`。

## V19：清除后资源链观察

新增公共 `summarize-writer-journal.py <D18PostReplay.jsonl> --output <writer-summary.json>` 和 `test-writer-journal.py`；统一collector自动收集并汇总。最多256条/1MiB，保留同帧资源编号、clear四字/view/caller及后续读写，标记失效后观察和末尾不完整记录。不能将同资源编号当作view范围重叠证明，也不能从选定帧推断全部帧。

运行时原语 `Dx11WriterJournal.h` 位于V19完整候选，默认仅post-replay诊断开启，首三帧+每1000帧一帧；不保留资源引用、不读回纹理、不会在首次拒绝后停止观察。实际DLL测试 `builds/D18_Wildlands_PostReplayV19_20260913/test-sr.py draw6 offscreen-post` 验证Float/Uint清除值和失效后的记录；运行中共享读取也已验证。详细身份/局限见 `reports/D18_WILDLANDS_WRITER_JOURNAL_V19_20260913_CN.md`。共享跨adapter runner迁移仍待办。

## V20：计算声明与归档连接

共享 `analyze-compute-journal.py <证据目录>` 连接 D18PostCompute.jsonl/bin 与 D18PostReplay.jsonl，复用 disassemble-observed-shaders.py；collector自动复制有界CS归档并输出compute-summary、compute-disassembly。CS归档最多256条/16MiB/单shader256KiB。声明掩码与实际绑定分开，compute_bound_unused不当作真实写入；compute_declared_target仍只表示潜在访问。

候选公共原语 Dx11ComputeIdentity.h 与V20真实DLL测试验证反射声明、残留绑定区分、组数和仅RT目标私有初始化。详见 `reports/D18_WILDLANDS_COMPUTE_IDENTITY_V20_20260913_CN.md`。未来计算重放必须以同一份字节码/资源/组数证据为前提，避免单独补抓多轮；跨adapter runner及完整API覆盖仍待办。

## V21：剥离反射shader回归与按观察优先归档

不能用D3DReflect返回空资源列表证明“未使用资源”：游戏可能删除RDEF。V21从可执行声明提取资源槽/线程组/CB范围，未知声明保守返回unknown。`builds/D18_Wildlands_PostReplayV21_20260913/test-identity.py` 保留真实荒野剥离shader、有/无反射合成shader、空shader、非法输入回归；实际DLL测试同时验证CB5范围、相关shader优先归档和既有UI/像素契约。

dispatch_details末项2才代表可执行声明来源。analyze-compute-journal.py会统计legacy_declaration_dispatches，旧compute_bound_unused不能当作可靠未使用证据。compute_constant_buffer.copy_range前四项为first、count、声明向量数、ByteWidth，不是常量值。详细缓存/归档上限与身份见 `reports/D18_WILDLANDS_STRIPPED_DECLARATIONS_V21_20260913_CN.md`。

## C8：共享 NR 状态与输入反馈宿主（2026-09-13）

公共入口 [SHARED_FEEDBACK_PROBE_CN.md](SHARED_FEEDBACK_PROBE_CN.md) 连接 prepare-feedback/anchored/fixed-input 三个生成器、`run-shared-feedback-probe.py` 与 `analyze-feedback-probe.py`。显式选择完整独立候选、原 runtime/forwarder/driver、模式和新输出目录；不从普通 collector 自动启动 GPU。固定 640×384、64 帧、仅最后16帧读回，manifest 版本化保存哈希/参数/完成状态。支持两遍、三遍状态隔离和固定输入纹理对照，输出每遍曲线及局部统计；读取到有限值不等于游戏画质通过。

C8 九组真实 GPU 宿主已完成；三个固定输入纹理模式各自全部32张模型输出与对应原模式字节一致。首遍独立/后两遍共享只是定位工具，需要两个活动实例且不同指标未全面改善，不能称为原共享模式修复。完整证据 [C8 报告](../../reports/D18_SHARED_FEEDBACK_C8_20260913/REVIEW_CN.md)。源码夹具依赖、C2–C4 解析合并、跨 API adapter 与诊断/release 隔离仍待总体集成，保留另一任务所有诊断入口。

## C9：第二输入/参数块/Reset 状态隔离（2026-09-13）

两个生成器 `prepare-feedback-isolation-probe.py`、`prepare-reset-isolation-probe.py` 及原共享runner/分析器组成显式宿主扩展；`--frozen-input`记录并读回校验固定图，`--reset-policy`只在独立诊断宿主生效。固定两遍640×384/64帧/34文件63.75MiB，仍不由collector自动启动，不修改游戏设置。已完成11组真实GPU对照：分开参数块输出不变；取消第二遍实时反馈减少本轮波动；每次Reset后各遍末16帧各自逐字节稳定。Reset可能影响多类内部状态，不能宣称已经识别具体history、修好共享模式或通过运动游戏画质。候选保留脚本快照和逐文件哈希，入口详见 [共享说明](SHARED_FEEDBACK_PROBE_CN.md)，证据见 [C9报告](../../reports/D18_SHARED_ISOLATION_C9_20260913/REVIEW_CN.md)。

## C10：首遍时序/辅助实例与分配对照（2026-09-14）

公共生成器 `prepare-temporal-anchor-probe.py`、`prepare-anchor-allocation-probe.py` 扩展独立宿主；共用runner显式控制constant/step48输入和parity/active创建数量。三遍640×384、64帧、末16帧及A/B共93.75MiB，默认不执行。分析器将输入改变的响应单独标注，不当作闪烁指标。八组真实GPU验证首遍保留、逐遍不同模型输出与两实例精确对照；本机末端占用少310.3125MiB，但运动画质/4K成本未验收。不是原共享历史修复；游戏保持C7。参见 [C10报告](../../reports/D18_TEMPORAL_ANCHOR_C10_20260914/REVIEW_CN.md) 与 [公共入口](SHARED_FEEDBACK_PROBE_CN.md)，脚本版本保存在完整候选中。

## NR资源投入评估R1（2026-09-14）

`prepare-nr-resource-probe.py`、`run-nr-resource-probe.py`、`analyze-nr-resource-probe.py`形成显式资源诊断入口。实际创建1/2实例、两遍1080p/4K、48帧，16预热/32GPU时间戳样本，结构化CSV/manifest/摘要；常规collector不自动执行。记录进程本地显存与逐遍Evaluate时间，区分当前占用/峰值、NR计算/游戏FPS，不推测瓦数。详细使用和边界见 [NR_RESOURCE_PROBE_CN.md](NR_RESOURCE_PROBE_CN.md)，五组真实GPU测量与投入建议见 [评估报告](../../reports/D18_NR_RESOURCE_REVIEW_20260914/REVIEW_CN.md)。原共享模式仍未修复，任务优先级由用户决定。

## C11：Reset 帧/遍边界与静态状态入口（2026-09-14）

`prepare-reset-boundary-probe.py` 与共享runner显式 `--reset-boundary` 新增首/次遍前Reset对照，记录128条有界请求事件；复用像素分析入口。`audit-nr-state-runtime.py` 只读解析runtime，输出键/调用候选引用、范围类型、常量注释与哈希，不加载/补丁DLL。五组真实GPU宿主支持优先检查上一帧末遍→下一帧首遍的状态/图像反馈；Reset静态可稳定不代表保留时序的游戏修复。说明见 [共享入口](SHARED_FEEDBACK_PROBE_CN.md)，证据见 [C11报告](../../reports/D18_SHARED_REPAIR_BOUNDARY_C11_20260914/REVIEW_CN.md)。有效内部Reset/虚函数状态集合观测与跨API归并仍待完成；缺失观测不得记作成功，日常collector不自动执行探针。

## C12：有效 Reset 与实际字段链（2026-09-14）

公共 `nr-state-debugger.cpp` / `prepare-runtime-state-observer.py` 通过既有runner显式开启owned-host主线程硬件断点；`summarize-nr-runtime-state.py`核验requested/effective/body配对及C11像素一致性。`audit-nr-state-access.py`提供只读成员偏移候选，复用现有反汇编入口。创建期和Evaluate期、CPU状态和GPU完成分别记录，默认关闭、768次断点/64状态/60秒、摘要输入1MiB，不附加游戏或改runtime字节。三组完整观测排除本夹具的重复隐式Reset，定位逐调用计数及辅助输入门控；完整history/输入写入者尚未查明，生产API适配仍待整合。使用见 [公共说明](SHARED_FEEDBACK_PROBE_CN.md)，身份/失败尝试/结果见 [C12报告](../../reports/D18_STATE_OBSERVE_C12_20260914/REVIEW_CN.md)。


## 2026-09-13：V22 私有 clear / compute replay

- 可复用 GPU 测试入口：`python -X utf8 tools/D18/test-private-compute.py --source-root <OptiScaler源码目录> --output <证据输出目录>`。固定夹具位于 `tools/D18/tests/private-compute-host.cpp`，覆盖硬件/WARP 的 FL11.0/11.1、整块/带偏移常量的原始数据保护、私有 float2 更新、奇数尺寸 R32 纹理逐像素映射、重复使用和越界/尺寸变化拒绝。构建 /W4 /WX，保留源码和夹具 SHA256。
- 实际 DLL + 捕获的荒野 CS 组合夹具：`python -X utf8 builds/D18_Wildlands_PostReplayV22_20260913/test-sr.py offscreen-compute draw6`。与通用原语测试分开，包含 1800 帧、六轮真实 UI 开关、隐藏 UAV 计数器、游戏状态恢复、私有最终图像读取和主动注入的未知写入拒绝。它使用合成图像，不是游戏画质验收。
- 主采集入口仍为 `collect-diagnostics.ps1`；新增 `private_clear`、`private_compute` 和首次 `compute_enter/capture/prepare_begin/prepare_end` 阶段事件，继续使用总共 96 条 post replay 预算。共享 summary 将这些标为正常阶段，不能误报为拒绝，也不能把某阶段出现当作 GPU 完成。
- UI 的 Private clears / compute passes 是 CPU 提交累计数；accepted 是已有 hook 覆盖下的帧闭合；GPU frames 是查询完成；只有实际屏幕输出和画质测试才能证明可见 SR。V22 仍是离屏保护，未接 NR/FG。
- 私有颜色池最多 192MiB，新增辅助纹理最多 64MiB、常量缓冲有固定上限；默认关闭，保持原始 API 执行和失败回退。辅助纹理采用中心点最近邻映射，实际画面语义需要实机验证。
- 首次组合测试异常退出记录保存在候选的 first-compute-test-failed；修正新增 CSGetShader 查询的空输出参数后重跑。首次阶段记录保留为后续诊断，不将异常退出码本身当作 GPU 崩溃根因。
- 高优先级待办继续保留：诊断先做阶段/子阶段开关；诊断构建与 release 分开；本任务结束后 DX11.0/11.1 DLSS 系列一致性 review；其余历史实际 DLL 夹具逐步迁入共享跨适配器入口。


## 2026-09-14：V23 命令列表元数据与帧完整性

- 公共采集入口自动归档存在的 `D18CommandLists.jsonl`，调用 `summarize-command-lists.py <日志> --output <摘要>`。与既有 SR/UI/writer/compute 及另一任务的 layer capture 共存；不自动启动 GPU 捕获。
- 摘要区分帧起止、列表细节丢失、写入覆盖、Present 配对和 GPU/视觉验收。decision 0/1/2/3 分别是不相交、未知、改写链路、仅观察。记录理由非零却标不相交将报错，保留原始证据。
- 完整性测试：`python -X utf8 tools/D18/tests/test-command-list-summary.py <真实宿主日志> --work-dir <工作目录>`；覆盖断尾、中段损坏、错误放行、细节丢失及缺帧尾。
- 录制写入跟踪及元数据当前在独立 V23 候选的通用 `Dx11CommandListWrites.h` / `D18CommandListHooks.h`，候选实际 DLL 夹具验证不相交 draw/copy、相交 clear、Finish(TRUE) 状态、晚接入和超限。常规功能关闭；诊断标记启用后的跟踪有额外成本，尚无实机性能结论。
- 未采全纹理/常量数值、全部即时命令及管线状态；非普通 2D view 范围有限。存在文件上限和每列表细节上限；完整帧外壳不是完整 GPU 帧。详见 [V23 报告](../../reports/D18_WILDLANDS_COMMAND_LIST_V23_20260914_CN.md)。
- 待办保留：实际 DLL 夹具迁入公共跨适配器宿主；诊断/release 分离；阶段/子阶段优先；DX11.0/11.1 全链一致性 review。


## 2026-09-14：V24 临时扩额与高命令量回归

- `test-command-journal.py --source-root <OptiScaler源码> --output <目录>` 使用公共 `tests/command-journal-host.cpp`，测试无关列表压力、关键列表 4096 条记录、细节耗尽后的阶段专用额度及后续采样帧。默认不启动，由明确诊断测试调用。
- collector 与摘要支持 v2 临时 64 MiB/65536 条；v1 仍按原 12 MiB/8192 条验证。不把有意省略或截断当作完整调用流，缺 backbuffer 查询不推断身份相同。
- 扩额只在独立 V24 诊断候选，不进入 release 默认；采集后需恢复标准额度。具体预算、宿主结果及待办见 [V24 报告](../../reports/D18_WILDLANDS_CAPTURE_V24_20260914_CN.md)。


## 2026-09-14：V25 原生颜色交接验证

公共入口 `test-native-handoff.py --candidate <完整候选>` 与 `tests/wildlands-handoff-host.cpp` 固定源码身份，执行真实 DLL 的 native handoff/原离屏两组矩阵。测试私有颜色输入、原生小区域叠加、深度、绑定恢复、关闭/不支持采样器回退、UI 开关和 GPU 生命周期；合成夹具不作游戏画质验收。collector 归档 native-handoff marker；summary 分开 handoffs 和 handoff_gpu_completed，识别 native_handoff 为正常阶段。V25 模式停用扩额日志选帧，保留交接前保护。详见 [V25 报告](../../reports/D18_WILDLANDS_NATIVE_HANDOFF_V25_20260914_CN.md)。


## 2026-09-14：C13 输入来源 / C14 计数隔离

共享 NR 宿主入口 `run-shared-feedback-probe.py` 现有显式诊断路径新增 bindings/resources，分别由 `summarize-nr-runtime-bindings.py` 核验。新 `prepare-nr-counter-isolation.py` 与 `nr-counter-bank-debugger.cpp`、`summarize-nr-counter-isolation.py` 将**数据干预**单列；仅显式 `--counter-bank` 可运行，常规采集器不启动。结构化 trace/manifest 保留只读或写入语义、策略、覆盖/失败、哈希及真实GPU readback；C12 adapter拒绝把新阶段当有效Reset。

C13已定位历史颜色/MV和每遍临时输出写回；C14仅调计数与仅首次冷启动均有混合变化，未解决共享波动。下一步历史图像状态隔离仍待资源布局/寿命调查。无生产/游戏变更；当前适配是固定runtime的owned DX12宿主，跨API和生产通用接口仍待整合。使用/有界要求见 [公共说明](SHARED_FEEDBACK_PROBE_CN.md)，结果见 [C13](../../reports/D18_INPUT_BINDING_C13_20260914/REVIEW_CN.md)、[C14](../../reports/D18_COUNTER_ISOLATION_C14_20260914/REVIEW_CN.md)。

### V26 计算优先分支回归（2026-09-14）
`test-native-handoff.py --candidate E:\DLSSNR\builds\D18_Wildlands_BranchV26_20260914` 使用更新后的共享夹具，交替验证直接 compute/先 draw 再 compute，并执行归档的两支后处理变体。保留 V25 输入绑定恢复、原生深度/覆盖层和失败回退检查。
`alternate_blur`、`alternate_composite` 是有界的成功提交观测，不是拒绝，也不证明画质验收。交接前仍相交的 command list 继续回退；优先分析它的结构而不是盲目扩大整帧日志。V25 旧候选保留自己的固定夹具和 test-sr.py，可独立重跑。
详细边界与后续事项见 reports/D18_WILDLANDS_BRANCH_V26_20260914_CN.md。

### V27 自动短列表冲突快照（2026-09-14）
`D18CommandConflicts.jsonl` 是冲突 Execute 的快照，不是全帧 trace。由诊断预览路线的交接前相交触发，最多 3 份；扩额租期为首次触发后 3 帧且不超过 1 秒。首份可能截断，检查 trace_dropped、录制前缀和 selected_details_complete。
公共 collect-diagnostics.ps1 自动输出 command-conflict-summary.json；summarize-command-lists.py 的 passes 列表给出有序 PS/CS、观测 SRV 与输出影子绑定。未读取 payload，不包含完整混合/视口状态，不把影子绑定超集当作实际读写证明。复用 test-command-journal.py 和 test-native-handoff.py 进行预算、回退与 GPU 路线回归。
实现与缺失项见 reports/D18_WILDLANDS_CONFLICT_V27_20260914_CN.md。V26 及以前的固定候选仍使用各自保留的夹具，不覆盖它们的测试文件。

## 2026-09-14：渲染分支收口入口

高分辨率/C2–C14/R1 统一能力索引、离线 ring 摘要和显式研究入口见 [渲染工具集](RENDER_TOOLKIT_CN.md)。新增 collect-render-diagnostics.ps1 / render-toolkit.py，不覆盖另一任务使用的公共 collector/解析器。发布与诊断候选已分目录；完整跨 API 隔离仍待双方交接。原共享历史研究暂停。发布后数据保留流程见 [保留与清理](RELEASE_RETENTION_CN.md)。

### V28 局部原生图层保留（2026-09-14）
通用 Dx11NativeLayer 组件的逐像素测试入口：`test-native-layer.py --source-root <完整候选/core-source/OptiScaler> --output <证据目录>`，覆盖 WARP FL11.0/11.1 与 RGBA8/R10。未变化区域保留 SR；变化区域采用原生分辨率颜色，不等于高分辨率精确图层重建。UI/真实 SR 集成继续用 test-native-handoff.py。
新增 native_layer_merged 是 CPU 提交观测；native_layer_merges 由 summarize-input-sr.py 汇总。完整层操作的 GPU 完成仍随最终 handoff fence 判断。实机重点验收局部清晰度过渡与性能。详细证据与边界见 reports/D18_WILDLANDS_NATIVE_LAYERS_V28_20260914_CN.md。

### V29 可见 SR Preset 切换回归（2026-09-14）
共享 native-handoff 夹具增加 Default/L/M/K/Default/L/Default 提示值请求与 created 序列断言，并要求六次 prepared_after_drain 的 submitted==drained。修正 setter 对10参数可见交接模式的识别；没有真实创建序列时测试失败，不把无切换的普通运行算成功。仍保留原生图层合成与最终显示颜色检查。候选旧版本保留各自固定夹具。

## V30 SR 实际尺寸档位

`test-sr-quality.py --source-root <candidate>/core-source/OptiScaler --output <evidence>/policy` 验证标准/自定义比例及非法契约。`test-sr-quality-gpu.py --candidate <candidate>` 使用候选固定身份和共享host跑75%可见交接回归、五档离屏NGX输出回归。公共fixture新增100%、58%、33.3%输入尺寸，旧75%交接场景保持原行为。

`sr_quality` 创建参数事件最多64条，`summarize-input-sr.py`生成quality_parameters。档位请求、实际内部模型、GPU完成、可见画质四者分开判断。标准档位离屏验证不是全档游戏接入验收。长时间边界测试由用户延后，不据日志上限外缺少错误判断稳定。

## 即时分辨率重建与原生设置诊断（V31离线候选）

`test-sr-resize.py --candidate <candidate>` 通过独立保留的wildlands-resize-host.cpp，在同一进程切换四次场景尺寸，同时检查SR与私有图排空、UI/Preset、原生层合成和最终颜色。`sr_resize`为默认实验日志内最多64条事件，summarize-input-sr.py新增resize_transitions/unsafe_releases。GPU夹具改变资源不代表引擎设置UI已接入。

`inspect-pe-setting-refs.py <exe> --term <setting> --output <dir>` 生成静态候选/映像身份，支持额外data-va及target-va；没有完整反汇编覆盖保证。`native-settings-watch.py --profile <inventory.json> --output <dir>` 只编译并自测只读观察器；显式加--pid及--seconds才观察该GRW映像，最多60秒256条。当前对象RVA仅适用于该荒野映像；其他引擎必须另建并验证adapter，禁止盲用偏移。原生菜单实时确认由用户稍后进行，未部署V31。

原生设置观察离线汇总：summarize-native-settings.py <game-settings.jsonl> --output <summary.json>，按对象记录比例/尺寸变化、完整结束与上限/读取错误。时间差不能当GPU耗时，设置字段变化不证明RT已重建。

### 原生Apply调用线程诊断（2026-09-14）
`inspect-pe-setting-refs.py`新增`--unwind-va`，解析最多32层chained unwind；重叠记录返回ambiguous，不能据字节片段直接调用。`native-settings-watch.py --render-state`额外只读一次已验证游戏适配器的渲染比例、更新标志与窗口TID；窗口TID不等于Apply TID。

`native-apply-trace.py --output <dir>`默认只编译并跑独立命中/超时fixture；加`--pid <pid> --profile <含唯一unwind主入口的inventory.json> --rva <RVA> --seconds 60`才附加。这是**硬件一次性断点/调试器附加**，与只读轮询分开；不写游戏设置、不调用目标函数。最多一次命中24帧栈，命中或超时均detach。实际采集前要求目标映像全SHA匹配和入口核验；按原生动作等待命中，不把任意热函数地址当稳定API。`summarize-native-settings.py`自动汇总trace_armed/native_apply_hit/trace_end，保留未命中和错误边界。

实现约束：DbgEng拥有breakpoint接口，ONE_SHOT移除后不可再Release；必须消费硬件单步事件再detach，避免向被调试进程遗留异常。fixture包含超时后再次调用目标，确认无遗留断点。此公共外部诊断默认关闭；Windows/DX11荒野地址属于单独适配，跨API/其他引擎不能复用偏移。游戏中观察尚待执行，仍不能据此宣称即时SR已完成。

### 覆盖前节：native-apply-trace禁止实机附加
首次荒野附加时用户未操作就崩溃，Apply未命中。现wrapper拒绝--pid，原生工具仅允许synthetic fixture；先前实机命令不可再用。夹具通过不证明游戏接受调试附加，live_enabled=false。保留失败资料和工具作为诊断研究，不作为生产/实机采集入口。证据game-apply-48628/review_CN.md；不重复同一实机尝试。

### V31 原生队列档位诊断（2026-09-14）
`D18NativeScale.enabled` 显式开启最多128条 `D18NativeScale.jsonl`；默认不写日志。记录请求代次、窗口/回调线程、实际输入尺寸与SR GPU完成。公共 `collect-diagnostics.ps1` 自动保全并输出 `native-scale-summary.json`，也可直接用 `summarize-native-settings.py <log> --output <summary>`。汇总按代次区分 native_finished / input_observed / sr_gpu_completed，不能以一次API返回冒充完整生效。
复用夹具：`test-native-scale-queue.py --source-root <候选/core-source/OptiScaler> --output <证据目录>`，覆盖线程队列、参数寿命、超时、原生限制和与SR开关独立的请求状态；它是模拟引擎测试，不证明GRW实机覆盖。真实GPU尺寸回归继续使用 test-sr-resize.py；本版固定候选还保留 test-sr.py。详见 reports/D18_WILDLANDS_NATIVE_QUEUE_V31_20260914_CN.md。
外部 native-apply-trace 仍为fixture-only，不因新队列方案通过离线测试而重新启用实机附加。待办：将通用状态和事件契约纳入最终跨API诊断/发布分离交接，荒野地址指纹保持引擎适配层。

### V32 比例提交与编辑草稿保护
队列夹具新增 `--editor-merge`（V32+），验证只提交已应用设置中的新比例，保留无关草稿、已有比例/分辨率草稿和应用期间的新编辑。省略该参数仍支持V31历史行为。
`native_scale_context` 与 `native_scale` 共用128条预算，新增窗口匹配、设置状态、pending、首个差异字段偏移/原始位值。summarize-native-settings.py 按generation附带contexts，公共collector入口不变；零差异必须结合settings_state非Unreadable解读。failure 6/7/8/9/10分别是窗口/可读性/忙碌/尺寸/比例检查。编辑对象差异用于诊断，V32不再以整个对象相等作为比例提交前提。
待办归并：通用生命周期与结构化事件继续纳入诊断/release分离交接；游戏字段偏移和原生调度证据保持在引擎适配层。画质模糊反馈按用户要求暂缓。

### V33 同尺寸颜色交接
`test-same-size-colour.py --source-root <候选/core-source/OptiScaler> --output <证据目录>` 验证共享颜色资源契约及FL11.0/11.1逐位RGBA复制。`test-same-size-handoff.py --candidate <V33及后续兼容候选>` 运行实际DLSS/后处理末端交接夹具，省略原生放大draw，验证后续151-draw局部覆盖、深度、UI/Preset、GPU完成。字节码由候选prepare-fixture.py从已归档证据提取并校验。
新事件native_same_size_handoff纳入公共SR摘要成功阶段，沿用handoffs/handoff_gpu_completed与原生scale代次。事件不是实机全场景验收。游戏shader与后处理时序留在adapter，通用颜色拷贝契约不含游戏hash；未扩大诊断预算。研究版与release隔离/阶段隔离优先仍保留最终交接待办。


## SR → DX11 NR 桥接回归（V34）

`test-native-nr.py --candidate <保留候选目录> --output <证据目录> --case same-size|resize|failure` 是统一入口：同尺寸DLAA、低比例热切换、NR私有输出污染后失败三类GPU回归，归档原始日志、构建身份及断言结果。failure使用工具集里的 `tests/native-nr-failure-addon.cpp`，仅在隔离夹具目录装载，禁止部署到游戏。

`collect-diagnostics.ps1` 自动保存存在的 `D24Native.log`；`summarize-native-nr.py` 结合 `D18ExecutionTrace.jsonl` 的 `native_nr_return` 汇总输入格式/尺寸、NR完成观察及错误。`rendered_frames` 是本次模型生命周期内计数，重建会归零；桥接返回1之后仍须用SR交接query确认可见链路GPU完成。文件可能来自旧进程，先核对采集时间和构建身份。没有日志不表示API不兼容。

诊断仍使用现有有界日志，生产默认关闭详细输入记录。测试夹具显式设置 `[DlssNr] Diagnostics=1`（数字，不是布尔true），不改游戏配置。后续将该桥接回归纳入公共DX11适配器验收，并在最终DX11.0/11.1一致性review中保留FL11.0、FL11.1、UI状态与失败回退覆盖。

## Native DX11 FG 接入（V35）

- `test-native-fg.py --candidate E:/DLSSNR/builds/D18_Wildlands_FgV35_20260914`：480帧SR/NR/FG呈现共存短测、FG开关、状态与像素检查。使用用户本机runtime，只用于内部夹具。
- `summarize-native-fg.py <OptiScaler.log> --output <summary.json>`：区分 inputs accepted、SL query/status、额外生成帧观察。公共 collector 自动产生 native-fg-summary.json。
- 输入成功不是FG生效；SL计数是自上次查询以来，其他调用可能消费计数。V35逐Present查询并聚合，原有 app_presents 仅日志间隔。真实游戏与画面验收单独记录。
- 默认Diagnostics=0不输出这些例行文本；V35配置暂开1。输入状态/尺寸变化与5秒心跳，共256条；present摘要沿用有限预算。不可改成无界每帧打印。


## 渲染卡死离线关联（V35实机回归）

V35真实荒野开启SR后出现完成等待超时，已回退V34。480帧host不覆盖游戏并发提交。下一次需补渲染/呈现分线程及输入消费未完成路径。

`summarize-render-hang.py <归档目录> --output <summary.json>` 已接入collector，关联现有NR完成错误、返回与间隔、最后SR事件，记录文件hash/mtime和空FG日志。默认只读取现有日志；CPU栈与日志不直接证明GPU死锁根因。后续FG同步诊断使用独立有界事件，不依赖可能关闭的文本logger，继续公共结构化摘要/API适配器归并。


## FG输入消费完成与卡死隔离（V36）

`test-native-fg.py --candidate <V36候选>` 使用分开的渲染/Present线程，延迟消费fence二十帧，验证FG停止复用输入而SR/NR继续、信号释放后恢复。`--timeout`保持消费fence未完成，验证两秒超时仅停用FG输入路径，后续SR/NR仍运行。夹具只访问自身精确DLL/MAP内的状态并替换夹具消费fence，禁止对游戏进程运行这种故障注入。

D18NativeFG.jsonl是独立有界日志，Diagnostics=0时关闭，不依赖OptiScaler.log。阶段prepare_begin/end、tags_dispatched、consumption_fenced/complete、input_pending、stopped、toggle；记录线程、代次、队列、两类fence/value和提交/跳过/Present计数。最多512条，前16代阶段记录，后续5秒稀疏记录，错误/开关仍受总预算限制。collector自动归档，并由summarize-native-fg.py的--journal入口生成sync_journal，即使文本logger不存在。缺少阶段仍不能当未执行；prepare_end code=1也不代表有额外生成帧。

错误码：-1队列/设备身份变化，-3创建完成fence失败，-5队列Signal失败，-6Present/GetState或队列归属无效，-7标签命令列表关闭失败，-8消费fence报告设备移除，-9消费超过两秒。保留首个错误，UI提供重启重试提示，不把任何错误都说成死锁。

公共归并继续保留：接入其他API时复用生产/消费代次与完成证据语义；诊断/release分开，以及DX11.0/11.1最终一致性review仍为待办。本轮是V35卡死的定向回归，不替代用户延期的全边界长测。


## 呈现暂态恢复（V37）

新增present_retry、present_resumed、recovery_complete。present_hr、sl_query、sl_status和observed_queue保留原始返回与实际队列；present_resumed仅表示呈现有效，recovery_complete才表示该批输入的消费fence也已完成。公共摘要保留完整恢复事件及已知HRESULT名称。

正数DXGI状态和WAS_STILL_DRAWING进入WAITING，不按GPU两秒超时误判；有效呈现后重新给消费fence两秒完成窗口。GetState错误/非零runtime status允许两秒有界恢复；不能无限延长错误窗口。-10为失败Present，-11为队列/fence身份错误，-12为持续runtime状态/查询错误；-9仍为真实消费超时。默认有界诊断规则不变。

`test-native-fg-present-policy.py --candidate <候选>`编译并运行`tests/native-fg-present-policy.cpp`，归档策略与测试源码hash，覆盖MODE_CHANGED、长遮挡、背压、query/status恢复、持续runtime错误及设备移除。V37公共test-native-fg入口额外在夹具边界模拟MODE_CHANGED和非零SL状态，验证停止复用→实际Present恢复→GPU消费完成→输入继续；保留V36延迟和--timeout检查。夹具模拟不是游戏120→60调用链的复现，实际原始返回仍需用户实机验证。

## C2 发布成本收口与安装依赖（2026-09-15）

`test-command-trace-gate.py --candidate <C2>` 验证 release 无论研究标记是否残留都不分配详细 Trace；diagnostic 仅启动时显式研究标记启用。生产写集/绑定/操作计数/前缀和溢出拒绝仍保留。`summarize-input-probe.py` 保留 input_creation_policy 的 required_stage_mask/attached_stage_mask，区分生产 PS/CS 覆盖和研究六阶段观察。F9 研究入口只属于诊断构建并需显式面板开关。

安装依赖工具统一放在本目录：test-installer-auto-dependencies.ps1（离线边界）、test-installer-auto-workflow.ps1（仅项目 reports 下新建隔离夹具，实际 prepare/check/install/upgrade/uninstall）、test-installer-auto-ui.ps1（实际 WinForms 回调与 worker，不部署游戏）。安装器输出 d18-dependency-preparation-v1 的逐项 id/status/中英文说明、计划和 ready 状态；worker success 只表示操作完成，全部依赖是否就绪必须看 data.ready/rows。完整项目证据见 reports/D18_C2_INSTALL_20260915/AUTO_DEPENDENCIES_CN.md。下载缓存与 runtime 不纳入公开包。

## C3 安装缓存生命周期（2026-09-15）

`test-installer-cache.ps1 -Installer <D18目录> -Output <报告目录>` 验证会话锁、成功安装凭据、安装文件复核、按哈希清理、路径越界拒绝、未知/变化文件保留，以及用户原始 NR 在缓存内外均被保护。GUI `D18-Setup.ps1 -CacheSmokeDirectory <隔离目录> -SmokeRuntime <本地NR>` 验证安装完成页默认勾选、可操作的清理选项及 Finish 回调；只用于项目隔离夹具，不对真实游戏做此注入式测试。

结构化 `d18-cache-cleanup-v1` 回执记录每个路径、SHA256、状态、原因、删除逻辑字节数和保留数量，位于 InstallerLogs 对应会话；安装器业务代码统一在 D18-Cache.ps1。其他会话不共享可清理目录；清单外/链接/哈希变化/原始NR均不删除。无成功安装凭据或原安装文件发生变化时保留缓存。完整两游戏安装→卸载→重装和清理证据位于 reports/D18_C3_CACHE_INSTALL_20260915。原始NR输入保护是硬约束，不属于可清理下载副本。
# C4 补充（2026-09-15，优先于下方历史构建限制）

发布版和诊断版均开放实验性共享历史，默认关闭，已知可能画面闪烁；发布版 capability 为 9、诊断版 15，pixel capture/model bypass 仍只限诊断版。`verify-render-profiles.py` 同时识别历史 1/15 和当前 9/15。

共享 `mp_contract` 事件记录实际 DX12 backend、target 格式、通用 API 标志及请求/有效遍数；诊断默认关闭，仅签名变化记录，进程上限 32 条，不增加 GPU 等待。`summarize-diagnostics.py` 单独汇总 multipass_contracts，不将其格式值误认 NGX 错误。`test-multipass-backend-contract.py --candidate <候选> --output <证据目录>` 覆盖原生 API 未设定、格式拒绝、高分辨率单遍及两种构建功能隔离；这是 CPU 契约验证，不代表游戏画质验收。

## C5 原生 SR 启动与 FG 热开关（2026-09-15）

`native_sr_progress` 写入共用 D18Diagnostics.ring，Summary/Trace 显式开启后每 2 秒最多一条、每进程 128 条。`summarize-diagnostics.py` 输出 native_sr_progress 与门槛说明：关注 target 观察年龄、eval/GPU/handoff 计数变化、coverage 原因及比例代次；采样之间短暂状态可能未观察到。ring 旧头部 backend 固定字符串不能证明实际 API，以事件具体后端和输入观察为准。没有记录不能判定启动正常。

`test-native-controls.py --candidate <完整候选> --output <项目证据目录>` 编译实际呈现能力判断，校验四个交换链创建入口共同采用同一规则，覆盖 SR 未初始化、NoFG/其他呈现 API、首次设置、准备后热开关和共享 ring 二进制解码。CPU 检查不运行 GPU。

`test-native-fg.py --candidate <完整候选> --start-off` 使用候选保留的 GPU 宿主，从 SR/FG 都关闭启动，在 SR 之前热开 FG，再验证 480 帧输入提交、开关和消费恢复。运行库仅从本机已授权依赖读取，不包含在工具包；宿主不代表游戏画质或额外生成帧验收。首次普通 DX11 交换链到 FG 交换链的热替换仍未实现；已选择路线重启准备一次。
# C10：通用 DX11 输入观察（2026-09-15）

候选 `builds/D18_C10_Dx11Discovery_20260915` 在诊断构建且存在 `D18InputProbe.enabled` 时允许未知游戏使用已有输入探针；release 仍不会因该标记启动通用探针。未知游戏不能启用荒野渲染规则或其 numeric 固定指纹采样，即使残留这些标记也不启用。常规默认关闭。

进入实际场景后按一次 F8，15 秒内分别静止、平移、转镜头；至多 3 次触发，每次最多 864 个绑定观察。着色器归档上限 64 MiB、散列预算 96 MiB，保留 hook 覆盖与丢样记录。首轮只观察绑定/尺寸/格式/着色器身份，不执行 SR/NR/FG 或纹理数值回读；不要仅凭两个通道或尺寸就称找到运动矢量。已存在的 SR/NR 功能仍由用户配置控制，此探针本身不为未知游戏添加渲染路径。

沿用 `collect-diagnostics.ps1` 收集 `D18InputProbe.jsonl`、`.shaders.bin`；通过 `render-toolkit.py input-discovery -- <日志> --output <摘要>` 解析，后续使用 `disassemble-observed-shaders.py`、`audit-shader-inputs.py`、`trace-resolution-chain.py`。`input_discovery_policy` 记录实际运行策略、PID/tick，摘要明确 SR/FG 未验证。测试 `tests/input-discovery-policy.cpp` 编译期覆盖全部 32 种标记/构建/已知适配器组合；它不是实际游戏 hook 验收。

只在独立诊断候选按游戏部署，保留 release 核心备份；采样后恢复 release、移除本次诊断标记，并核对安装清单。不要发布研究 marker；根工具增量应在下次 release/diagnostic 打包时统一收录。

## C11 定向入口身份

诊断构建可读取 D18InputProbe.focus：最多 16 行，每行一个 32 位十六进制 DXBC checksum。仅优先保留指定 shader 的创建身份（最多 256 次 / 4 MiB），不会授权渲染或数值回读；总散列 96 MiB / 归档 64 MiB 不变。F8 仍是 15 秒有界绑定观察。input-discovery 的 focused_entry 区分创建与实际绑定；未看到绑定不等于该功能不存在。统一采集器同时保留 focus 文件与 shader 归档。
