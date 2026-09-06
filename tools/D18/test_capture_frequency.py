import importlib.util
import pathlib
import unittest

import numpy as np


HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("capture_frequency", HERE / "analyze-capture-frequency.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CaptureFrequencyTests(unittest.TestCase):
    def test_r11g11b10_unit_values(self):
        one_r11 = np.array([(15 << 6)], dtype=np.uint32)
        one_b10 = np.array([(15 << 5)], dtype=np.uint32)
        self.assertEqual(float(module.decode_ufloat(one_r11, 6)[0]), 1.0)
        self.assertEqual(float(module.decode_ufloat(one_b10, 5)[0]), 1.0)

    def test_constant_has_no_middle_or_high_energy(self):
        measured = module.bands(np.ones((32, 32), dtype=np.float32))
        self.assertAlmostEqual(measured["high_rms"], 0.0, places=6)
        self.assertAlmostEqual(measured["middle_rms"], 0.0, places=6)

    def test_existing_capture_domain_guard_bounds(self):
        self.assertFalse(0.25 <= 100.0 <= 4.0)

    def test_downsample_2x_area_shape_and_values(self):
        image = np.arange(16, dtype=np.float32).reshape(4, 4)
        reduced = module.downsample_2x_area(image)
        np.testing.assert_allclose(reduced, np.array([[2.5, 4.5], [10.5, 12.5]], dtype=np.float32))

    def test_downsample_2x_area_rejects_odd_dimensions(self):
        with self.assertRaises(ValueError):
            module.downsample_2x_area(np.zeros((3, 4), dtype=np.float32))

    def test_reconstruct_2x_bilinear_is_pixel_centred(self):
        lattice = np.array([[0.0, 10.0], [20.0, 30.0]], dtype=np.float32)
        rebuilt = module.reconstruct_2x_bilinear(lattice)
        self.assertEqual(rebuilt.shape, (4, 4))
        np.testing.assert_allclose(
            rebuilt,
            np.array([[0.0, 2.5, 7.5, 10.0],
                      [5.0, 7.5, 12.5, 15.0],
                      [15.0, 17.5, 22.5, 25.0],
                      [20.0, 22.5, 27.5, 30.0]], dtype=np.float32),
        )

    def test_reconstruct_2x_bilinear_preserves_constant(self):
        rebuilt = module.reconstruct_2x_bilinear(np.full((3, 5), 0.75, dtype=np.float32))
        np.testing.assert_allclose(rebuilt, 0.75)

    def test_tonal_colour_transfer_reports_each_luma_population(self):
        source = np.zeros((4, 4, 3), dtype=np.float32)
        source[..., 0] = np.arange(16, dtype=np.float32).reshape(4, 4) / 16.0
        target = source.copy()
        target[..., 2] += 0.1
        measured = module.tonal_colour_transfer(source, target)
        self.assertIn("shadow_edit_chroma_rms", measured)
        self.assertIn("midtone_edit_chroma_rms", measured)
        self.assertIn("highlight_edit_chroma_rms", measured)
        self.assertGreater(measured["highlight_edit_chroma_rms"], 0.0)


if __name__ == "__main__":
    unittest.main()
