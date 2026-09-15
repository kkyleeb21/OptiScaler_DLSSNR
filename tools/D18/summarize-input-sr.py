"""Bounded summary for SR-adapter events; API success is not image-quality acceptance."""
import argparse,json,hashlib,collections
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('log');p.add_argument('--ui-log');p.add_argument('--execution-log');p.add_argument('--output',required=True);a=p.parse_args()
f=Path(a.log);data=f.read_bytes()
if len(data)>1024*1024:raise SystemExit('SR event log exceeds budget')
events=[json.loads(l) for l in data.decode('utf-8-sig').splitlines()]
if len(events)>1016:raise SystemExit('SR event count exceeds budget')
if any(e.get('event') not in ('wildlands_sr','wildlands_sr_gpu','sr_preset','sr_post_replay','sr_quality','sr_resize') for e in events):raise SystemExit('unrecognized adapter event')
preset=[e for e in events if e['event']=='sr_preset']
if len(preset)>64 or any(not all(k in e for k in ('hint','submitted','drained','tick','stage')) for e in preset):raise SystemExit('Invalid preset events')
quality=[e for e in events if e['event']=='sr_quality']
if len(quality)>64 or any(not all(k in e for k in ('mode','standard','ratio','ngx_quality','preset_key','hint','tick','stage')) for e in quality):raise SystemExit('Invalid quality events')
resize=[e for e in events if e['event']=='sr_resize']
if len(resize)>64 or any(not all(k in e for k in ('width','height','output_width','output_height','submitted','drained','post_drained','tick','stage')) for e in resize):raise SystemExit('Invalid resize events')
cpu=[e for e in events if e['event']=='wildlands_sr'];gpu=[e for e in events if e['event']=='wildlands_sr_gpu']
out=dict(schema='d18-sr-adapter-summary-v1',adapter='wildlands-dx11-preview',log_sha256=hashlib.sha256(data).hexdigest(),
         event_counts=dict(collections.Counter(e['stage'] for e in events)),last_event=events[-1] if events else None,
         recorded_composed_frames=max((e['composed'] for e in cpu),default=0),
         gpu_completed_frames=max((e['completed'] for e in gpu),default=0),
         gpu_last_event=gpu[-1] if gpu else None,
         errors=[e for e in events if e.get('code',0)!=0],in_game_visual_acceptance='unverified',nr_handoff='not_implemented')
out['preset_switches']=dict(records=preset,at_record_limit=len(preset)>=64,
    unsafe_preparations=[e for e in preset if e['stage']=='prepared_after_drain' and e['submitted']!=e['drained']],
    boundary='Created hint is not proof of the runtime internal model. Missing events are not successful switches.')
out['diagnostic_mode']='evaluate_only_no_writeback' if any(e['stage']=='diagnostic_evaluate_only_armed' for e in cpu) else 'normal_or_unspecified'
if any(e['stage']=='diagnostic_stage_panel_armed' for e in cpu):out['diagnostic_mode']='stage_panel'
out['gpu_stage_completion']={stage:max((e.get(stage,0) for e in gpu),default=0) for stage in ('prepare_done','evaluate_done','compose_done')}
out['resolution_contracts']=[dict(input=[w,h],output=[ow,oh]) for w,h,ow,oh in sorted({(e['width'],e['height'],e['output_width'],e['output_height']) for e in cpu if e.get('output_width') and e.get('output_height')})]
out['quality_parameters']=dict(records=[e for e in events if e['event']=='sr_quality'], boundary='Requested NGX creation parameters only; actual model and gameplay quality are not inferred')
out['resize_transitions']=dict(records=resize,at_record_limit=len(resize)>=64,unsafe_releases=[e for e in resize if e['stage']=='released_after_drain' and (e['submitted']!=e['drained'] or not e['post_drained'])],boundary='Resource retirement only; does not prove the engine accepted a UI quality request')
out['resolution_contract_boundary']='Reported allocation contract; GPU completion and pixel/visual validation are separate evidence.'
post=[e for e in events if e['event']=='sr_post_replay']
if len(post)>96:raise SystemExit('Post replay event count exceeds budget')
out['post_replay']=dict(records=post,at_record_limit=len(post)>=96,
    native_layer_merges=max((e.get('native_layers',0) for e in post),default=0),
     native_handoff_submitted=max((e.get('handoffs',0) for e in post),default=0),
    native_handoff_gpu_completed=max((e.get('handoff_gpu_completed',0) for e in post),default=0),
     rejections=[e for e in post if e['stage'] not in ('private_target_created','seeded','replayed','progress','private_clear','private_compute','compute_enter','compute_capture','compute_prepare_begin','compute_prepare_end','command_list_disjoint','native_handoff','native_same_size_handoff','alternate_blur','alternate_composite','native_layer_merged')],
    observed_clean_frames=max((e['accepted_frames'] for e in post if 'accepted_frames' in e),default=None),
    rejected_frames=max((e['rejected_frames'] for e in post if 'rejected_frames' in e),default=None),
    counter_boundary='Accepted means CPU frame closure within observed hooks only, not GPU-complete or full mutation coverage. Null counters mean legacy/unavailable; rejection records are deduplicated.',
    boundary='Private replay and native colour handoff submissions/fence completion, not visual quality acceptance or complete game mutation coverage.')
