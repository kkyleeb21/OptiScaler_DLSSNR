# D24 审计工具

## 当前运行诊断入口（2026-09-08）

统一使用 `python tools/D24/summarize_runtime.py <日志路径> --output <报告.json>`，自动识别 DX11 的 D24Native.log、Vulkan 的 D24VulkanDiagnostics.log 和 DX12 的 D18Diagnostics.ring。输出含 summary_schema、adapter、source 和 gameplay_verdict；API 专属证据保留在各自字段内。不要把模式、CPU 记录、GPU 完成或画质确认相互代替。

正式开关是菜单 Diagnostics，常规运行保持 Off；纯转换和像素采样默认关闭。D24VulkanNR.enabled 是后端激活文件，不是日志开关。历史 diagnostics-only 部署脚本已停用；已有日志和 marker 不证明当前会话仍在采样。

Vulkan 摘要区分 mode=1 纯转换和 mode=2 且 NGX result=1 的模型记录。sr_submit 的 Vulkan 成功值是 0，按 epoch 关联 gpu_completed；旧 nr_frame 未携带 epoch，不能用 SR 完成冒充 NR 完成。未知布局仍为未知，未观测到不等于执行失败。

色彩四阶段分析继续使用 analyse-colour-capture.py；曝光元数据使用 summarize-colour-contract.py；FG 阶段使用 summarize-fg-pacing.ps1。这些专用分析保留原始证据，不在本次虚构统一时间轴。更完整的跨 API 时间轴整合仍是后续独立待办。

回归：`python tools/D24/test-runtime-summary.py`。安装清单与最新验证结果见 reports/D24_BUILD_INSTALL_ROLLBACK_CN.md 和 reports/D24_VALIDATION_CURRENT_CN.md。

用于 D24 的可重复证据采集与反例检查。静态审计工具只把 DLL 当数据读取；新加入的 DX11 独立宿主会加载固定 hash 的原版并调用内部实现，NR 实验模式还会改动子进程内的代码与后端虚表。所有工具均不写 runtime 文件或游戏目录。现有 D18 产品运行路径不变。

## 已提供

| 工具 | 用途 | 边界 |
|---|---|---|
| ngx_audit.py | x64 PE 导出、别名 RVA、序号导出、转发、普通/延迟导入、字符串、局部 RIP xref、有限调用关系；记录文件版本/哈希/驱动 | 不执行目标 DLL；间接调用和预算边界明确标未知 |
| query_callpath.py | 在采集图中查询入口到候选构造/内核点的已观察直接路径 | 找不到路径返回 UNKNOWN；共享函数不证明共享对象 |
| validate_probe.py | 校验未来原生 Feature 18 宿主的 60 帧结果、完成 fence、实际读回、格式/行距、NaN/Inf、旧输出、输入变化 | 不启动宿主；证据齐全也要求人工审查；不自动认证状态布局 |
| test_d24.py | 合成 PE 与像素/动态记录反例测试 | 合成结果仅测试工具，不是 GPU 支持证据 |

## 使用

在 PowerShell 从 `E:\DLSSNR` 执行。每次输出必须使用 `evidence/D24` 下一个不存在的新目录，避免覆盖已有证据。

```powershell
python tools/D24/ngx_audit.py E:/DLSSNR/backups/Onimusha-before-013a-20260907_003614/nvngx_dlssnr.dll --output E:/DLSSNR/evidence/D24/static-new-run --capstone E:/DLSSNR/workspace/dlss5/tools/third_party/capstone --dumpbin C:/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe
python -m unittest discover -s tools/D24 -p test_d24.py -v
```

进一步跟踪时用 `--root 0x候选RVA --depth 3`，`--root` 可重复。先核对 manifest 的 DLL SHA256；历史补丁脚本的字典键可能是文件偏移，不能直接当 RVA。工具读取真实 ImageBase，并拒绝把无文件 backing 的虚拟尾部当代码。

2026-09-07 基线修正：上述游戏备份是带 `.d16` 节的已修改 runtime（SHA256 `ccac1129...`），不是原版。原版对照位于 `workspace/dlss5/extracted/DLSS310.8.0-Streamline2.13/nvngx_dlssnr.dll`，SHA256 为 `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`。原始能力审计用原版，并传 `--expected-sha256`；manifest 现在记录 PE sections，以免版本相同但补丁状态不同被混淆。

`check_vk_init_abi.py` 从当前产品源码提取 Vulkan 初始化函数，生成顺序修正候选 diff 和宿主 ABI 回归。它不加载 NGX、不修改产品源码。原始签名必须在正确类型的模拟 callee 处编译失败，候选必须编译并传递正确参数。具体结论见 `reports/D24_S1_ABI_FINDINGS_20260907_CN.md`。

