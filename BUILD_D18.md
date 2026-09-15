# D18 0.1.8 revision 1 build provenance

R1 keeps both OptiScaler cores and the GUI launchers from the original 0.1.8 build. The shared native addon and runtime checker are rebuilt from this source with the same patch-site validator. Run tools/D24/Build-D18.ps1 -AddonOnly with Python 3, Visual Studio C++ tools and the documented dependency checkout. The build verifies generated patch tables. Public binaries include release only; diagnostic builds remain an internal workflow. Original 0.1.8 source/tag is preserved. See tools/D24/runtime-guard/README.md for host parity tests.

# Building D18 0.1.8

Use Visual Studio 2022 C++ tools and a Windows SDK, with dependencies initialized under external and OptiScaler/library. The main source tree corresponds to the exact C9 release core; tools/D24 contains the complete C9 native addon source. The diagnostic core additionally uses the four-file overlay in community/d18-diagnostic-overlay. Apply that overlay to a separate checkout and build with /p:D18DiagnosticBuild=1; release uses 0. Output/intermediate directories must be separate.

The original release reused recorded binaries. Revision 1 rebuilds the native addon and checker; core rendering binaries remain unchanged. Compiler/SDK/build labels may change rebuilt hashes. Internal candidate labels remain in those binaries; VERSION, the source tag and SOURCE_PROVENANCE.json identify this distribution. Build identity does not establish gameplay correctness.

Core: MSBuild OptiScaler/OptiScaler.vcxproj /p:Configuration=Release /p:Platform=x64 /p:D18DiagnosticBuild=0 (or 1) /p:SolutionDir=<dependency-checkout>/ /p:OutDir=<output>/ /p:IntDir=<intermediate>/ /p:PostBuildEventUseInBuild=false /p:PreBuildEventUseInBuild=false.

Native addon (shared by both profiles): cl /O2 /EHsc /std:c++20 /LD /I<repo>/OptiScaler /I<dependencies>/external/nvngx_dlss_sdk tools/D24/bg3-native/D24Native.cpp. Forwarder sources are under OptiScaler/dlssnr/forwarder; runtime checker sources under tools/D24/runtime-guard. Generated advanced shaders are included with source/provenance. Dependency installation is documented by the inherited setup scripts; an existing complete dependency checkout may be supplied.

Shared diagnostics and experiment tools are in tools/D18; they require the inputs shown by --help. Game logs, captures, original NR runtimes and private PDBs are not source/release payload. NVIDIA SR/FG/NR DLLs are not bundled. Installer source is community/d18-installer; tests use isolated temporary fixtures and never establish game compatibility.
