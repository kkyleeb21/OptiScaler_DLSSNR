"""Functional Windows PowerShell 5.1 regression suite in disposable fixture games."""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]


def digest(data):
    return hashlib.sha256(data).hexdigest().upper()


def snapshot(root):
    return {str(p.relative_to(root)): digest(p.read_bytes()) for p in root.rglob("*") if p.is_file()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=pathlib.Path)
    a = parser.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True)
    package = out / "package"
    package.mkdir()
    for name in ("Uninstall-D18.ps1", "D18-Uninstall.ps1", "uninstall-catalog.json"):
        shutil.copy2(ROOT / "community/d18-installer" / name, package / name)
    catalog = json.loads((package / "uninstall-catalog.json").read_text())
    for name, kind, data in (("dxgi.dll", "core", b"installed-core"), ("_storage_/d3d12.dll", "core", b"installed-core"),
                             ("nvngx_dlssnr.dll", "runtime", b"installed-runtime"), ("OptiScaler/sdk.dll", "dependency", b"sdk")):
        catalog["files"].append(dict(path=name, kind=kind, sha256=[digest(data)]))
    (package / "uninstall-catalog.json").write_text(json.dumps(catalog))
    env = {k.upper(): v for k, v in os.environ.items()}
    env["PSMODULEPATH"] = str(pathlib.Path(env["SYSTEMROOT"]) / "System32/WindowsPowerShell/v1.0/Modules")
    results = []

    def run(game, expected=0, args=(), pkg=package):
        proc = subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(pkg / "Uninstall-D18.ps1"),
                               "-GameDir", str(game), "-Yes", *args], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        log = out / f"{len(results):02d}-{game.name}.log"
        log.write_bytes(proc.stdout)
        assert proc.returncode == expected, f"{log}: expected {expected}, got {proc.returncode}: {proc.stdout.decode(errors='replace')}"
        results.append(dict(case=game.name, arguments=list(args), exit_code=proc.returncode, passed=True))

    def fixture(name, version="0.1.2", root_record=True, modified=False):
        game = out / name
        backup = game / "D18_Backups/old-install"
        (backup / "files").mkdir(parents=True)
        (backup / "files/dxgi.dll").write_bytes(b"pre-install-original")
        (game / "dxgi.dll").write_bytes(b"changed-core" if modified else b"installed-core")
        (game / "nvngx_dlssnr.dll").write_bytes(b"installed-runtime")
        (game / "Game.exe").write_bytes(b"inert game sentinel; never executed")
        state = dict(format="dlssnr-d18-install-state-v1", game_dir=str(game), proxy_name="dxgi.dll", backup_relative="D18_Backups/old-install",
                     files=[dict(target_relative="dxgi.dll", existed=True, backup_relative="files/dxgi.dll", installed_sha256=digest(b"installed-core")),
                            dict(target_relative="nvngx_dlssnr.dll", existed=False, backup_relative=None, installed_sha256=digest(b"installed-runtime"))])
        if version is not None:
            state.update(package_name="D18", package_version=version)

        def save():
            text = json.dumps(state)
            (backup / "install-state.json").write_text(text)
            if root_record:
                (game / ".dlssnr-d18-install.json").write_text(text)
        save()
        return game, backup, state, save

    for name, version in (("community", None), ("old-012", "0.1.2"), ("old-013a", "0.1.3a"), ("current", "0.1.4"), ("future-same-schema", "99.0.0")):
        game, backup, state, save = fixture(name, version, modified=True)
        before = snapshot(game)
        run(game)
        assert (game / "dxgi.dll").read_bytes() == b"pre-install-original"
        assert not (game / "nvngx_dlssnr.dll").exists()
        assert not (game / ".dlssnr-d18-install.json").exists()
        recovery = next((game / "D18_Backups").glob("uninstall-recovery-*"))
        assert (recovery / "files/dxgi.dll").read_bytes() == b"changed-core"
        assert digest((recovery / "root-state.json").read_bytes()) == before[".dlssnr-d18-install.json"]
        assert digest((game / "Game.exe").read_bytes()) == before["Game.exe"]
    game, _, _, _ = fixture("preview")
    before = snapshot(game); run(game, args=("-PlanOnly",)); assert snapshot(game) == before
    game, _, _, _ = fixture("backup-auto", root_record=False)
    run(game); assert (game / "dxgi.dll").read_bytes() == b"pre-install-original"
    # A completed backup must not be replayed on a later invocation.
    before = snapshot(game); run(game); assert snapshot(game) == before
    game, backup, state, save = fixture("backup-explicit", root_record=False, modified=True)
    before = snapshot(game); run(game, 1); assert snapshot(game) == before
    run(game, args=("-StateFile", str(backup / "install-state.json")))
    game, backup, state, save = fixture("ambiguous", root_record=False)
    other = game / "D18_Backups/another"
    shutil.copytree(backup, other)
    alternate = dict(state, backup_relative="D18_Backups/another")
    (other / "install-state.json").write_text(json.dumps(alternate))
    before = snapshot(game); run(game, 1); assert snapshot(game) == before
    for name, change in (
        ("unknown-schema", lambda s: s.update(format="dlssnr-d18-install-state-v99")),
        ("other-game", lambda s: s.update(game_dir=str(out / "another-game"))),
        ("duplicate", lambda s: s["files"].append(dict(s["files"][0], target_relative="DXGI.dll"))),
        ("traversal", lambda s: s["files"][1].update(target_relative="../outside.dll")),
        ("ads", lambda s: s["files"][1].update(target_relative="innocent.txt:stream")),
        ("executable", lambda s: s["files"][1].update(target_relative="Game.exe")),
        ("reserved", lambda s: s["files"][1].update(target_relative="D18_Backups/old-install/files/dxgi.dll")),
        ("missing-late-backup", lambda s: s["files"][1].update(existed=True, backup_relative="files/missing.dll")),
        ("bad-bool", lambda s: s["files"][0].update(existed="false")),
    ):
        game, _, state, save = fixture(name); change(state); save()
        before = snapshot(game); run(game, 1); assert snapshot(game) == before
    game, _, _, _ = fixture("managed-cannot-bypass")
    before = snapshot(game); run(game, 1, ("-Manual",)); assert snapshot(game) == before
    game, _, _, _ = fixture("corrupt-root")
    (game / ".dlssnr-d18-install.json").write_text("{broken")
    before = snapshot(game); run(game, 1); assert snapshot(game) == before
    game, _, _, _ = fixture("escaped-state", root_record=False)
    before = snapshot(game); run(game, 1, ("-StateFile", str(out / "install-state.json"))); assert snapshot(game) == before
    game = out / "storage-only"
    (game / "_storage_").mkdir(parents=True)
    (game / "_storage_/d3d12.dll").write_bytes(b"installed-core")
    (game / "_storage_/OptiScaler.ini").write_bytes(b"storage config")
    (game / "OptiScaler.ini").write_bytes(b"unrelated root config")
    run(game)
    assert not (game / "_storage_/OptiScaler.ini").exists()
    assert (game / "OptiScaler.ini").read_bytes() == b"unrelated root config"
    for name, core in (("manual-known", b"installed-core"), ("manual-unknown", b"unrelated graphics proxy")):
        game = out / name; (game / "OptiScaler").mkdir(parents=True)
        (game / "dxgi.dll").write_bytes(core); (game / "OptiScaler/sdk.dll").write_bytes(b"sdk")
        (game / "OptiScaler.ini").write_bytes(b"user options")
        (game / "amd_fidelityfx_dx12.dll").write_bytes(b"native game file")
        before = snapshot(game); run(game, args=("-PlanOnly",)); assert snapshot(game) == before
        run(game)
        assert (game / "amd_fidelityfx_dx12.dll").read_bytes() == b"native game file"
        if name == "manual-known":
            assert not (game / "dxgi.dll").exists() and not (game / "OptiScaler.ini").exists()
            recovery = next((game / "D18_Backups").glob("uninstall-recovery-*"))
            assert (recovery / "files/OptiScaler.ini").read_bytes() == b"user options"
        else:
            assert snapshot(game) == before
            run(game, args=("-Manual", "-ManualFiles", "dxgi.dll"))
            assert not (game / "dxgi.dll").exists()
    # Failure after one operation must restore every changed file and root state.
    fault = out / "fault-package"; shutil.copytree(package, fault)
    path = fault / "D18-Uninstall.ps1"
    text = path.read_text()
    needle = "}elseif(Test-Path -LiteralPath $item.Target -PathType Leaf){Remove-Item -LiteralPath $item.Target -Force}"
    assert text.count(needle) == 1
    path.write_text(text.replace(needle, needle + "\n            if($applied.Count -eq 1){throw 'injected test failure'}"))
    game, _, _, _ = fixture("rollback", modified=True)
    before = snapshot(game); run(game, 1, pkg=fault)
    after = snapshot(game)
    for name, value in before.items():
        assert after[name] == value, name
    # Missing directory and -Yes without a directory cannot prompt indefinitely.
    run(out / "absent-game", 1)
    (out / "results.json").write_text(json.dumps(results, indent=2))
    print(f"PASS {len(results)} universal uninstall cases")


if __name__ == "__main__":
    main()
