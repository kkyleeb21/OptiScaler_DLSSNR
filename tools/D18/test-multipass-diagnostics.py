import importlib.util
from pathlib import Path
p=Path(__file__).with_name('summarize-diagnostics.py')
spec=importlib.util.spec_from_file_location('diagnostics',p);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
flags=(4<<8)|(4<<12)|(4<<16)|(3<<20)|1|16
record=dict(sequence=1,monotonic_ms=1,frame=7,feature_generation=2,queue=1,command_list=2,fence_target=1,fence_completed=0,width=3840,height=2160,network_width=1920,network_height=1080,guide_width=1920,guide_height=1080,result=0xBAD00001,flags=flags,ratio=.5,exposure=1,white_point=1,mv_scale_x=1920,mv_scale_y=1080,type='mp_evaluate',reason='independent_history')
s=module.summarize({},[record]);e=s['multipass_events'][0]
assert (e['pass'],e['requested'],e['ready'],e['recorded'])==(4,4,4,3)
assert e['submitted'] and not e['completed'] and e['reset']
assert s['first_anomaly']==record and not s['high_resolution_contracts']
assert len(s['incomplete_fences'])==1
print('PASS multipass diagnostic counts, reset, API failure, submitted-vs-complete and high-resolution separation')
contract=dict(record,type='mp_contract',flags=10,result=2,reason='backend=dx12;format=10;global_api=0;requested=2;effective=2')
s=module.summarize({},[contract]);assert not s['multipass_events'] and s['first_anomaly'] is None
assert s['multipass_contracts'][0]['target_format']==10 and s['multipass_contracts'][0]['effective']==2
print('PASS DX12 contract format/effective count does not masquerade as a pass or NGX failure')
failure=dict(record,type='nr_failure_state',flags=26,result=0,reason='High-resolution NR output format/capabilities or size unsupported')
s=module.summarize({},[failure]);assert not s['multipass_events']
assert s['nr_failure_states'][0]['target_format']==26 and not s['nr_failure_states'][0]['device_lost']
s['header']=dict(game='fixture',backend='DX12',session=1,dropped=0)
assert s['first_anomaly']==failure
assert 'Retained NR failures' in module.markdown(s)
print('PASS late-enabled failure snapshot carries actual output format, reason and separate device-lost state')
