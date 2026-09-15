# NR实例资源与GPU时间探针

显式宿主诊断，默认不执行；不修改游戏、时钟或功率设置，不从普通collector自动启动GPU。

1. `prepare-nr-resource-probe.py --baseline <完整C10或已复核兼容候选> --output <不存在的新目录>`：完整复制并新增宿主，精确模板替换断言，保留来源哈希。
2. `<新目录>/run-multipass-checks.py probe-nr-resources`：编译宿主；工具链路径沿用本机已核验设置，迁移机器需重新审查。
3. 先确认游戏退出，再调用 `run-nr-resource-probe.py --build <新目录> --forwarder <原forwarder> --runtime <原runtime> --driver <driver核心> --mode independent_batch|shared_batch --size 1080p|4k --ratio 0.5 --run <新名称>`。ratio支持0.5/1，当前两遍；独立实际创建2个实例，共享1个。
4. `analyze-nr-resource-probe.py <运行目录...> --output <新分析目录>`：核对哈希、二进制/driver/runtime、参数和帧集；按分辨率/模式保留每遍与合计中位数、均值、P95及本地占用。宽高可以不同以形成矩阵，其他核心条件必须一致。

每组48帧，预热16/计时32；每遍Evaluate区间使用实际GPU时间戳，同队列频率前后核对。每帧真实fence完成后读取小查询缓冲；只读回起始图和最后两遍作有限值检查，不将大尺寸像素写盘。日志+CSV/manifest通常只有数十KiB。90秒超时、10秒GPU等待、创建前6GiB预算余量；没有自动四遍4K扩展。

`d18-nr-resource-probe-v1`保存参数、创建实例数、运行身份、完整48帧CSV及日志哈希、GPU时间口径与内存采样；`d18-nr-resource-analysis-v1`保留分析身份和逐组结果。CurrentUsage为进程当前本地用量，采样最大值不等于真实峰值。时间不含SR/FG/最终合成，debug layer和逐帧CPU等待影响绝对性能；没有板卡功率测量，不自动折算FPS/瓦数。

本轮实例与结论见 [资源投入评估](../../reports/D18_NR_RESOURCE_REVIEW_20260914/REVIEW_CN.md)。后续若增加遍数、运动输入或不同API，应采用新的显式有界profile，不覆盖当前源码/CSV/清单/脚本快照。
