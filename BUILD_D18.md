# D18 0.1.9 build provenance

Base: dlssnr-d18-v0.1.8-r1 (81c8e33b597d3e4ba778ea5c6df0567d6d5be40b). Retains SH0 and experimental V8 from the reviewed previews. Core and native addon/checker are rebuilt from this source in Release x64 with D18_DIAGNOSTIC_BUILD=0. Diagnostic source is integrated and profile-gated; do not apply an old overlay.

Build OptiScaler/OptiScaler.vcxproj with MSBuild, D18DiagnosticBuild=0, and the existing dependency checkout. tools/D24/Build-D18.ps1 -AddonOnly -Profile Release builds the native addon and checker using the shared patch-site validator. Use the VS x64 developer environment, Python 3 and UTF-8 source compilation. GUI launchers, installer backend (apart from the explicit all-off fresh-install policy), forwarder and dependency payload are retained from 0.1.8 R1. Public SOURCE_PROVENANCE.json records component hashes and the release source commit. No NVIDIA runtime is distributed.
