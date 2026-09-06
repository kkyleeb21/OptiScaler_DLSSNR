"""Numerical reference invariants for the bounded 50% reconstruction A/B.

These test the equations, not GPU execution or perceptual quality.
"""
import unittest
import numpy as np


def cubic(x, b, c):
    x = abs(x)
    if x < 1:
        return ((12-9*b-6*c)*x**3 + (-18+12*b+6*c)*x*x + 6-2*b)/6
    if x < 2:
        return ((-b-6*c)*x**3 + (6*b+30*c)*x*x + (-12*b-48*c)*x + 8*b+24*c)/6
    return 0.


class ReconstructionMathTests(unittest.TestCase):
    def test_area_footprint_is_not_single_sample(self):
        pixels = np.array([[1., 3.], [5., 7.]])
        self.assertEqual(float(pixels.mean()), 4.)
        self.assertNotEqual(float(pixels.mean()), pixels[0, 0])

    def test_gain_first_is_distinct_and_identity_safe(self):
        source = np.array([0.01, 0.2])
        target = np.array([0.02, 0.2])
        floor = 1/512
        first = np.mean(np.clip((target+floor)/(source+floor), .5, 2))
        second = (target.mean()+floor)/(source.mean()+floor)
        self.assertGreater(abs(first-second), .2)
        np.testing.assert_array_equal((source+floor)/(source+floor), [1, 1])
        self.assertTrue(.5 <= first <= 2)

    def test_cubic_kernels_are_normalized_and_distinct(self):
        for b, c in ((1/3, 1/3), (0, .5)):
            for phase in (0., .25, .5, .75):
                self.assertAlmostEqual(sum(cubic(i-phase, b, c) for i in range(-2, 4)), 1.)
            self.assertEqual(cubic(2., b, c), 0.)
        self.assertAlmostEqual(cubic(0., 0., .5), 1.)
        self.assertAlmostEqual(cubic(0., 1/3, 1/3), 8/9)


if __name__ == "__main__":
    unittest.main()
