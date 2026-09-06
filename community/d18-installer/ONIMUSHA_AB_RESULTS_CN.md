# 鬼武者低比例 A/B 收尾结果（2026-09-06）

保持 Color/Output 全分辨率，完成相同滤波 1080p 域重测、SR 引导重建、
面积/gain-first 与 Mitchell/Catmull-Rom 输入核实验。没有采用 WorkingScale。

| 条件（50%） | 最终中频 after/before | 低频逐帧变化 after/before |
|---|---:|---:|
| Runtime 输入、sample-first guided | 0.9736 | 1.0088 |
| Runtime 输入、area + gain-first | 1.0003 | 1.1270 |
| Mitchell、sample-first | 0.9831 | 1.0356 |
| Catmull-Rom、sample-first | 0.9763 | 1.0234 |

8 帧匹配四流，全帧统计；跨组仍是不同时间的游戏帧。能量比不是纹理对应评分，
短序列时域变化也不直接等同可见闪烁。gain-first 有能量/稳定性取舍；
Catmull 输入中频约增 6%、模型可见输出约增 4%，但最终合成无明确收益。
没有同本轮视角配套 100% 色差评分，不宣称哪种核恢复了 100% 色彩。

结论：保留实验入口，默认关闭，不将其设为最终通用画质修复。
同滤波降到 1080p 后接近 1 的能量比不能证明损失全在重建，也不能证明网络已到上限。
本轮不追加 Lanczos 或组合扫描。后续以兼容性与可复现缺陷为主。

## 验证与源码产物

- Release/x64 核心及 DX12/Vulkan shader 构建成功，实机捕获来自该核心：
  SHA256 `8A062E2995D9C098049D6D441324B710420659EBBF5C496D2A29002B3000F7CF`。
- 源码回归：`python -m unittest discover -s tools/D18 -p "test_*.py"`，需要 NumPy/SciPy。
  分析工具仅用于显式捕获，默认不采集图像、不上传。
- 安装器 PowerShell 5.1 测试：`community/d18-installer/tests/Test-UiToggleKey.ps1`。
  隔离安装/升级/卸载也已验证首次 F10、双份配置、升级保留及默认 Insert。
- 热键 UI 保存重启及恢复默认尚待用户完整实机确认；GPU 开销没有按组完成可靠对照。
- 本次源码推送不移动旧 RC3 tag，也不自动替换 FlickerFix 安装包。
