import importlib.util
import pathlib
import unittest
import numpy as np

spec = importlib.util.spec_from_file_location("colour", pathlib.Path(__file__).with_name("compare-capture-colour.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ColourTests(unittest.TestCase):
    def test_fraction_exposure_invariance(self):
        rgb = np.array([[[0.1, 0.2, 0.4]]])
        np.testing.assert_allclose(module.fractions(rgb), module.fractions(rgb * 5))

    def test_decode_and_block(self):
        np.testing.assert_allclose(module.linearize(np.array([0., 0.04045, 1.])),
                                   [0., 0.003130807, 1.], atol=1e-8)
        np.testing.assert_allclose(module.block_mean(np.ones((16, 16, 3))), np.ones((2, 2, 3)))

    def test_identical_inputs_and_empty_mask(self):
        rgb = np.arange(1, 49, dtype=float).reshape(4, 4, 3)
        streams = {k: rgb for k in ("before", "after", "model_input", "model_output")}
        result = module.compare(streams, streams, 0.01)
        self.assertGreater(result["eligible_blocks"], 0)
        self.assertEqual(result["difference_of_colour_edits_rms"]["model"], 0.)
        flat = {k: np.ones((4, 4, 3)) for k in streams}
        self.assertIsNone(module.compare(flat, flat, 0.01)["difference_of_colour_edits_rms"]["model"])


if __name__ == "__main__":
    unittest.main()
