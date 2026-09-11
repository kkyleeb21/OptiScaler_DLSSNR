"""Stage matching English/Chinese installer corrections for source and release."""
from pathlib import Path
import difflib
root=Path(r'E:\DLSSNR')
targets=[root/'releases/DLSSNR_D18_0.1.4',root/'workspace/dlss5/worktrees/d18-012-re-integration/community/d18-installer']
cn_runtime='''仓库和 Release 不包含、下载或重新分发 NVIDIA `nvngx_dlssnr.dll`。用户需自行提供 310.8 Runtime。已验证的参考输入为：

```text
SHA-256 E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
大小    165840496 字节
```

安装器在本机副本上验证并应用完整 D18 补丁，记录输入和输出哈希，原文件保持不变。每个修改区域必须是已知的原始字节或完整 D18 替换字节；未知布局会停止安装。

**DX11 另有完整 Runtime 哈希要求。** 当前 addon 接受本安装器由上述参考输入生成的已验证版本，输出 SHA-256 为 `CCAC112995922D8BD2C5F2D0DCB7A6756B7806D3D868692ACB9AF64D4AEF7414`。社区兼容 Runtime 即使通过补丁区域检查，也不一定能用于 DX11；安装器会在替换现有安装前检查并说明。不要把“补丁字段兼容”理解为任意 GPU 或后端运行兼容。

DX12/Vulkan 安装采用共同补丁区域检查；不覆盖 D18 区域的社区修改可保留，但这不等于该 Runtime 已通过游戏实测。已经完整打过 D18 补丁的输入可重复安装。

`runtime_patch_source` 提供可读补丁规范、RVA 说明与反汇编；发布构建验证其生成结果与 `runtime_patch.json` 一致。本机生成的修改版 Runtime 不再保有原 NVIDIA Authenticode 有效签名。

'''
en_runtime='''This project does not contain, redistribute or download NVIDIA's `nvngx_dlssnr.dll`. Supply your own 310.8 Runtime. The verified reference input is:

```text
SHA-256 E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
size    165840496 bytes
```

The installer verifies and applies the complete D18 patch to a local copy, records both hashes and leaves the supplied file unchanged. Every patch region must contain known original bytes or the complete D18 replacement; an unknown layout stops installation.

**DX11 also requires a verified full-file Runtime hash.** The current addon accepts the output generated from the reference input above: SHA-256 `CCAC112995922D8BD2C5F2D0DCB7A6756B7806D3D868692ACB9AF64D4AEF7414`. A community Runtime may pass the patch-region checks and still be unsuitable for DX11; the installer checks this before replacing an existing installation. Patch compatibility does not establish GPU or backend compatibility.

DX12/Vulkan installation uses the common patch-region checks. Community edits outside the D18 regions can be retained, but this does not establish gameplay compatibility. Fully D18-patched inputs can be installed again without repeating the patch.

`runtime_patch_source` provides readable patch specifications, RVA notes and disassembly. Release builds verify that they reproduce `runtime_patch.json`. The locally modified output no longer has a valid NVIDIA Authenticode signature.

'''
cn_manual='''## 手动复制 payload（高级用户）

`payload` 不能原样拖入游戏目录。普通用户建议使用安装器，它会准备 Runtime、映射文件名、保留配置并记录卸载清单。

1. 退出游戏，备份所有将被覆盖的同名文件。
2. 将 `payload/OptiScaler.dll` 复制到游戏执行文件目录，改为所选代理名称；通常为 `dxgi.dll`，终末地使用已验证的 `d3d12.dll`。
3. **首次安装**将 `payload/OptiScaler.ini.d18` 复制为 `OptiScaler.ini`；升级保留原配置。复制 `payload/nvngx.dll_dlssnr.dll`、`payload/OptiScaler` 和 `payload/Licenses`。
4. 根据实际启动 API 补齐以下文件。表中的 Runtime 均由用户自行提供并完成完整 D18 补丁；不能用原始 Runtime 或仅做了显卡兼容修改的文件代替。

| 游戏启动 API | Runtime 在游戏目录中的名称 | 额外文件 |
| --- | --- | --- |
| DX12 | `nvngx_dlssnr.dll` | 无原生 addon |
| DX11 | `D24Runtime.dll` | `payload/D24Native.dll` |
| Vulkan | `D24Runtime.dll` | `payload/D24VulkanNR.enabled` |

DX11 Runtime 必须满足上文的完整输出哈希要求。原生 DX11/Vulkan 在 `[Upscalers]` 分别设置 `Dx11Upscaler=dlss` 或 `VulkanUpscaler=dlss`，在 `[DLSS]` 设置 `Enabled=true`。`nvngx.dll_dlssnr.dll` 是转发器，与 NVIDIA Runtime 不可混用。保留游戏自带的 SR/RR/FG 和 Streamline 文件，不用其他游戏的文件覆盖它们。

'''
en_manual='''## Manual payload installation (advanced)

`payload` is not a drop-in folder. Use the installer for Runtime preparation, filename mapping, configuration preservation and an exact uninstall manifest.

1. Close the game and back up every file that would be overwritten.
2. Copy `payload/OptiScaler.dll` into the game executable directory and rename it to your selected proxy, usually `dxgi.dll`; Endfield uses the verified `d3d12.dll` name.
3. **On a fresh install**, copy `payload/OptiScaler.ini.d18` as `OptiScaler.ini`; preserve the existing configuration on upgrades. Copy `payload/nvngx.dll_dlssnr.dll`, `payload/OptiScaler` and `payload/Licenses`.
4. Add the files for the API actually selected when launching the game. Supply your own Runtime with the complete D18 patch; an original Runtime or a GPU-compatibility edit alone is insufficient.

| Game API | Runtime filename in the game directory | Additional file |
| --- | --- | --- |
| DX12 | `nvngx_dlssnr.dll` | No native addon |
| DX11 | `D24Runtime.dll` | `payload/D24Native.dll` |
| Vulkan | `D24Runtime.dll` | `payload/D24VulkanNR.enabled` |

DX11 requires the full output hash stated above. For native DX11/Vulkan set `Dx11Upscaler=dlss` or `VulkanUpscaler=dlss` respectively under `[Upscalers]`, and `Enabled=true` under `[DLSS]`. `nvngx.dll_dlssnr.dll` is the forwarder, not NVIDIA's Runtime. Preserve the game's own SR/RR/FG and Streamline files; do not replace them with files from another game.

'''
for index,folder in enumerate(targets):
 for lang in ('CN','EN'):
  name='README_CN.md' if lang=='CN' else 'README.md';path=folder/name;old=path.read_text(encoding='utf-8-sig');t=old
  heading='## 不包含 NVIDIA Runtime\n\n' if lang=='CN' else '## NVIDIA Runtime is not included\n\n'
  a=t.index(heading)+len(heading);b=t.index('## 安装' if lang=='CN' else '## Install',a)
  t=t[:a]+(cn_runtime if lang=='CN' else en_runtime)+t[b:]
  a=t.index('## 手动复制 payload' if lang=='CN' else '## Manual payload installation');b=t.index('## 安全边界' if lang=='CN' else '## Safety',a)
  t=t[:a]+(cn_manual if lang=='CN' else en_manual)+t[b:]
  if lang=='CN':
   t=t.replace('4. 选择游戏可执行文件所在目录；安装器自动选择普通或 RE 模式，已有安装保留代理名。','4. 选择游戏执行文件目录、代理名称和实际启动 API（DX12 / DX11 / Vulkan）；已有安装保留所选代理名，API 可按本次启动方式调整。')
   t=t.replace('安装器不会仅因完整文件哈希未知就拒绝安装，也不会将“可打补丁”宣称为运行兼容。','以下分类描述补丁区域检查；DX11 还必须通过上文完整输出哈希检查，才会继续安装。')
   t=t.replace('[完整选项与研究发现](../../README_CN.md)','[功能说明](COMMON_FEATURES.md)')
   t+='\n## 中英文界面\n\nD18 面板顶部的 **Language / 语言** 可切换 **English / 简体中文**，立即生效，无需重启游戏。点击 **Save Settings / 保存设置** 后为当前游戏保留；配置项为 `[Menu] D18Language=0`（英文）或 `1`（简体中文）。两种语言使用同一套控件与启用条件。\n\n中文字体从 Windows 系统字体加载，不随包分发；找不到可用字体时保留英文并显示原因。技术名、文件名和原始诊断日志保留原文，便于排查。NR → 人物与风格中的细节闪烁修正开关可热切换；仁王 2 默认开启，其他游戏默认关闭。\n'
  else:
   t=t.replace('4. Enter the directory containing the game executable and select an appropriate proxy name. `dxgi.dll` is the default.','4. Select the game executable directory, proxy name and the API used to launch the game (DX12 / DX11 / Vulkan). Upgrades retain the proxy choice; select the API matching your next launch.')
   t=t.replace('[All D18 controls and findings](../../README.md)','[Feature guide](COMMON_FEATURES.md)')
   t+='\n## English and Simplified Chinese UI\n\nUse **Language / 语言** at the top of D18 to switch between **English / 简体中文** immediately. **Save Settings** retains the choice for this game: `[Menu] D18Language=0` for English or `1` for Simplified Chinese. Both languages share the same controls and availability rules.\n\nChinese fonts are loaded from Windows, not redistributed. If a suitable font is unavailable, the panel stays in English and explains why. Technical names, filenames and raw diagnostic logs remain unchanged. The NR detail-flicker correction under Characters and style switches live; it defaults on for Nioh 2 and off for other games.\n'
  staged=root/'builds/D18_014'/f'docs-{index}-{name}';staged.write_text(t,encoding='utf-8')
  diff=list(difflib.unified_diff(old.splitlines(),t.splitlines(),n=3));hunks=['@@' if x.startswith('@@') else x for x in diff[2:]]
  (root/'builds/D18_014'/f'docs-{index}-{lang}.patch').write_text('*** Begin Patch\n*** Update File: '+str(path)+'\n'+'\n'.join(hunks)+'\n*** End Patch\n',encoding='utf-8')
  print(path)
