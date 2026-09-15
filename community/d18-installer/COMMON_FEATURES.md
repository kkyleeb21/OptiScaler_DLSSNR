# D18 0.1.8 功能 / Features

总览 / Overview: SR、FG、NR 状态，实时诊断、界面与快捷键、原图对比。Status, diagnostics, UI/hotkeys and comparison.

| 栏目 / Tab | 用途 / Purpose |
| --- | --- |
| 神经渲染 / Neural rendering | 最终细节/颜色合成、细节重建、颜色传递；模型设置中的模式、遍数、历史、逐遍 ratio/强度/预设/风格及共享输入/曝光/时序参数。Final detail/colour composition, reconstruction, transfer, model mode/count/history/per-pass controls and shared input/exposure/temporal settings. |
| 超分辨率 / SR | 现有 SR 或受支持的原生接入开关、画质档位及模型设置。Controls for existing SR or supported native input integration, quality and model settings. |
| 帧生成 / FG | 输入/输出路径、开关、倍率、实际状态和依赖说明；首次路径准备可能需要重启。Routes, enable, multiplier, observed state and dependencies; initial preparation may require a restart. |
| 锐化 / Sharpening | 按需覆盖与锐化强度。Optional sharpening overrides and strength. |
| 调试 / Debug | 按构建/API 可用的诊断和捕获，默认关闭且有界。Profile/API-specific diagnostics and capture, off by default and bounded. |

灰色选项显示可用条件；请求遍数与实际执行分开报告。语言切换立即生效，保存设置后按游戏保留。Disabled controls explain availability; requested and executed passes are distinct. Language changes apply immediately and persist with Save Settings.

多遍/高分辨率不保证更好画质；共享历史实验性且可能闪烁。见 [中文更新说明](RELEASE_NOTES_CN.md) / [English notes](RELEASE_NOTES_EN.md)。
