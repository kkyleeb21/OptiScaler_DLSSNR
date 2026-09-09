"""Offline legacy state-schema fixtures; never executes game or runtime binaries."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess


def sha(path):
    return hashlib.file_digest(path.open("rb"), "sha256").hexdigest().upper()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--script", required=True, type=pathlib.Path)
    p.add_argument("--output", required=True, type=pathlib.Path)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    results = []
    env = dict(os.environ)
    env["PSModulePath"] = str(pathlib.Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/Modules")
    for version in ("community-20260903", "0.1.2", "0.1.3a", "missing-root-record"):
        game = (a.output / version).resolve()
        game.mkdir()
        backup = game / "D18_Backups/old-install"
        (backup / "files").mkdir(parents=True)
        (backup / "files/dxgi.dll").write_bytes(b"original proxy fixture, never loaded")
        (game / "dxgi.dll").write_bytes(b"installed D18 fixture, never loaded")
        (game / "nvngx_dlssnr.dll").write_bytes(b"runtime fixture, never loaded")
        records = [dict(target_relative="dxgi.dll", existed=True, backup_relative="files/dxgi.dll", installed_sha256=sha(game / "dxgi.dll")),
                   dict(target_relative="nvngx_dlssnr.dll", existed=False, backup_relative=None, installed_sha256=sha(game / "nvngx_dlssnr.dll"))]
        state = dict(format="dlssnr-d18-install-state-v1", game_dir=str(game), backup_relative="D18_Backups/old-install", files=records)
        if version != "community-20260903":
            state.update(package_name="D18", package_version=version)
        encoded = json.dumps(state)
        (backup / "install-state.json").write_text(encoded, encoding="utf-8")
        if version != "missing-root-record":
            (game / ".dlssnr-d18-install.json").write_text(encoded, encoding="utf-8")
        (game / "dxgi.dll").write_bytes(b"post-install changed proxy, never loaded")
        proc = subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(a.script.resolve()),
                               "-GameDir", str(game), "-Yes"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        (a.output / (version + ".log")).write_bytes(proc.stdout)
        if version == "missing-root-record":
            assert proc.returncode == 1
            assert (game / "dxgi.dll").read_bytes() == b"post-install changed proxy, never loaded"
            assert (game / "nvngx_dlssnr.dll").is_file()
        else:
            assert proc.returncode == 0
            assert (game / "dxgi.dll").read_bytes() == (backup / "files/dxgi.dll").read_bytes()
            assert (backup / "post-install-user-files/dxgi.dll").read_bytes() == b"post-install changed proxy, never loaded"
            assert not (game / "nvngx_dlssnr.dll").exists()
            assert not (game / ".dlssnr-d18-install.json").exists()
        results.append(dict(case=version, exit_code=proc.returncode, verified=True))
    (a.output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results))


if __name__ == "__main__":
    main()
