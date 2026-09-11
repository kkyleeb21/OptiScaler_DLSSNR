# D18 patch-site validation

The installer and DX11 addon share check.h and patch-sites.generated.h. These check the installer manifest patch locations plus the seven-byte instruction modified by the native adapter at RVA 0x20f2c. There is no full-file, host-code, vtable-data or read-only-section fingerprint gate. Basic x64 PE validity and readable bounds remain required. Existing runtime initialization and operation checks remain; installation acceptance is not execution compatibility.

The CLI checker returns JSON with rule d18-patch-sites-v1; installer diagnostics and bounded native runtime_layout events retain their existing schema. Unknown files remain UNVERIFIED_COMPATIBLE or ALREADY_PATCHED, not automatically VERIFIED. Recommended inputs are the original runtime or a compatible RenoDX Discord community version; compatibility with every community modification is not guaranteed.

Regenerate with generate-patch-sites.py from the installer runtime_patch.json. The installer and native backend must be rebuilt and released together. Older generate.py/rules.generated.h are historical host-layout rules and are not included by check.h.
