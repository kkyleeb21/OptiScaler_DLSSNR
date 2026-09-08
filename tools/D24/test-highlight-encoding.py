"""Offline curve checks; does not validate model behaviour or gameplay."""
import math

def encode(x, hybrid):
    if hybrid:
        if x <= .75:
            return x
        e = (x - .75) / .25
        return .75 + .25 * e / math.sqrt(1 + e * e)
    return x / math.sqrt(1 + x * x)

def inverse(y, hybrid):
    if hybrid:
        if y <= .75:
            return y
        u = (y - .75) / .25
        return .75 + .25 * u / math.sqrt(1 - u * u)
    return y / math.sqrt(1 - y * y)

for hybrid in (False, True):
    points = [0, 1e-6, .01, .25, .749999, .75, .750001, 1, 2, 4, 16, 64, 100]
    values = [encode(x, hybrid) for x in points]
    assert all(math.isfinite(y) and 0 <= y < 1 for y in values)
    assert all(a < b for a, b in zip(values, values[1:]))
    for x, y in zip(points, values):
        assert math.isclose(inverse(y, hybrid), x, rel_tol=1e-8, abs_tol=1e-10)
    for rgb in ((100, 20, 1), (4, .1, .05), (.6, .2, .1)):
        scale = encode(max(rgb), hybrid) / max(rgb)
        out = [v * scale for v in rgb]
        assert math.isclose(out[0] / out[1], rgb[0] / rgb[1], rel_tol=1e-12)
assert encode(.5, True) == .5
print('PASS: monotonicity, finite range, mathematical round trip and RGB ratios; GPU precision/model effects not covered')
