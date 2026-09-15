# Shared DX11 runtime patch compatibility check

The installer helper D18RuntimeCheck.exe and the native addon D24CheckRuntime compile the same check.h and patch-sites.generated.h. Run generate-patch-sites.py from this source tree to regenerate the table from community/d18-installer/runtime_patch.json. No NVIDIA runtime is needed for generation.

Only basic x64 PE validity, the manifest patch ranges and the seven bytes written by the native DX11 adapter are checked. Whole-file hashes and unrelated code/data-region fingerprints are not admission conditions. Actual in-memory vtable/instruction checks remain. Passing this check is not proof of arbitrary runtime, model, hardware or game compatibility.

Build both binaries together with tools/D24/Build-D18.ps1 -AddonOnly. Run test.py --addon <D24Native.dll> --checker <D18RuntimeCheck.exe> --runtime <user-original-NR> --output <new-evidence-directory>. It compares both validators on original/patched and deliberately invalid synthetic files, without loading or executing the NR runtime. Keep user originals unchanged.

0.1.8 revision 1 fixes the previously distributed addon using a stale host-region checker. Old tables/generator are retained only in the original 0.1.8 source tag, not active here.
