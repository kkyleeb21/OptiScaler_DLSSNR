"""Exercise the production section wrapper and ImGui with offscreen WARP.

This verifies the common UI, not live DX11/DX12/Vulkan game input acquisition.
"""
import argparse
import os
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=pathlib.Path, required=True)
    p.add_argument("--imgui-objects", type=pathlib.Path, required=True)
    p.add_argument("--dependency-root", type=pathlib.Path, required=True)
    p.add_argument("--vcvars", default=r"C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat")
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    header = (ROOT / "OptiScaler/menu/menu_common.h").read_text(encoding="utf-8")
    section = header[header.index("class ScopedCollapsingHeader"):header.index("template <typename T> struct MenuOption")]
    (out / "scroll-section.inc").write_text(section, encoding="utf-8")
    objects = " ".join(f'"{a.imgui_objects / (name + ".obj")}"' for name in (
        "imgui", "imgui_draw", "imgui_widgets", "imgui_tables", "imgui_freetype", "imgui_impl_dx11"))
    command = (f'@echo off\ncall "{a.vcvars}" >nul\ncl /nologo /EHsc /std:c++20 /I"{out}" '
               f'/I"{ROOT / "OptiScaler"}" /I"{ROOT / "OptiScaler/include"}" '
               f'"{ROOT / "tools/D18/ui-scroll-test.cpp"}" /Fe:ui-scroll-test.exe /link {objects} '
               f'"{a.dependency_root / "external/freetype/freetype.lib"}" d3d11.lib d3dcompiler.lib dxgi.lib user32.lib gdi32.lib\n')
    (out / "build-scroll.cmd").write_text(command, encoding="utf-8")
    env = {k.upper(): v for k, v in os.environ.items()}
    subprocess.run(["cmd.exe", "/d", "/c", "build-scroll.cmd"], cwd=out, env=env, check=True)
    for scale in (0.5, 1, 2):
        for language in (0, 1):
            subprocess.run([str(out / "ui-scroll-test.exe"), str(scale), str(language)], cwd=out, check=True)


if __name__ == "__main__":
    main()