out['colour_handoff_observation']=dict(
    routes=sorted({e['stage'] for e in post if e['stage'] in ('native_handoff','native_same_size_handoff')}),
    gpu_completed=out['post_replay']['native_handoff_gpu_completed'],
    boundary='A legacy evaluate_only label describes the initial private SR evaluation. Native handoff records separately establish colour submission; do not interpret that label as proof of no visible handoff. Missing route events can reflect the logging budget.')
if a.ui_log:
 ui_data=Path(a.ui_log).read_bytes()
 if len(ui_data)>256*1024:raise SystemExit('UI event log exceeds budget')
 ui=[json.loads(l) for l in ui_data.decode('utf-8-sig').splitlines()]
 if len(ui)>144 or any(e.get('event')!='dx11_ui' for e in ui):raise SystemExit('Invalid UI event stream')
 out['ui']=dict(log_sha256=hashlib.sha256(ui_data).hexdigest(),records=len(ui),visibility_transitions=sum(x['visible']!=y['visible'] for x,y in zip(ui,ui[1:])),visible_geometry_records=sum(x['visible'] and x['vertices']>0 for x in ui),errors=[x for x in ui if x['code']],last_event=ui[-1] if ui else None,causality='timestamps and CPU submissions only; not proof of GPU fault origin')
 # The UI "frame" field counts Record calls, not game frames or GPU draws.
 out['ui']['counter_meaning']='UI diagnostic visits; not game frames or draw calls'
 out['ui']['visit_rates']=[dict(tick=y['tick'],visible=y['visible'],visits_per_second=round(1000*(y['frame']-x['frame'])/(y['tick']-x['tick']),3)) for x,y in zip(ui,ui[1:]) if y['tick']-x['tick']>=900 and y['frame']>=x['frame']]
