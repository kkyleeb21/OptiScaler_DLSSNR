# Shared DX11 runtime patch compatibility check

The installer helper D18RuntimeCheck.exe and the native addon D24CheckRuntime compile the same check.h and patch-sites.generated.h. Run generate-patch-sites.py from this source tree to regenerate the table from community/d18-installer/runtime_patch.json. No NVIDIA runtime is needed for generation.

Admission checks the registered runtime profile, consistent original/patched state, x64 PE layout, manifest patch ranges and the bytes written by the native DX11 adapter. Whole-file hashes and unrelated code/data-region fingerprints are not admission conditions. Actual in-memory vtable/instruction checks remain. Passing this check is not proof of arbitrary runtime, model, hardware or game compatibility.

Build both binaries together with tools/D24/Build-D18.ps1 -AddonOnly. Run test.py --addon <D24Native.dll> --checker <D18RuntimeCheck.exe> --runtime <user-original-NR> --output <new-evidence-directory>. It compares both validators on original/patched and deliberately invalid synthetic files, without loading or executing the NR runtime. Keep user originals unchanged.

0.1.8 revision 1 fixes the previously distributed addon using a stale host-region checker. Old tables/generator are retained only in the original 0.1.8 source tag, not active here.

## D18 0.5.1 runtime support / 运行库支持

- DX12 / Vulkan: NR 310.8 and 310.9.16, with colour grading on both versions. NVIDIA runtime files are supplied by the user and patched locally, never bundled.
- Native DX11: 310.8 only. 310.9.16 is refused with an explicit message and NR is disabled; its internal DX11 adapter has not been ported.
- The two registered official inputs classify as VERIFIED. A known community 310.9.16 compatibility layout classifies as EXPERIMENTAL_UNSIGNED: experimental, unsigned, unknown origin. It is not recommended or distributed, and has not been tested on RTX 20/30/40.
- DX12 / Vulkan：支持 NR 310.8 与 310.9.16，两者均可调色。运行库由用户自备，在本机修补，不随 D18 分发。
- 原生 DX11：暂只支持 310.8，遇到 310.9.16 会提示并停用 NR，尚未移植其内部 DX11 适配器。
- 登记的两个官方输入归为 VERIFIED；一个已知社区 310.9.16 兼容布局归为 EXPERIMENTAL_UNSIGNED（实验、未签名、来源不明），不推荐、不分发，未在 RTX 20/30/40 上测试。
