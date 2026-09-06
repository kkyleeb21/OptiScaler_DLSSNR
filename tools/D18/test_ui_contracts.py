import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKTREE = ROOT / "OptiScaler"


class UiContractTests(unittest.TestCase):
    def test_collapsing_children_forward_wheel_to_parent(self):
        text = (WORKTREE / "menu/menu_common.h").read_text(encoding="utf-8")
        begin = text[text.index('ImGui::BeginChild("##CollapsingHeaderChild"'):]
        call = begin[:begin.index(");")]
        self.assertIn("ImGuiWindowFlags_NoScrollWithMouse", call)
        self.assertNotIn("ImGuiWindowFlags_NoScrollbar", call)

    def test_diagnostic_ui_has_mode_and_live_code(self):
        text = (WORKTREE / "dlssnr/DlssNr_Menu.cpp").read_text(encoding="utf-8")
        d18 = text[text.index("void RenderD18Menu"):]
        self.assertIn('"Off", "Summary", "Trace"', text)
        self.assertIn("Diagnostics::Latest()", d18)
        self.assertIn("code 0x%08X", d18)

    def test_original_input_route_is_not_replaced_by_onimusha_polling(self):
        windows = (WORKTREE / "menu/input/input_system_windows_hooks.cpp").read_text(encoding="utf-8")
        menu = (WORKTREE / "menu/menu_common.cpp").read_text(encoding="utf-8")
        self.assertTrue("PollingWheelThreadProc" not in windows)
        self.assertTrue("PollingOnly" not in menu)
        self.assertTrue('Keybind("UI hotkey", 110)' in menu)
        self.assertTrue('Keybind("NR hotkey", 111)' in menu)

    def test_original_sharpener_is_preserved(self):
        menu = (WORKTREE / "menu/menu_common.cpp").read_text(encoding="utf-8")
        nr = (WORKTREE / "shaders/dlssnr/DlssNr_Dx12.cpp").read_text(encoding="utf-8")
        self.assertTrue("Enable OptiScaler sharpening (RCAS/DA)" in menu)
        self.assertTrue("resolveParams.PostSharpness = 0.0f;" in nr)
        self.assertTrue("nativePostSharpenRoute" not in nr)
        self.assertTrue("OnimushaWotS.exe" not in nr)

    def test_guided_reconstruction_is_a_live_ab_control(self):
        menu = (WORKTREE / "dlssnr/DlssNr_Menu.cpp").read_text(encoding="utf-8")
        config_h = (WORKTREE / "Config.h").read_text(encoding="utf-8")
        config_cpp = (WORKTREE / "Config.cpp").read_text(encoding="utf-8")
        self.assertIn("Guided network reconstruction##d18", menu)
        self.assertIn("DlssNrGuidedReconstruction { false }", config_h)
        self.assertIn('readBool("DlssNr", "GuidedReconstruction")', config_cpp)


if __name__ == "__main__":
    unittest.main()
