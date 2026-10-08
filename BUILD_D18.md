# D18 0.4.1 build provenance

Source baseline: release-041 / bb0917c plus release041.patch. No new commit or tag is created by this build. Core and native addon/checker use Release x64 with D18DiagnosticBuild=0. Existing forwarder, DXC, Agility and optional AMD/Intel components come from the explicit local read-only 0.4.0 packages. No NVIDIA runtime is distributed.

Use `community/d18-installer/Build-Installer1.py` with explicit --build, --report and --previous local paths. For this release, --previous is `E:/DLSSNR/builds/D18_040_RELEASE_20261008/DLSSNR_D18_0.4.0_release`; unchanged optional DLLs are read from its sibling `DLSSNR_D18_0.4.0_optional_components`. Run core; build `tools/D24/Build-D18.ps1 -AddonOnly -Profile Release` in a VS x64 environment to <build>/native-output; then stage, refresh, exe, zip. The script name is retained for continuity, and its output names are DLSSNR_D18_0.4.1_release and DLSSNR_D18_0.4.1_optional_components. resource_build_commit.h records the actual current short commit hash. The build uses the existing dependency checkout without modifying it.

## Compile-time provenance

The scripted core build writes `core-output/build-provenance.json`; `tools/D24/Build-D18.ps1`
writes `native-output/build-provenance.json` for the native addon and checker. The managed
installer build writes `installer-build-provenance.json`. Each snapshot records the full HEAD,
branch, dirty file names and binary Git patch SHA-256 (including untracked files), dependency
directory content hashes, tool versions/paths/hashes, exact executed argv and cwd, Release/x64
and diagnostic properties, UTC times and output sizes/SHA-256. The project path in argv is the
actual build input; the current script builds a vcxproj directly, not a solution.

Binary builds use the existing precompiled shader headers. Their exact input hashes are recorded;
no shader compilation is claimed. Original generation commands without recorded evidence remain
`unknown`. A build that regenerates shaders must also retain its actual shader command and generated
header hashes in the build evidence. Missing Git/tool metadata is recorded as `unknown` with its
reason. Native evidence collection failures warn without failing normal compilation; packaging
requires valid manifests. Direct IDE builds are unchanged; use the scripted workflow for release
provenance. `--dependency-root` selects an explicit local dependency checkout.

`stage` and `refresh` verify compiled binary hashes against their manifests before copying.
`exe` records the embedded ZIP input hash. `verify` checks both the independent payload manifest
and provenance hashes without creating release ZIPs; `zip` performs the same checks before archiving.
The payload-only provenance is archived next to the build and referenced by the embedded
`D18/release-manifest.json`. The complete four-component `build-provenance.json` and its referencing
`release-manifest.json` accompany the installer at the package root. This avoids the circular hash
that would result from embedding an installer's own final hash inside itself. Keep all three compile
manifests and the merged manifests with the release artifacts. This is provenance, not a promise of
bit-for-bit reproducibility.

Compile manifests also retain their exact dirty-source patch beside the output. `core --rebuild`
requests a complete core rebuild; the selected MSVC and Windows SDK environment versions are pinned
in the actual MSBuild argv when available.

For offline validation, pass `--validation-only` to every builder invocation: staging names clearly
identify test data and `zip` is forbidden. Put build, reports, TEMP/TMP and fixture roots inside the
authorized report directory. No deployment or game scan is part of this build workflow.
