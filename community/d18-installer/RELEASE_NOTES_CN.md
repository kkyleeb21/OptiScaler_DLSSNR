## 功能

鬼武者专用 D18 0.1.1 预览补丁：保留游戏原生 DLSS SR，在其输出后运行 NR；支持内部 network ratio 调节，Color / Output 保持全分辨率。已完成本机 NR 开关、100% / 50% ratio、原生 FG 开关及多次切换测试，并恢复 D18 UI 与游戏未暂停时的菜单鼠标点击、拖动。

## 安装

1. 完全退出游戏及 CrashReport。先安装 **Nexus Mods 的鬼武者专用 REFramework**，确认 `dinput8.dll` 在 `OnimushaWotS.exe` 旁，先验证 REF 单独运行正常。
2. 自行准备原版或适配自己显卡的 DLSS5 Runtime：**`nvngx_dlssnr.dll`**，放入同一游戏目录。本包不附带、不下载 Runtime 或 REFramework。
3. 解压本包，双击 **`Install-D18.bat`** 并选择游戏目录。安装器自动采用 **`d3d12.dll`**、备份旧文件，支持升级及 `Uninstall-D18.bat` 卸载恢复。
4. 游戏中选择原生 DLSS SR，按 **Insert** 打开 D18。新装默认 NR 开启、ratio **100%**；升级保留个人 NR 设置。REF 菜单键建议改为 **PgDn**，避免冲突。

`nvngx.dll_dlssnr.dll` 是本包转发器，不是用户需要准备的 Runtime。未知 Runtime 只要补丁字段兼容即可继续安装，但会提示“未经验证，可能可用”；字段冲突则停止、不改原文件，全部补丁已存在则不重复修改。

## 局限性

- **鬼武者 only / Onimusha ONLY**，不是通用 RE Engine 包；不支持据此推断生化危机等其他游戏兼容，也不宣称 DX11 / Vulkan 或路径追踪 / RR 已通过验证。
- **50% ratio 可能明显损失细节，并改变色彩和明暗**，建议先用 100%。
- FG 面板的 **UNOBSERVED** 仅表示未观测游戏原生 FG 状态，不代表 FG 关闭或失效。
- UI 滚轮、文字输入尚未完善；GPU 信息可能显示不准确。
- 兼容 Runtime 的显卡支持范围以其提供者为准，未逐一实测所有显卡代际和驱动。REF 文件存在检查也不等于验证其版本正确。
- 本候选已构建并通过安装器与独立依赖测试；重新构建后的完整游戏冒烟测试仍待完成，之前的本机测试不等同于本包最终验收。
