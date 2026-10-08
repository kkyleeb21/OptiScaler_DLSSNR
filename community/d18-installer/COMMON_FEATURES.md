# D18 0.4.1 功能 / Features

总览 / Overview: SR、FG、NR 状态，实时诊断、界面与快捷键、原图对比。Status, diagnostics, UI/hotkeys and comparison.

| 栏目 / Tab | 用途 / Purpose |
| --- | --- |
| 神经渲染 / Neural rendering | 最终细节/颜色合成、细节重建、颜色传递；模型设置中的模式、遍数、历史、逐遍 ratio/强度/预设/风格及共享输入/曝光/时序参数。Final detail/colour composition, reconstruction, transfer, model mode/count/history/per-pass controls and shared input/exposure/temporal settings. |
| 超分辨率 / SR | 现有 SR 或受支持的原生接入开关、画质档位及模型设置。Controls for existing SR or supported native input integration, quality and model settings. |
| 帧生成 / FG | 输入/输出路径、开关、倍率、实际状态和依赖说明；首次路径准备可能需要重启。Routes, enable, multiplier, observed state and dependencies; initial preparation may require a restart. |
| 锐化 / Sharpening | D18 SH0（O/S、Full/Half、Mid/Fine）与 OptiScaler 锐化分区。Separate D18 SH0 and OptiScaler sharpening controls. |
| 调试 / Debug | 按构建/API 可用的诊断和捕获，默认关闭且有界。Profile/API-specific diagnostics and capture, off by default and bounded. |

灰色选项显示可用条件；请求遍数与实际执行分开报告。语言切换立即生效，保存设置后按游戏保留。Disabled controls explain availability; requested and executed passes are distinct. Language changes apply immediately and persist with Save Settings.

多遍/高分辨率不保证更好画质；共享历史实验性且可能闪烁。见 [中文更新说明](RELEASE_NOTES_CN.md) / [English notes](RELEASE_NOTES_EN.md)。

V8 / R0 comparison: opt-in, fixed supported inputs; see current release notes.

## Colour grade

The Colour grade section on Basics can disable the style's built-in grade or replace it with custom exposure, contrast, saturation and the additional controls under More. It can be combined with any model style, and three preset slots save both style and grade. Custom grading is off by default, preserving the style's original grade. Requires NR runtime 310.8 on DX12 / Vulkan.

## Brighten limit / darken limit

Advanced → Composition provides separate controls for how much NR brightens and darkens the image. Separate darkening is off by default (MaxDarken=auto follows the brighten limit), preserving previous behavior. Native DX11 does not yet support a separate darken limit. This limit can act twice during composition, so the actual darkest value may fall below the displayed percentage.

## Pause NR while the game turns frame generation off

This switch on Advanced pauses NR while the game turns its built-in DLSS frame generation off and resumes automatically when it returns. It is off by default and only applies to DX12 games using their built-in DLSS frame generation. In menus, dialogue or cutscenes, this avoids running NR at the full real frame rate after the game removes its frame-rate cap.

## 调色

在基础页的“调色”栏位，可关闭风格自带的调色，或自定义曝光、对比、饱和度及“更多”里的其他调整，与任意模型风格组合。三个预设栏位可保存“风格 + 调色”。默认关闭，沿用风格原有调色；需要 NR 运行库 310.8，仅支持 DX12 / Vulkan。

## 提亮上限 / 压暗下限

高级 → 合成中的两项限制可分别控制 NR 的提亮和压暗程度。单独的压暗下限默认关闭（MaxDarken=auto，跟随提亮上限），保持旧版行为；原生 DX11 暂不支持独立压暗下限。限制在合成中可作用两次，实际最暗值可能低于所示百分比。

## 游戏关闭帧生成时暂停 NR

高级页的此开关会在游戏关闭自带 DLSS 帧生成时暂停 NR，并在帧生成恢复时自动继续。默认关闭，只对使用游戏自带 DLSS 帧生成的 DX12 游戏有效；菜单、对话或过场期间可借此避免 NR 按解除帧率上限后的真实帧率全速运行。
