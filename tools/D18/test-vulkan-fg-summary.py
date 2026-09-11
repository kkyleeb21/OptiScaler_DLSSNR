"""Do not promote incomplete Vulkan input observations into successful FG proof."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('fg_summary', Path(__file__).with_name('summarize-fg-present.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class VulkanInputSummary(unittest.TestCase):
    def test_missing_is_not_success(self):
        result = module.summarize('')
        self.assertEqual(result['coverage'], 'missing')
        self.assertEqual(result['vulkan_input_contract']['sampled_guide_coverage'], 'missing')
        self.assertEqual(result['vulkan_input_contract']['resource_reuse_safe'], 'unknown')

    def test_partial_contract_preserves_unknowns(self):
        log = ('event=fg_sampled_input record=1 handoffs=1 submitted=0 same_queue=0 invalidated=1 failed=0 '
               'descriptor_writes=2 sr_barriers=1 later_barriers=0 other_commands=0 result=0\n'
               'event=fg_sampled_guide record=1 role=depth alive=0 metadata=1 sampled_layout=4 present_layout=4 '
               'layout_known=1 ownership_conflict=0 aspect=2 format=129 usage=36 width=1920 height=1080\n')
        result = module.summarize(log)
        contract = result['vulkan_input_contract']
        self.assertEqual(contract['malformed'], 0)
        self.assertEqual(contract['sampled_input_observations'][0]['submitted'], 0)
        self.assertEqual(contract['sampled_guide_observations'][0]['alive'], 0)
        self.assertEqual(contract['resource_reuse_safe'], 'unknown')
        self.assertEqual(result['coverage'], 'missing')

    def test_owned_runtime_samples_preserve_failures_and_missing(self):
        log = ('event=fg_vulkan_frame requested=3 actual=3 status=0 query=0 present=0 depth_snapshot=1\n'
               'event=fg_vulkan_frame requested=3 actual=3 status=4 query=0 present=0 depth_snapshot=1\n'
               'event=fg_vulkan_frame requested=3 actual=3\n'
               'event=fg_vulkan_gate reason=Motion-vector read contract unavailable\n')
        result = module.summarize(log)['owned_vulkan_fg']
        self.assertEqual(result['generated_samples'], 1)
        self.assertEqual(result['samples'], 2)
        self.assertEqual(result['malformed'], 1)
        self.assertEqual(result['gates']['Motion-vector read contract unavailable'], 1)
        self.assertEqual(module.summarize('')['owned_vulkan_fg']['coverage'], 'missing')

    def test_truncated_record_is_not_zero(self):
        result = module.summarize_vulkan_contract('event=fg_sampled_guide record=1 role=depth alive=1')
        self.assertEqual(result['malformed'], 1)
        self.assertEqual(result['sampled_guide_observations'], [])

    def test_depth_evidence_does_not_imply_generation(self):
        log = ('event=fg_vulkan_input_gate token=1 tagged=1 depth=0 invalid=0 submissions=1 same_queue=1\n'
               'event=fg_vulkan_depth_barrier frame=9 same_cmd=0 old_layout=3 new_layout=3 aspect=6 source_family=4294967295 destination_family=4294967295 invalid=0 submissions=1\n'
               'event=fg_vulkan_depth_input frame=9\n')
        result = module.summarize(log)['owned_vulkan_fg']
        self.assertEqual(result['generated_samples'], 0)
        self.assertEqual(result['coverage'], 'missing')
        self.assertEqual(result['input_evidence']['fg_vulkan_input_gate'][0]['depth'], 0)
        self.assertEqual(result['input_evidence']['fg_vulkan_depth_barrier'][0]['same_cmd'], 0)
        self.assertEqual(result['input_evidence_malformed'], 1)

    def test_submission_order_coverage_is_explicit(self):
        log = 'event=fg_vulkan_input_gate token=1 tagged=1 depth=1 invalid=0 submissions=1 same_queue=1 depth_submissions=1 depth_before_sr=0 depth_same_cmd=0 order_invalid=1'
        result = module.summarize(log)['owned_vulkan_fg']
        self.assertEqual(result['input_evidence']['fg_vulkan_input_gate'][0]['order_invalid'], 1)
        self.assertEqual(result['generated_samples'], 0)

    def test_minimal_inputs_are_not_hudless_quality_proof(self):
        log = 'event=fg_vulkan_frame requested=2 actual=2 status=0 query=0 present=0 depth_snapshot=1 colour_source=swapchain hudless=0 volatile_inputs=2'
        result = module.summarize(log)['owned_vulkan_fg']
        self.assertEqual(result['generated_samples'], 1)
        self.assertEqual(result['samples_raw'][0]['hudless'], 0)
        self.assertEqual(result['samples_raw'][0]['volatile_inputs'], 2)
        self.assertEqual(result['samples_raw'][0]['colour_source'], 'swapchain')

    def test_pacing_retains_focus_and_wait_without_generation_claim(self):
        log = 'event=fg_vulkan_pacing frames=26 foreground_frames=12 window_ms=1003 control_us=43 present_us=860000 readback_us=72 max_present_us=34000 enabled=0 active=0 ready=0 result=0 reflex_policy=unmodified'
        result = module.summarize(log)['owned_vulkan_fg']
        row = result['input_evidence']['fg_vulkan_pacing'][0]
        self.assertEqual(row['foreground_frames'], 12)
        self.assertEqual(row['enabled'], 0)
        self.assertEqual(row['max_present_us'], 34000)
        self.assertEqual(result['generated_samples'], 0)

    def test_reflex_lifecycle_is_not_generation_proof(self):
        log = 'event=fg_vulkan_reflex enabled=1 result=0\nevent=fg_vulkan_reflex enabled=0 result=0'
        result = module.summarize(log)['owned_vulkan_fg']
        self.assertEqual([r['enabled'] for r in result['input_evidence']['fg_vulkan_reflex']], [1, 0])
        self.assertEqual(result['generated_samples'], 0)

    def test_fault_between_periodic_samples_is_retained(self):
        log = 'event=fg_vulkan_fault requested=2 actual=0 status=2 query=0 present=0 depth_snapshot=1'
        result = module.summarize(log)['owned_vulkan_fg']
        self.assertEqual(result['input_evidence']['fg_vulkan_fault'][0]['status'], 2)
        self.assertEqual(result['coverage'], 'missing')
        self.assertEqual(result['generated_samples'], 0)

    def test_early_depth_preserves_input_evidence_only(self):
        log = 'event=fg_vulkan_depth_input frame=9 generation=31 recorded_layout=0 cached_layout=0 observed_layout=0 observed_known=0 descriptor_layout=0 aspect=2 early_depth=1'
        result = module.summarize(log)['owned_vulkan_fg']
        self.assertEqual(result['input_evidence']['fg_vulkan_depth_input'][0]['early_depth'], 1)
        self.assertEqual(result['generated_samples'], 0)


if __name__ == '__main__':
    unittest.main()
