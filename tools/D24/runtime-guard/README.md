# DX11 Runtime host-layout check

`D18RuntimeCheck.exe <runtime.dll>` is a read-only offline checker. It prints one JSON result and returns 0 for a compatible host layout, 1 for rejection, 2 for invalid usage. The installer hash-verifies the helper as part of its payload and checks the patched temporary Runtime before uninstalling or copying any game files. The helper stays in the package; it is not deployed to games.

The DX11 addon compiles the same `check.h` and `rules.generated.h` and rechecks the file before loading it. `D24CheckRuntime` exposes this same read-only check for the regression suite. Existing in-memory vtable/instruction checks remain. `runtime_layout` events use the existing bounded native log and are included by `tools/D24/summarize_runtime.py`; missing events are reported as not observed.

## Scope of v1

This replaces whole-file SHA256 gating with a 310.8 host-layout policy. It validates x64 PE headers, section layout, relevant directories, complete host code, read-only data/vtables, unwind/relocation metadata, the D18 appended code when present, and initial/backend global state. The two known host-code variants are reference E16BCF15 and local community 8270B350, with their D18-patched equivalents. Their host code differs at 25 bytes (an immediate and several branch/selection sequences); these are accepted as complete known code variants, not wildcard bytes.

Model data between `.data` RVA +0x600 and RVA 0x1141000, model resources in `.rsrc`, and certificate data are outside the host-code fingerprint. This is intentionally not a whole-file whitelist, a model verifier, a DLL security scanner, or a GPU support certificate. Unknown host-code changes still need a rule update. Same host layout alone does not prove model contents are usable. The installer also retains the existing D18 patch-byte checks for every API.

`stock` in a check result denotes the unpatched PE layout, not verified provenance. Full-file hashes remain informational in installation state. Unknown inputs that pass are still classified as unverified/already patched by the installer.

## Rebuild and regression

`generate.py --stock <E16BCF15-file> --community <8270B350-file> --patch <runtime_patch.json>` regenerates the hash/metadata tables. It requires the documented input hashes and emits no runtime bytes. The generated header is checked in with the rule JSON. `tools/D24/Build-D18.ps1 -AddonOnly` builds both addon and helper.

`test.py --package <candidate> --old-package <baseline> --output <new-directory> --stock <reference> --community <community>` compares CLI and backend results, then exercises real installers in non-executable fixtures. It tests rejection before upgrade, changed-model input, protected-code/vtable changes, wrong architecture, truncation, three-API upgrades, config preservation and uninstall. Existing `test-installer-upgrade.ps1` additionally tests API switching and rollback after uninstall/copy faults. No game is launched by these tests.

Keep GUI work separate until game acceptance. This preview is not a published release.
