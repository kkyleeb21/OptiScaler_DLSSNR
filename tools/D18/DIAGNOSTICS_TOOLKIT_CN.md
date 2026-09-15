# D18 diagnostic support tools / 诊断支持工具

Public downloads contain the release build only. The diagnostic build is retained internally for explicitly arranged research. Summary is off by default; this collector reads existing logs and does not change settings or start a game. Shared history remains selectable in release, experimental and known to flicker.

公开下载仅提供 release；诊断构建留作内部排查。默认不开 Summary。采集器仅复制已有日志，不启动游戏，不修改配置、marker 或原始 NR。共享历史仍在发布版保留，实验性且可能闪烁。

```powershell
.\collect-diagnostics.ps1 -GameDirectory 'D:\Games\Example' -OutputDirectory "$env:USERPROFILE\Desktop\D18-report-new"
python .\render-toolkit.py
python .\render-toolkit.py native-nr -- --help
```

Requires PowerShell 5.1+ and Python 3 on PATH. Without OutputDirectory, creates a unique folder under %LOCALAPPDATA%\DLSSNR\Diagnostics (temporary directory fallback). Explicit paths on other drives are accepted. Use a new directory outside the game/capture source; existing output is never overwritten. collect-render-diagnostics.ps1 forwards to the same collector, including DX11/Vulkan adapters.

需要 PowerShell 5.1+ 和 PATH 中的 Python 3。省略输出目录时，使用 %LOCALAPPDATA%\DLSSNR\Diagnostics 下唯一子目录；也可指定其他盘路径。请使用游戏/采集源之外的新目录，避免覆盖既有证据。旧 collect-render-diagnostics.ps1 入口已归并到同一采集器。

Layer capture is copied only with explicit -LayerCaptureDirectory. On-disk snapshots are not atomic GPU captures; missing observations are not successful execution. Logs/configuration and bounded explicitly associated archives may contain identifying information; inspect the bundle before sharing. No upload is performed by these tools. Public tooling is selected by community/d18-installer/public-tools.json; internal deployment, storage cleanup, builds and GPU probes remain in engineering source only.

Layer Capture 仅在显式指定时归档。磁盘快照不是 GPU 原子采样，缺失记录不代表执行成功。分享前自行检查日志/配置中的信息；工具不上传数据。新诊断继续整合公共分析入口，研究工具仍保存在工程源码中。
