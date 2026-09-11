import json
import unittest
from unittest.mock import MagicMock
from summarize_runtime import summarize_dx11


class PerformanceSummary(unittest.TestCase):
    def test_coverage_and_optional_failure(self):
        path = MagicMock()
        path.resolve.return_value = 'native.log'
        failure = json.dumps({'event': 'allocation_failed', 'role': 'exposure_staging',
                             'required': False, 'recovery': 'manual_white_until_restart'})+'\n'
        path.read_text.return_value = failure
        result = summarize_dx11(path)
        self.assertEqual(result['performance']['coverage'], 'not_observed')
        self.assertFalse(result['allocation_failures'][0]['required'])
        path.read_text.return_value = failure + json.dumps({'event': 'dx11_performance', 'frames': 120,
                                     'model_waits': 120, 'compose_waits': 120,
                                     'total_us': 1000, 'pixel_capture': False})+'\n'
        result = summarize_dx11(path)
        self.assertEqual(result['performance']['coverage'], 'cpu_process_windows')
        self.assertEqual(result['performance']['windows'][0]['model_waits'], 120)
        self.assertEqual(result['gameplay_verdict'], 'not_measured')


if __name__ == '__main__':
    unittest.main()
