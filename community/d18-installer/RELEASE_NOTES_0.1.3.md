# D18 0.1.3

## 中文
- 增加 RE 引擎游戏支持，需要配合 REFramework 使用。已实测安魂曲、Pragmata、怪物猎人：荒野；其他游戏仍需分别验证，不代表所有 RE 游戏都已兼容。
- 统一安装器自动识别已知 RE 游戏并选择适配的 REFramework nightly；若没有适配记录，用户可选择最新 nightly 或自行安装。唯一自动下载来源：https://github.com/praydog/REFramework-nightly 。已有 REFramework 保留，未知 dinput8.dll 不会被自动覆盖。
- REFramework 菜单快捷键设为 PgDn，D18 默认菜单键仍为 Insert。
- 普通与 REFramework 环境共用游戏根目录 OptiScaler.ini；安装器管理所需 DLL 镜像，用户不必维护两份配置。
- 修复原生 SR/RR 后 NR 的状态恢复默认值、RR 接入及输入子矩形；保留资源状态、队列与 GPU 完成检查。
- NVIDIA nvngx_dlssnr.dll 不包含在安装包中，仍需用户自行提供。本版本主 DLL 与随包 forwarder 必须配套更新。

## English
- Adds RE Engine support with REFramework required for the supported setup. Resident Evil Requiem, Pragmata and Monster Hunter Wilds have been tested; other titles require separate validation.
- One installer detects known RE games and selects a matched official nightly. If no match is available, choose the latest nightly or install manually. Downloads come only from https://github.com/praydog/REFramework-nightly . Existing REFramework is retained; unknown dinput8 loaders are not silently overwritten.
- REFramework menu key: PgDn. D18 default menu key: Insert.
- One game-root OptiScaler.ini for normal and REFramework installations, with installer-managed DLL mirrors.
- Fixes native SR/RR-to-NR state-restore defaults, RR integration and input subrect handling while retaining resource/queue/fence guards.
- NVIDIA nvngx_dlssnr.dll is not bundled. Update the D18 core and supplied forwarder together.
