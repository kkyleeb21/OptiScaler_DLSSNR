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

    def test_polling_only_menu_has_wheel_observer(self):
        windows = (WORKTREE / "menu/input/input_system_windows_hooks.cpp").read_text(encoding="utf-8")
        lifecycle = (WORKTREE / "menu/input/input_system.cpp").read_text(encoding="utf-8")
        self.assertIn("if (_state.PollingOnly)", windows)
        self.assertIn("wParam == WM_MOUSEWHEEL", windows)
        self.assertIn("_state.MouseWheel += static_cast<float>(wheel)", lifecycle)
        self.assertIn("UpdateExternalMouseHookLocked();", lifecycle)
        self.assertIn("PollingWheelThreadProc", windows)
        self.assertIn("InterlockedExchangeAdd(&_state.PollingWheelDelta", windows)

    def test_onimusha_native_route_has_real_post_nr_sharpening(self):
        menu = (WORKTREE / "menu/menu_common.cpp").read_text(encoding="utf-8")
        nr = (WORKTREE / "shaders/dlssnr/DlssNr_Dx12.cpp").read_text(encoding="utf-8")
        shader = (WORKTREE / "shaders/dlssnr/precompile/dlssnr.hlsl").read_text(encoding="utf-8")
        self.assertIn("Enable integrated post-NR sharpening", menu)
        self.assertIn("nr.postSharpenedFrames", menu)
        compose = nr[nr.index("const bool nativePostSharpenRoute"):]
        self.assertIn('"OnimushaWotS.exe"', compose)
        self.assertIn("resolveParams.PostSharpness", compose)
        self.assertNotIn("g_postRcas", nr)
        self.assertIn("OriginalLowCross", shader)
        self.assertIn("result + (original - originalLow)", shader)
        self.assertIn("++g_postSharpenedFrames", compose)

    def test_guided_reconstruction_is_a_live_ab_control(self):
        menu = (WORKTREE / "dlssnr/DlssNr_Menu.cpp").read_text(encoding="utf-8")
        config_h = (WORKTREE / "Config.h").read_text(encoding="utf-8")
        config_cpp = (WORKTREE / "Config.cpp").read_text(encoding="utf-8")
        self.assertIn("Guided network reconstruction##d18", menu)
        self.assertIn("DlssNrGuidedReconstruction { false }", config_h)
        self.assertIn('readBool("DlssNr", "GuidedReconstruction")', config_cpp)


if __name__ == "__main__":
    unittest.main()
