"""Explicit, bounded real-NR host runner. Does not discover or mutate any game installation."""
from pathlib import Path
import argparse,subprocess,hashlib,json,datetime,shutil
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--forwarder',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--driver',type=Path,required=True)
p.add_argument('--mode',choices=['shared_batch','shared_same_input','independent_batch','anchored_shared','anchored_reset_aux','shared_chain_fixed','shared_same_input_fixed','independent_fixed','feedback_live_shared','feedback_live_independent','feedback_frozen_shared','feedback_frozen_independent','feedback_live_split_params'],required=True);p.add_argument('--style',type=int,choices=[0,2],default=2);p.add_argument('--passes',type=int,choices=[2,3],default=2);p.add_argument('--run',required=True);p.add_argument('--frozen-input',type=Path);p.add_argument('--reset-policy',choices=['initial','every_call','first_pass','second_pass']);p.add_argument('--anchor-input',choices=['constant','step48']);p.add_argument('--anchor-allocation',choices=['parity','active']);p.add_argument('--reset-boundary',action='store_true');p.add_argument('--observe-runtime-state',action='store_true');p.add_argument('--observer-phase',choices=['controls','state','bindings','resources'],default='controls');p.add_argument('--counter-bank',action='store_true',help='Owned-host diagnostic intervention: write only the preblock CPU counter; never a game option');p.add_argument('--counter-policy',choices=['per_pass','second_cold_start'],default='per_pass');a=p.parse_args()
if a.counter_policy!='per_pass' and not a.counter_bank:p.error('Counter policy requires explicit --counter-bank')
if a.counter_bank and not (a.observe_runtime_state and a.observer_phase=='bindings' and a.reset_boundary and a.reset_policy=='initial' and a.mode=='feedback_live_shared' and a.style==2 and a.passes==2):p.error('Counter intervention requires the fixed two-pass shared style2 boundary host, initial Reset, bindings phase')
if a.observer_phase!='controls' and not a.observe_runtime_state:p.error('Observer phase requires --observe-runtime-state')
if a.observe_runtime_state and not a.reset_boundary:p.error('Runtime observer only supports the explicit boundary host')
if a.reset_boundary and not a.reset_policy:p.error('Boundary host requires an explicit Reset policy')
if a.reset_policy in ['first_pass','second_pass'] and not a.reset_boundary:p.error('Pass-specific Reset requires the boundary host')
isolation=a.mode.startswith('feedback_');fixed=a.mode.endswith('_fixed') or isolation
anchor=a.anchor_input is not None;extra_input=isolation or anchor
if a.anchor_allocation and not anchor:p.error('Allocation control requires the explicit anchor host')
if extra_input!=bool(a.frozen_input):p.error('Isolation/anchor modes require an explicit alternate input; other modes do not accept it')
if anchor and (a.passes!=3 or a.mode not in ['shared_batch','independent_batch','anchored_shared','anchored_reset_aux']):p.error('Anchor host requires a supported three-pass mode')
if a.mode=='anchored_reset_aux' and not anchor:p.error('Auxiliary Reset is only available in the explicit anchor host')
if a.reset_policy and not isolation:p.error('Reset policy is a diagnostic isolation-host option only')
if (a.mode=='anchored_shared' and a.passes!=3) or ((a.mode=='shared_same_input' or fixed) and a.passes!=2):p.error('Mode does not match compiled host architecture')
if not a.run.replace('_','').isalnum():p.error('simple run name required')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
expected={'forwarder':'D187321BCD70F3C3A0B1BB18E4178CAC0C6A8AAA585C05352EDD2DA8A22D722E','runtime':'CCAC112995922D8BD2C5F2D0DCB7A6756B7806D3D868692ACB9AF64D4AEF7414'}
for k,v in expected.items():assert sha(getattr(a,k))==v,k+' identity differs; re-audit before running'
if extra_input:
 import numpy as np
 assert a.frozen_input.stat().st_size==640*384*8 and np.isfinite(np.fromfile(a.frozen_input,dtype='<f2')).all(),'Invalid frozen input'
r=a.build.resolve();out=r/'tests'/a.run;assert not out.exists();out.mkdir(parents=True)
private=r/'private-runtime';private.mkdir(exist_ok=True)
for k,name in [('forwarder','nvngx.dll_dlssnr.dll'),('runtime','nvngx_dlssnr.dll')]:
 dest=private/name
 if not dest.exists():shutil.copy2(getattr(a,k),dest)
 assert sha(dest)==expected[k]
