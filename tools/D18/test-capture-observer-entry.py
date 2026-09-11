"""Regression for the production entry wiring absent from the standalone WARP host."""
from pathlib import Path
import importlib.util, unittest
root=Path(__file__).resolve().parents[2]/'OptiScaler'
code=(root/'shaders/dlssnr/DlssNr_Dx12.cpp').read_text()

class ObserverEntry(unittest.TestCase):
    def test_request_installs_before_recording(self):
        start=code.index('if (const auto requested = g_captureRequest.exchange(0); requested != 0)')
        request=code[start:code.index('if (g_capture.readyToWrite()',start)]
        self.assertLess(request.index('EnsureNativeSubmissionObserver(device)'),request.index('g_capture.request(requested)'))
        self.assertNotIn('activeFg',request)
        self.assertNotIn('RayReconstruction',request)
        self.assertIn('g_captureBusy.store(false)',request)
        self.assertIn('capture_rejected',request)
    def test_helper_does_not_require_opti_fg(self):
        text=(root/'resource_tracking/ResTrack_dx12.cpp').read_text()
        part=text[text.index('bool ResTrack_Dx12::EnsureNativeNrQueueObserver'):text.index('void ResTrack_Dx12::HookToQueue')]
        self.assertIn('HookToQueue(device)',part);self.assertNotIn('activeFg',part)
    def test_no_completion_shortcut(self):
        self.assertIn('g_capture.readyToWrite() && g_captureSubmissions.complete()',code)
        text=(root/'dlssnr/CaptureSubmission.h').read_text()
        self.assertIn('point->failed || !point->submitted',text)
        self.assertIn('done == UINT64_MAX || done < 1',text)
    def test_rejection_in_shared_summary(self):
        spec=importlib.util.spec_from_file_location('summary',Path(__file__).with_name('summarize-diagnostics.py'))
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        event=dict(type='capture_rejected',reason='submission_observer_unavailable',flags=0,fence_target=0,queue=0)
        self.assertEqual(module.summarize({},[event])['first_anomaly']['type'],'capture_rejected')

if __name__=='__main__':unittest.main()
