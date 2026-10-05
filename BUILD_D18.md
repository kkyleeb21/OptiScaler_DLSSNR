# D18 0.3.0 build provenance

Source baseline: release-021 / bca72ab plus the release030 patch. No new commit or tag is created by this build. Core and native addon/checker use Release x64 with D18DiagnosticBuild=0. Existing forwarder, DXC, Agility and optional AMD/Intel components come from the explicit local read-only 0.2.0 package copy. No NVIDIA runtime is distributed.

Use `community/d18-installer/Build-Installer1.py` with explicit --build, --report and --previous local paths. Run core; build `tools/D24/Build-D18.ps1 -AddonOnly -Profile Release` in a VS x64 environment to <build>/native-output; then stage, refresh, exe, zip. The script name is retained for continuity, and its output names are DLSSNR_D18_0.3.0_release and DLSSNR_D18_0.3.0_optional_components. resource_build_commit.h records the actual current short commit hash. The build uses the existing dependency checkout without modifying it.
