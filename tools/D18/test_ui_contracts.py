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
        detours=(WORKTREE / "menu/input/input_system_detours.cpp").read_text(encoding="utf-8")
        lifecycle=(WORKTREE / "menu/input/input_system.cpp").read_text(encoding="utf-8")
        self.assertIn("static LRESULT CALLBACK PollingWheelProc",detours)
        self.assertIn("msg.message == WM_MOUSEWHEEL",detours)
        self.assertIn("_state.MouseWheel += delta",detours)
        self.assertIn("WH_GETMESSAGE",detours)
        self.assertIn("UpdatePollingWheelHookLocked();",lifecycle)

    def test_sharpening_controls_keep_the_current_sh0_route(self):
        menu = (WORKTREE / "dlssnr/DlssNr_Menu.cpp").read_text(encoding="utf-8")
        nr = (WORKTREE / "shaders/dlssnr/DlssNr_Dx12.cpp").read_text(encoding="utf-8")
        self.assertIn("Enable D18 sharpening",menu)
        self.assertIn("DlssNrSh0Mid",menu)
        self.assertIn("DlssNrSh0Fine",menu)
        self.assertIn("resolveParams.PostSharpness = 0.0f",nr)
        self.assertIn("_sh0->Prepare",nr)
        self.assertIn("_sh0Half->Prepare",nr)

    def test_guided_reconstruction_is_a_live_ab_control(self):
        menu = (WORKTREE / "dlssnr/DlssNr_Menu.cpp").read_text(encoding="utf-8")
        config_h = (WORKTREE / "Config.h").read_text(encoding="utf-8")
        config_cpp = (WORKTREE / "Config.cpp").read_text(encoding="utf-8")
        self.assertIn("Guided network reconstruction##d18", menu)
        self.assertIn("DlssNrGuidedReconstruction { false }", config_h)
        self.assertIn('readBool("DlssNr", "GuidedReconstruction")', config_cpp)


if __name__ == "__main__":
    unittest.main()
