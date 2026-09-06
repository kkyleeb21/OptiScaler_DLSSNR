import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

def read(path):
    return (ROOT / path).read_text(encoding="utf-8")

class CommonPortTests(unittest.TestCase):
    def test_default_gates(self):
        cfg = read("OptiScaler/Config.h")
        for field in ("ExperimentalCompose", "GuidedReconstruction", "GainFirstReconstruction", "CatmullRomInput"):
            self.assertTrue(f"DlssNr{field} {{ false }}" in cfg, field)
        self.assertTrue("DlssNrDiagnostics { 0 }" in cfg)

    def test_full_resolution_and_capture_submission(self):
        dx = read("OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp")
        self.assertTrue("const float requestedWorkScale = 1.0f;" in dx)
        self.assertTrue("g_captureWriteAtFrame" not in dx)
        self.assertTrue("g_capture.readyToWrite() && g_captureSubmissions.complete()" in dx)
        self.assertTrue("g_capture.record(cmdList, device, g_nr.hdrCopy," in dx)
        sub = read("OptiScaler/dlssnr/CaptureSubmission.h")
        self.assertTrue("done == UINT64_MAX || done < 1" in sub)
        self.assertTrue("point->failed || !point->submitted" in sub)
        hooks = read("OptiScaler/resource_tracking/ResTrack_dx12.cpp")
        self.assertTrue(re.search(r"o_ExecuteCommandLists\([^;]+;\s+DlssNr::NotifyCommandListsSubmitted", hooks) is not None)

    def test_installer_is_generic_and_preserves_full_ini(self):
        script = read("community/d18-installer/Install-D18.ps1")
        self.assertTrue("onimushaOnly" not in script)
        self.assertIn("if ($reProfile.IsRE) { 'd3d12.dll' } else { 'dxgi.dll' }", script)
        self.assertTrue("$iniSource = if (Test-Path" in script)
        self.assertTrue(script.index("$iniSource = if") < script.index("& $windowsPowerShell"))

    def test_shader_constant_layout_matches(self):
        common = read("OptiScaler/shaders/dlssnr/DlssNr_Common.h").split("struct alignas(256) DlssNrConstants")[1].split("};")[0]
        shader = read("OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl").split("cbuffer Params : register(b0)")[1].split("};")[0]
        c = re.findall(r"\b(uint32_t|float)\s+(\w+)\s*;", common)
        h = re.findall(r"\b(uint|float)\s+g(\w+)\s*;", shader)
        self.assertEqual([(t.replace("uint32_t", "uint"), n) for t, n in c], h)

if __name__ == "__main__":
    unittest.main()
