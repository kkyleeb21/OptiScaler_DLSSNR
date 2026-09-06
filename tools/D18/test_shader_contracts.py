import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SHADER = (ROOT / "OptiScaler/"
          "shaders/dlssnr/precompile/dlssnr.hlsl")


class ShaderContractTests(unittest.TestCase):
    def test_experimental_gain_first_and_kernel_are_isolated(self):
        source = SHADER.read_text(encoding="utf-8")
        dx12 = (SHADER.parents[1] / "DlssNr_Dx12.cpp").read_text(encoding="utf-8")
        self.assertIn("cellGain += weight * clamp", source)
        self.assertIn("gain = cellGain;", source)
        self.assertIn("pxy = 0.25 * (pxy", source)
        self.assertIn("expectedNetworkWidth * 2 == width", dx12)
        self.assertIn("expectedNetworkHeight * 2 == height", dx12)
        self.assertIn("gCatmullRomInput != 0 ? 0.0 : 1.0 / 3.0", source)
        self.assertIn("g_nr.activeInputKernel != activeInputKernel", dx12)
        self.assertIn("NR history reset", dx12)

    def test_matched_residual_uses_logical_network_ratio(self):
        source = SHADER.read_text(encoding="utf-8")
        self.assertTrue("const bool modelRanSmall = gExperimentalCompose != 0" in source)
        self.assertTrue("? min(gNetworkRatioX, gNetworkRatioY) < 0.999" in source)
        self.assertTrue(": (proxyW != gWidth || proxyH != gHeight)" in source)
        self.assertTrue("gExperimentalCompose == 0 && gPreserveHighFrequency" in source)

    def test_post_nr_sharpening_stays_inside_compose_dispatch(self):
        source = SHADER.read_text(encoding="utf-8")
        dx12 = (SHADER.parents[1] / "DlssNr_Dx12.cpp").read_text(encoding="utf-8")
        common = (SHADER.parents[1] / "DlssNr_Common.h").read_text(encoding="utf-8")
        self.assertIn("float gPostSharpness;", source)
        self.assertIn("OriginalLowCross", source)
        self.assertIn("result + (original - originalLow)", source)
        self.assertIn("float PostSharpness;", common)
        self.assertIn("resolveParams.PostSharpness", dx12)
        self.assertNotIn("RCAS_Dx12", dx12)
        self.assertNotIn("g_postRcas", dx12)

    def test_guided_reconstruction_uses_network_cells_and_full_res_guide(self):
        source = SHADER.read_text(encoding="utf-8")
        self.assertIn("uint gGuidedReconstruction;", source)
        self.assertIn("GuidedNetworkRgb", source)
        self.assertIn("NetworkCellPhysical", source)
        self.assertIn("gOriginal.Load", source)
        self.assertIn("pixel-centred bilinear reconstruction", source)
        self.assertIn("const float3 lumaOnly = targetLuma.xxx + originalChroma", source)
        self.assertIn("modelColour = HueOkLab(guidedModel", source)


if __name__ == "__main__":
    unittest.main()
