# Building D18 0.1.6

Use Visual Studio 2022 C++ build tools, the Windows SDK and an x64 Developer PowerShell. Clone with submodules (`git submodule update --init --recursive`). The core uses headers/libraries under `external` and `OptiScaler/library`; initialize these dependencies before building. NVIDIA NR runtime DLLs are not build inputs and are not distributed here.

Run `tools/D24/Build-D18.ps1`. An existing dependency checkout can be supplied with `-DependencyRoot <checkout-root>`; it must contain `external`. Use `-OutputDirectory <path>` for build output. The script builds the core, NGX forwarder and DX11 native addon; `-AddonOnly` builds the native addon alone. Its sources and generated shader source are in `tools/D24/bg3-native`.

0.1.6 is cumulative from public 0.1.4a, including the private 0.1.5 integration and all subsequent validated source changes. The release package reuses the exact validated binaries: core `057C36C0AB9180B1CEFEE95169B29E7680CD1E63C2C04ED779DF8F6C614F2AF6`, native addon `21AE22965E45931245C5F1B55A1C4C28FA7716C3E9D24E4831BA8BEA72AFD459`, forwarder `D187321BCD70F3C3A0B1BB18E4178CAC0C6A8AAA585C05352EDD2DA8A22D722E`.

The embedded build-date/commit labels are retained from the tested build and are not the package version or a claim that this binary was built from an unmodified old commit. The 0.1.6 source/tag and ZIP SOURCE_PROVENANCE.json identify the complete source and artifact hashes. Compiler, SDK and embedded metadata may change hashes on a rebuild. Python bytecode caches are omitted from release source; no production source or shader was dropped.

Installer source is `community/d18-installer`. Its README and RELEASE_NOTES_0.1.6.md are the installation and cumulative release documents. Build-D18Release.ps1 packages a validated staging installation using explicit core and forwarder hashes. The binary ZIP contains no NVIDIA NR/SR/RR/FG/Streamline runtime; the installer patches the user's compatible NR runtime locally.

Installer regressions run only against temporary fixtures:

```powershell
tools/D24/test-installer-upgrade.ps1 -PackageRoot <extracted-package> -Runtime <user-runtime> -OutputRoot <new-fixture-directory>
tools/D24/test-release-install.ps1 -PackageZip <package-zip> -Runtime <user-runtime>
```

These checks cover settings, API switching, upgrade rollback, installation and uninstall. They do not launch games or establish gameplay correctness. Shared capture and diagnostic tools are under tools/D18 and tools/D24; capture is opt-in. Runtime, game logs, captures and personal configuration do not belong in source or release artifacts.

中文：0.1.6 完整继承公开 0.1.4a 之后的 0.1.5 私测整合及后续修复。发布包复用已验收二进制，保留嵌入构建标签，真实源码来源与哈希由标签及 SOURCE_PROVENANCE.json 对应。构建依赖和编译器变化可能导致重新构建的哈希不同。安装回归只操作独立目录，不启动游戏。