```powershell
python tools/D24/query_callpath.py evidence/D24/static-new-run/S1_graph.json 0x15750 0x候选目标
python tools/D24/validate_probe.py evidence/D24/未来原生宿主运行目录
```

调用图最多 150 个节点、每片段最多 4096 字节/512 条指令，默认向下两层。pdata 是函数片段边界；无 pdata 时采用 128 字节上限。间接调用、解码失败、预算截断均保留，不推断“不可达”。字符串记录包含 ASCII 与 UTF-16LE，但 xref 仅覆盖已访问指令的 RIP 相对直接引用，不是全模块数据流。

## 对原计划的修正

| 计划中的假设 | 工具采用的判定 |
|---|---|
| 最小集合要求 GetCapabilityParameters / GetParameters | 仍记录缺失，但 snippet 和 NGX loader 的职责需区分；缺失不判桩 |
| Vulkan RequiredExtensions / Helper | 同时查真实导出 GetFeatureDeviceExtensionRequirements / GetFeatureInstanceExtensionRequirements；静态字符串只列候选，不冒充返回值 |
| 桩 / 转发 / 独立实现三选一 | 增加 UNKNOWN；只对精确的极短常量返回体做局部描述，不扩大为整个 API 结论 |
| Create/Evaluate 失败即该 runtime 不可用 | 先排查 ABI、caller、参数 provider、设备 features/扩展和格式；记录此配置失败，不从单次失败宣布所有原生路径不可用 |
| 从返回句柄 dump 内部状态 | NGX 句柄可能不透明；先证明内部对象身份、大小、生命周期，再读有界字段；相同 0x20 字节也不证明语义一致 |
| 60 帧 success + 非零输出 | 加 GPU 完成、sentinel 前后变化、至少两个受控输入案例、RGB/alpha 分离、finite 检查和行距处理；仍需读图与 D3D12 对照 |
| ngxGym 直接作为原生 Feature 18 宿主 | 官方项目目前描述的是 D3D11/Vulkan DLSS + ReShade/bridge 测试。原生 Feature 18、D3D12 对照、内部状态观测都需另做宿主适配 |

ngxGym 资料：https://github.com/NIGos/ngxGym 。本轮仅核对项目说明，没有下载执行第三方脚本。不能把其桥接成功日志导入后标成原生后端成功。

## 动态宿主输出协议（schema 1）

每个 API / 格式独立目录，包含 `probe.json`、`contract.json`、`frames.jsonl` 及原始 readback 文件。宿主需读回输出，不能自行填一个“已完成”布尔值代替 fence 值。

`probe.json` 必填：

```json
{
  "schema": 1, "api": "D3D11", "feature_id": 18, "route": "native",
  "runtime_sha256": "实际64位十六进制哈希", "host_sha256": "实际64位十六进制哈希",
  "driver": "实际版本", "gpu": "实际型号", "adapter_id": "LUID等",
  "caller_module": "实际调用模块", "parameter_provider": "实际参数对象来源",
  "runtime_modified": false, "bypass_used": false,
  "init_result": null, "create_result": null,
  "device_removed": null, "validation_errors": null,
  "colour_space": "linear", "width": 64, "height": 64,
  "format": "RGBA8_UNORM", "row_pitch": 256, "contract_file": "contract.json"
}
```

以上 null 是待采集项，不是成功默认值。格式支持 `RGBA8_UNORM` 和 `RGBA16_FLOAT`。尺寸/行距对应输出 readback；输入资源格式另写 contract。HDR 不仅是 16 位纹理，还要有正确 colour space、flags、exposure 和输入数值。

`contract.json` 至少记录 render_size、output_size、depth_format、mv_format、mv_scale、jitter、reset、exposure、flags；建议附完整 NGX 参数类型/值和创建/每帧差异。

`frames.jsonl` 每行含：

```json
{"frame":0,"evaluate_result":null,"fence_id":"queue0/fence0","fence_target":1,"fence_completed":null,"input_case":"pattern-a","before_file":"before-000.bin","output_file":"output-000.bin"}
```

至少连续 60 帧，每帧 output/before 都按声明的 row_pitch×height 读取，hash 排除 padding。before 是本帧 Evaluate 前有记录的 sentinel，不能用不明来源旧图。至少两个受控输入 case；像素非零比例只观察 RGB，alpha 仍参与 finite 检查。

校验器输出是“证据完整待审”或“失败/缺失”，不会输出“原生兼容通过”。字段相同、输出变化、函数相通都不足以复用 D18 内部状态补丁。

## 当前未覆盖

- 完整 S_ctor/K_launch 数据流、虚调用目标、Blackwell2/ScalingRatio 门控归属需要人工静态审计，工具提供逐次补 root 的入口。
- DX11 独立宿主已有原生 NR 60 帧读回证明，使用下文专用验证器；尚未接入上述通用宿主记录协议，也未实现游戏导出层接线。
- 本工具不进行状态内存 dump；对象来源证明完成后再加有界采样。