data=r/'runtime-data';data.mkdir(exist_ok=True);exe=r/'tests'/('probe-shared-feedback-runtime.exe' if a.passes==2 else 'probe-anchored-history-runtime.exe')
if fixed:exe=r/'tests/probe-fixed-input-runtime.exe'
if isolation:exe=r/'tests/probe-feedback-isolation-runtime.exe'
if a.reset_policy:exe=r/'tests/probe-reset-isolation-runtime.exe'
if getattr(a,'reset_boundary',False):exe=r/'tests/probe-reset-boundary-runtime.exe'
if anchor:exe=r/'tests/probe-temporal-anchor-runtime.exe'
if a.anchor_allocation:exe=r/'tests/probe-temporal-anchor-allocation-runtime.exe'
m={'schema':'d18-feedback-probe-v1','start':datetime.datetime.now().astimezone().isoformat(),'mode':a.mode,'style':a.style,'frames_evaluated':64,'warmup_frames':48,'saved_frames':list(range(48,64)),'passes':2,'width':640,'height':384,'ratio':1,'static_input':True,'motion_zero_all_passes':True,'depth':.5,'runtime_sha256':expected,'driver_core':str(a.driver),'driver_core_sha256':sha(a.driver),'executable_sha256':sha(exe),'readback_perturbs_timing':True,'cross_frame_wait':True,'game_loaded':False,'complete':False}
m['passes']=a.passes
m['fixed_input_resource']=fixed
if isolation:m['frozen_input_sha256']=sha(a.frozen_input);m['frozen_input_used']=a.mode.startswith('feedback_frozen_');m['split_parameter_blocks']=a.mode=='feedback_live_split_params'
if a.reset_policy:m['reset_policy']=a.reset_policy
if a.reset_boundary:m.update(reset_boundary=True,reset_observation='requested only; runtime implicit resets unobserved')
if anchor:
 m.update(frozen_input_sha256=sha(a.frozen_input),input_schedule=a.anchor_input,static_input=a.anchor_input=='constant',auxiliary_reset=a.mode=='anchored_reset_aux',active_feature_count=3 if a.mode=='independent_batch' else 1 if a.mode=='shared_batch' else 2,created_feature_count=3)
 if a.anchor_allocation:m['allocation_policy']=a.anchor_allocation;m['created_feature_count']=m['active_feature_count'] if a.anchor_allocation=='active' else 3
try:
 with (out/'probe.log').open('w',encoding='utf-8') as log:
  args=[str(exe),str(private/'nvngx.dll_dlssnr.dll'),str(private/'nvngx_dlssnr.dll'),str(data),str(a.driver.resolve()),a.mode,str(out),str(a.style)]
  if extra_input:args.append(str(a.frozen_input.resolve()))
  if a.reset_policy:args.append(a.reset_policy)
  if anchor:args.append(a.anchor_input)
  if a.anchor_allocation:args.append(a.anchor_allocation)
  if a.observe_runtime_state:
   observer=r/'tests'/('nr-counter-bank-debugger.exe' if a.counter_bank else 'nr-state-debugger.exe');m.update(runtime_state_observer=True,observer_phase=a.observer_phase,observer_sha256=sha(observer),runtime_data_intervention=a.counter_bank,counter_policy=a.counter_policy if a.counter_bank else None,observation_scope='owned host only; preblock CPU counter intervention per explicit counter_policy; no code patches; scheduling perturbed' if a.counter_bank else 'owned host main-thread execution breakpoints; no code/data patches; scheduling perturbed')
   args=[str(observer),str(out/'runtime-state.jsonl'),str(private/'nvngx_dlssnr.dll'),('counter-cold-start' if a.counter_bank and a.counter_policy=='second_cold_start' else a.observer_phase)]+args
  m['command']=args
  q=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,cwd=out,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW)
 m['exit_code']=q.returncode;m['files']={f.name:sha(f) for f in out.glob('*.rgba16f')};m['complete']=q.returncode==0 and len(m['files'])==1+a.passes*16+int(extra_input)
 if a.observe_runtime_state:
  state=out/'runtime-state.jsonl'
  m['observer_trace_sha256']=sha(state) if state.exists() else None
  assert not state.exists() or state.stat().st_size<=1024*1024,'Observer trace exceeds 1 MiB bound'
  records=[]
  try:records=[json.loads(line) for line in state.read_text().splitlines()] if state.exists() else []
  except (ValueError,UnicodeError) as error:m['observer_parse_error']=str(error)
  m['observer_complete']=bool(records and records[-1].get('event')=='exit' and records[-1].get('code')==0 and all(records[-1].get(k)==128 for k in ['control_calls','gate_calls','after_calls']))
  m['complete']=m['complete'] and m['observer_complete']
 if extra_input:m['complete']=m['complete'] and m['files'].get('frozen_0.rgba16f')==m['frozen_input_sha256']
finally:
 m['end']=datetime.datetime.now().astimezone().isoformat();(out/'manifest.json').write_text(json.dumps(m,indent=2),encoding='utf-8')
print(json.dumps({k:m[k] for k in ['mode','style','complete','start','end']}));print('\n'.join((out/'probe.log').read_text().splitlines()[-3:]))
raise SystemExit(0 if m['complete'] else 1)
