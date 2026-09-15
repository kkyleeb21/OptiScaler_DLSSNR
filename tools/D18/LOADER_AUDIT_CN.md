# D18 加载入口静态审计

公共入口：`python tools/D18/audit-loader.py --game <执行文件目录> --core <待测试核心DLL> --system32 C:\Windows\System32 --output <新的JSON文件>`。

工具复用 `tools/D24/ngx_audit.py` 的 AMD64 PE 解析器，只读文件，不调用 LoadLibrary，不启动游戏。扫描执行文件目录内最多 256 个 EXE/DLL，以及指定 System32 中的 DXGI、D3D11、D3D10 和备用代理模块；不是完整依赖闭包。32 位或无法解析的文件明确记录 parse_error。

输出统一为 `d18-loader-static-v1`：核心及模块哈希、普通/延迟导入、导出、DLL 名称的 ASCII/UTF-16LE 观察、候选代理的已观察调用覆盖、ordinal 身份差异和现有同名文件。所有候选的运行时加载状态为 `not_observed`，游戏验证为 `not_tested`；没有导入或字符串不证明不会动态加载，具备导出不证明转发 ABI 或 Hook 正确。

这项按需离线诊断不会增加运行时日志。后续加载崩溃沿同一工具保存新核心/新游戏版本清单，再结合 `collect-diagnostics.ps1` 和 `summarize-diagnostics.py`。现有收集器要求 D18Diagnostics.ring，若崩溃早于环形诊断初始化，应明确记录 ring 未生成，并单独保存已有 OptiScaler.log、错误文字或 dump，不能把缺日志当作初始化成功。

首个案例：`reports/D18_WILDLANDS_LOADER_STATIC_20260912_CN.md`，输出 `evidence/D18/Wildlands_LoaderAudit_20260912/loader-static.json`。已对实际核心、20 个 AMD64 游戏根目录模块及选定系统模块运行成功，UbisoftConnectInstaller.exe 因架构范围而明确保留未解析状态。后续如增加运行时加载事件，应并入公共初始化诊断及摘要，不建立每游戏常驻探针。

## 按需设备约定运行探针

公共源文件：`tools/D18/device-contract-probe.cpp`，用 Windows SDK / MSVC 构建。
调用：`probe.exe <代理 DLL 的绝对路径或 system> <11_0|11_1|default|swap11_0>`。
每次调用使用独立进程；swap11_0 创建隐藏的 64x64 窗口测试交换链，不启动游戏。代理模式会执行指定 DLL，因此只用于可信的本地 D18 构建。
输出结构化 `device_contract` 事件，包括请求等级、返回等级、设备查询等级、Device5 与共享 Fence 能力。退出码 7 表示设备等级约定不一致。调用成功不证明 SR/FG/NR 输入完整或像素正确。
首轮系统/旧核心/新核心对照：`builds/D18_Compatibility_20260912/contract-test/results.json`。本探针按需运行，无游戏常驻日志；后续沿同一入口扩展设备、共享资源与同步能力检查。

## 输入着色器静态定位
`python tools/D18/audit-shader-inputs.py --output <JSON> <files...>`：每文件最大 512 MiB、最多 2048 DXBC、400 字符串候选；标明截断及 not_observed，不解包资源。
`python tools/D18/inspect-exported-shaders.py <PE DLL> --output <目录> --pattern <导出名正则>`：最多 32 个候选，复用 ngx_audit.PE，离线解析导出数据指针，用 System32 d3dcompiler_47 反汇编；不会 LoadLibrary 游戏 DLL。只支持导出指向 DXBC 的特定数据布局，不是通用 shader-container 解包器。
首次应用和解释边界见 reports/D18_WILDLANDS_INPUT_RESEARCH_20260912_CN.md。文件偏移/RVA、着色器名称和资源槽位不是实时纹理地址；资源用途和帧时序必须另行观测。

## 有界运行时输入观测
实验实现和覆盖边界：reports/D18_WILDLANDS_INPUT_PROBE_20260912_CN.md。公共摘要入口 `python tools/D18/summarize-input-probe.py <D18InputProbe.jsonl> --output <JSON>`；限制 4 MiB，保留安装状态、窗口开始/结束、角色命中、坏行数量，明确标记输入内容/深度/运动语义未验证。默认关闭；只有标记文件启用并由前台 F8 触发有限窗口。
