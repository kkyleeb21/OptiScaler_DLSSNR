# D18 0.1.3a

## 中文
- 修复鬼武者在低帧率（例如 30 FPS）下可能出现的 NR 间歇闪烁：补回已验证的 F2 队列历史策略，避免因 1500 ms 观察过期跳过有效帧。
- 仅保留已知、无歧义、属于当前固定队列的历史；首次观察、设备匹配、队列所有权和真实 Execute + Signal / fence 检查保持严格。
- 修复 RE 状态检查误用开发机 Windows SDK 路径的问题。随包提供私有 DXC 依赖，并在 RE 安装镜像中同步，无需用户安装开发工具。
- 增加 30/120 FPS 队列复用、过期边界、未知/歧义/时钟回退及真实 WARP 提交回归，打包时校验私有依赖。
- 保留 0.1.3 的统一安装器、根目录单份配置、官方 REFramework-nightly 自动选择与 PgDn 菜单键。RE 引擎支持需要配合 REFramework；无适配记录时可选最新 nightly 或自行安装。

## English
- Restores the tested F2 queue-history policy to prevent intermittent NR skips/flicker at low frame rates such as 30 FPS in Onimusha.
- Only known, unambiguous history on the current pinned owner queue bypasses the 1500 ms age limit. First observation, device/owner checks and actual Execute + Signal / fence completion remain required.
- Fixes RE state reflection loading DXC from a developer SDK path. Ships and validates the private DXC dependency in normal and mirrored installations; no Windows SDK installation is required.
- Adds queue reuse/boundary and real D3D12 WARP submission regression coverage.
- Retains the 0.1.3 unified installer, single root configuration and official REFramework-nightly selection. RE support requires REFramework; choose latest nightly or manual installation when no matched build exists. REF menu remains PgDn.

Official REFramework source: https://github.com/praydog/REFramework-nightly