## DX11 原生研究宿主（2026-09-07）

仅针对原版 SHA256 `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`，依赖现有 MSVC、Windows SDK；`--kernel` 另需 CUDA nvcc。详细边界与证据见 `reports/D24_DX11_NATIVE_POC_20260907_CN.md`。

```powershell
python tools/D24/run_dx11_backend_probe.py
python tools/D24/run_dx11_backend_probe.py --kernel
python tools/D24/run_dx11_backend_probe.py --nr-create-dry
python tools/D24/run_dx11_backend_probe.py --nr-create-evaluate
python tools/D24/validate_dx11_nr.py E:/DLSSNR/evidence/D24/dx11-live-20260907-150533
```

- 无参数只验证底层 Init/Shutdown；`--kernel` 对自编译 affine 内核做 60 次精确读回，不修改后端。
- `--nr-create` 保留原生拒绝门控；`--nr-create-experiment` 只跳过门控，是保留的失败反例，会遇到未初始化 dispatch 注册状态，不能当候选修复。
- `--nr-create-adapter` 创建真实 NR；`--nr-create-evaluate` 启用进程内研究适配并验证 60 帧；不代表游戏兼容。
- `--nr-create-dry` 捕获真实参数但不提交 NR 内核；`--nr-create-prefixN` 仅提交前 N 次，N 为 1–156。这些模式明确写入 `nr_output_not_tested`，不会输出完整成功状态。
- 每次宿主运行创建新证据目录，记录源码快照、编译日志、EXE/hash、原版前后 hash、事件、runtime 日志。原型采用固定尺寸、单设备、单 feature、immediate context；资源注册保守覆盖当前实验的 7 个资源，未优化为逐层精确读写集。
- 原型尚不适合直接用于游戏：多 feature、resize/recreate、纹理替换和长期内存回收未验证；所有内部 RVA 需要逐版本审核。

## 运行期诊断汇总入口

`python tools/D24/summarize_runtime.py <D24Native.log> --output <summary.json>`

目前接入 DX11 JSON 元数据；缺少动态输入时明确报告 dynamic_inputs_not_observed。汇总采样档位、参数范围、资源变体和缺失参数，不把执行成功判为画质通过。Vulkan文本事件与D18 ring适配仍在收尾整合待办中。

DX11采样默认关闭；在插件旁放置 D24Dx11Diagnostics.enabled 后重启启用，移走标记并重启关闭。采样最多1800组，非逐帧抓图。

DX11中心裁剪适配：同一标记启用时，每600次NR调用连续采4帧，最多128帧；记录SR、模型输入、模型输出和合成的中央128x128 RGBA16F（其他格式记unsupported）。最多64MiB，读回会影响采样帧时序。归档须复制日志旁的D24Capture_*.rgba16f；共享汇总入口自动分析日志引用的裁剪，相邻差异不等于闪烁判定。元数据更新为每60次连续4次，避免资源双缓冲采样混叠。

可配置裁剪位置：D24Dx11CaptureROI.txt填两个0–1坐标（例如0.5 0.463），新版采样帧读取，四阶段使用相同位置。缺失/非法回退中心，边缘钳制；无需为位置变化重新编译。摘要不跨位置计算相邻差。

更新：ROI第三项为边长64–512，默认512，例如0.5 0.537 512。新版最多32帧四阶段，原始数据上限256MiB/运行，取代旧128帧预算。区域坐标属于NGX纹理，不保证与屏幕同向，须用预览确认。运行中的旧DLL需重启更新后才支持第三项。

四阶段预览：python tools/D24/render_runtime_crops.py <evidence-folder> --flip-y。使用summary.json引用的尺寸和文件，显示最后四个完整帧；flip-y仅改变显示方向，不改变原始数据和渲染。输入/模型直接显示模型域，SR/合成从线性转换显示并裁到0–1，预览不用于HDR亮度定量比较。

终末地DX11皮肤保护候选：部署旁D24Dx11SkinProtection.enabled在feature创建时启用AutoMask=1、SkinStructure=0，其他增强参数不变；移除标记并重启恢复。日志skin_protection_automask1_structure0说明是否启用。当前为待实机验证的游戏配置，不是全游戏默认策略。

当前终末地实验profile已按用户要求改为Vulkan参数对齐：旧文件名D24Dx11SkinProtection.enabled现在选择AutoMask=1、SkinStructure=-1、UICorrection=1、MaxRatio=2、Transfer=1，恢复皮肤增强。以文件内容和事件vulkan_parity_mask1_skin_minus1_ui1辨认本轮，不沿用旧skin0说明。仅该游戏部署启用，待实机验证。
