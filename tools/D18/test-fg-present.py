import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('fg', Path(__file__).with_name('summarize-fg-present.py'))
fg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fg)


def row(**updates):
    fields = dict(api='dx11', query=0, status=0, presented=360, app_presents=120,
                  warmup=0, enabled=1, active=1, paused=0, requested=2, present_hr=0)
    fields.update(updates)
    return '[I] d18_fg_present ' + ' '.join(f'{k}={v}' for k, v in fields.items())


class TestSummary(unittest.TestCase):
    def test_missing_is_not_zero(self):
        self.assertEqual(fg.summarize('other log')['coverage'], 'missing')

    def test_warmup_failed_and_modes(self):
        result = fg.summarize('\n'.join([row(warmup=1, presented=10000), row(query=1),
            row(present_hr=2289696773), row(), row(enabled=0, active=0, presented=120)]))
        self.assertEqual(result['samples'], 5)
        self.assertEqual(sorted(g['presented_min'] for g in result['groups'].values()), [120, 360])
        self.assertTrue(all('runtime_presented_per_app_present' not in g for g in result['groups'].values()))

    def test_malformed_does_not_count(self):
        self.assertEqual(fg.summarize('[I] d18_fg_present api=dx11')['coverage'], 'missing')


if __name__ == '__main__':
    unittest.main()