if a.execution_log:
 raw=Path(a.execution_log).read_bytes()
 if len(raw)>1024*1024:raise SystemExit('Execution trace exceeds budget')
 trace=[json.loads(l) for l in raw.decode('utf-8-sig').splitlines()]
 if len(trace)>4096 or any(e.get('event')!='d18_execution' for e in trace):raise SystemExit('Invalid execution trace')
 out['execution']=dict(log_sha256=hashlib.sha256(raw).hexdigest(),records=len(trace),at_record_limit=len(trace)>=4096,stage_counts=dict(collections.Counter(e['stage'] for e in trace)),sr_targets_during_execute=sum(e['stage']=='sr_target_begin' and e['execute_depth']>0 for e in trace),last_by_thread={str(t):next(e for e in reversed(trace) if e['thread']==t) for t in sorted(set(e['thread'] for e in trace))},errors=[e for e in trace if e['code']!=0],boundary='Bounded CPU checkpoints only; absent stages can reflect sampling limits rather than a hang')
 out['execution']['toggle_timeline']=[e for e in trace if e['stage'] in ('ui_open_requested','ui_close_requested','sr_enable_requested','sr_disable_requested')]
 # Coverage snapshots are bounded CPU observations, not a proof of full-screen geometry.
 names={1:'missing_present_owner',2:'unsupported_draw_command',4:'topology',8:'geometry_shader',16:'hull_shader',32:'domain_shader',64:'fill_mode',128:'scissor',256:'sample_mask',512:'blend_or_alpha_coverage',1024:'rgb_write_mask',2048:'depth_or_stencil',4096:'stream_output',8192:'geometry_capture_unavailable'}
 def pair(v):return [v>>32,v&0xffffffff]
 def signed(v):return v if v<0x80000000 else v-0x100000000
 snapshots=[];current=None
 for e in trace:
  stage=e['stage']
  if stage=='sr_coverage_reason':
   current=dict(tick=e['tick'],thread=e['thread'],context=e['context'],reason_mask=e['a'],reasons=[name for bit,name in names.items() if e['a']&bit],dimensions=pair(e['b']),raw=[]);snapshots.append(current)
  if current is None or not stage.startswith('sr_coverage_'):continue
  current['raw'].append(e)
  if stage=='sr_coverage_draw':current['draw']=dict(count=e['a'],first=e['b'])
  elif stage=='sr_coverage_raster':current['raster']=dict(zip(('topology','fill','scissor_enabled','rect_count'),pair(e['a'])+pair(e['b'])))
  elif stage=='sr_coverage_scissor':current['scissor_rect']=[signed(v) for v in pair(e['a'])+pair(e['b'])]
  elif stage=='sr_coverage_blend':current['blend']=dict(zip(('alpha_to_coverage','blend_enabled','sample_mask','write_mask'),pair(e['a'])+pair(e['b'])))
  elif stage=='sr_coverage_depth':current['depth']=dict(zip(('enabled','stencil_enabled','comparison'),pair(e['a'])+pair(e['b'])[:1]))
  elif stage=='sr_coverage_stencil_func':current['stencil_comparison']=dict(front=e['a'],back=e['b'])
 out['execution']['coverage_snapshots']=snapshots
 out['execution']['replay_commands']=[dict(tick=e['tick'],kind={1:'Draw',2:'DrawIndexed',3:'DrawInstanced',4:'DrawIndexedInstanced'}.get(e['a'],'unsupported'),count=pair(e['b'])[0],first=pair(e['b'])[1]) for e in trace if e['stage']=='sr_replay_command']
 out['execution']['replay_parameters']=[e for e in trace if e['stage'] in ('sr_replay_instances','sr_replay_base_vertex')]
 stages={0:'observe',1:'context_state',2:'prepare',3:'create_feature',4:'evaluate_only',5:'original_writeback',6:'full_sr',7:'copy_original_only',8:'offscreen_replay',9:'context_create_only',10:'context_swap_only'}
 out['execution']['stage_timeline']=[dict(tick=e['tick'],action=e['stage'],stage=stages.get(e['a'],'unknown'),stage_id=e['a'],value=e['b']) for e in trace if e['stage'] in ('sr_diagnostic_mode_requested','sr_diagnostic_mode_applied','sr_diagnostic_cpu_completed','sr_diagnostic_gpu_completed')]
 out['execution']['initialization_checkpoints']=[e for e in trace if e['stage'].startswith(('sr_pipeline_create_','sr_ngx_'))]
 out['execution']['context_transactions']=[e for e in trace if e['stage'].startswith('context_transaction_')]
 out['execution']['context_state_checkpoints']=[e for e in trace if e['stage'].startswith(('context_create_check_','context_swap_','context_clear_scope_','context_explicit_reset_scope_','context_manual_scope_'))]
 out['execution']['transaction_busy_count']=sum(e['stage']=='context_transaction_busy' for e in trace)
 out['execution']['transaction_boundary']='D18-only nonblocking serialization; busy is a skipped attempt, not proof of a GPU fault; sampling may truncate pairs'
Path(a.output).write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
