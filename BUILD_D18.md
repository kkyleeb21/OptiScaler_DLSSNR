# Building D18 0.1.4

Use Visual Studio 2022 C++ build tools, the Windows SDK and an x64 Developer PowerShell. Clone with submodules (`git submodule update --init --recursive`). The core uses the SDK headers/libraries under `external` and the repository's `OptiScaler/library`; ensure these dependencies are present before building. NVIDIA NR runtime DLLs are not build inputs and are not distributed here.

Run `tools/D24/Build-D18.ps1`. An existing dependency checkout can be supplied with `-DependencyRoot <checkout-root>`; it must contain `external`. Use `-OutputDirectory <path>` to select build output. The script builds the core, NGX forwarder and DX11 native addon. `-AddonOnly` builds just the native addon. The native addon sources and generated shader source are in `tools/D24/bg3-native`.

The 0.1.4 installer revision retains the previously tested rendering binaries. Its source corresponds to this tree's `OptiScaler` and native-addon files; compiler, SDK and embedded build metadata can change binary hashes on rebuild. `SOURCE_PROVENANCE.json` in the binary ZIP identifies the source commit, binary hashes and package revision. Previous 0.1.4 archive and tag fingerprints are retained in the maintainer's release records.

The installer source is `community/d18-installer`. Its README and RELEASE_NOTES_0.1.4.md are the authoritative installation and release documents. The binary ZIP contains no NVIDIA NR/SR/FG runtime. The installer patches the user's own compatible NR runtime locally.

Regression tests, run only against temporary fixture directories:

```powershell
tools/D24/test-installer-upgrade.ps1 -PackageRoot <extracted-package> -Runtime <user-runtime> -OutputRoot <new-test-directory>
tools/D24/test-release-install.ps1 -PackageZip <package-zip> -Runtime <user-runtime>
```

The first test covers existing settings, native runtime discovery, API switching and deterministic upgrade rollback faults. The second covers installation, configuration persistence and uninstall for all three APIs. Neither launches a game or establishes gameplay correctness. Shared diagnostic entry points are under `tools/D18` and `tools/D24`; capture remains opt-in.

中文：使用 VS 2022 x64 Developer PowerShell，准备 Windows SDK 与仓库 external/library 依赖后执行构建脚本。本次安装器修订保留已验证的渲染二进制，ZIP 中 SOURCE_PROVENANCE.json 记录对应源码与指纹。安装测试只在独立目录运行，不启动游戏。
