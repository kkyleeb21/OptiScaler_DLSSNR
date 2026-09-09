"""Merge recognised release payload hashes for conservative manual D18 cleanup.

No binary/runtime contents are embedded. Existing entries are retained so newer
packages keep recognising older manually copied D18 files.
"""
import argparse
import hashlib
import json
import pathlib


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--releases", required=True, type=pathlib.Path)
    p.add_argument("--output", required=True, type=pathlib.Path)
    p.add_argument("--verified-removal-record", type=pathlib.Path)
    a = p.parse_args()
    entries = {}

    def add(path, digest, kind):
        path = path.replace("/", "\\")
        item = entries.setdefault(path, {"path": path, "kind": kind, "sha256": set()})
        item["sha256"].add(digest.upper())

    if a.output.exists():
        for item in json.loads(a.output.read_text(encoding="utf-8-sig"))["files"]:
            for digest in item["sha256"]:
                add(item["path"], digest, item["kind"])
    manifests = list(a.releases.glob("*/payload_manifest.json"))
    for manifest in manifests:
        data = json.loads(manifest.read_text(encoding="utf-8-sig"))
        for item in data["files"]:
            name = item["path"].replace("/", "\\")
            for prefix in ("", "_storage_\\"):
                if name == "OptiScaler.dll":
                    for proxy in ("dxgi.dll", "d3d12.dll", "winmm.dll", "version.dll", "dbghelp.dll"):
                        add(prefix + proxy, item["sha256"], "core")
                elif name in ("nvngx.dll_dlssnr.dll", "D24Native.dll"):
                    add(prefix + name, item["sha256"], "forwarder" if name.startswith("nvngx") else "native")
                elif name.startswith("OptiScaler\\"):
                    add(prefix + name, item["sha256"], "dependency")
        patch = manifest.parent / "runtime_patch.json"
        if patch.exists():
            digest = json.loads(patch.read_text(encoding="utf-8-sig")).get("reference_output_sha256")
            if digest:
                for prefix in ("", "_storage_\\"):
                    for name in ("nvngx_dlssnr.dll", "D24Runtime.dll"):
                        add(prefix + name, digest, "runtime")
    if a.verified_removal_record:
        evidence = json.loads(a.verified_removal_record.read_text(encoding="utf-8-sig"))
        assert evidence["verified"] is True
        for item in evidence["files"]:
            name = item["relative"].replace("/", "\\")
            kind = {"dxgi.dll": "core", "nvngx.dll_dlssnr.dll": "forwarder", "nvngx_dlssnr.dll": "runtime", "D24Runtime.dll": "runtime", "D24Native.dll": "native"}.get(name)
            if name.startswith("OptiScaler\\"):
                kind = "dependency"
            if not kind:
                continue
            path = pathlib.Path(evidence["backup"]) / name
            with path.open("rb") as stream:
                assert hashlib.file_digest(stream, "sha256").hexdigest().upper() == item["sha256"].upper()
            for prefix in ("", "_storage_\\"):
                if kind == "core":
                    for proxy in ("dxgi.dll", "d3d12.dll", "winmm.dll", "version.dll", "dbghelp.dll"):
                        add(prefix + proxy, item["sha256"], kind)
                else:
                    add(prefix + name, item["sha256"], kind)
    result = {"format": "d18-uninstall-catalog-v1", "files": [dict(item, sha256=sorted(item["sha256"])) for _, item in sorted(entries.items())]}
    a.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Merged {len(manifests)} manifests; {len(entries)} recognised paths")


if __name__ == "__main__":
    main()
