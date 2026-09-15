from pathlib import Path
r=Path('E:/DLSSNR/builds/D18_Wildlands_StagePanelV9_20260913')
p=r/'sr-host.cpp';s=p.read_text()
s=s.replace('if(argc!=7)return 2;', 'if(argc!=7&&argc!=8)return 2;')
s=s.replace('const bool transition=', 'const bool cycle=wcscmp(argv[3],L"stage-cycle")==0;\n const auto stageSetter=argc==8?reinterpret_cast<void(*)(unsigned)>(reinterpret_cast<unsigned char*>(module)+wcstoull(argv[7],nullptr,16)):nullptr;\n const bool transition=',1)
s=s.replace('const bool ui=wcscmp', 'const bool ui=cycle||wcscmp',1)
s=s.replace(' for(unsigned frame=0;frame<(stress?1800u:90u);++frame){', ''' for(unsigned frame=0;frame<(stress?1800u:90u);++frame){
  if(stageSetter){
   if(cycle&&frame%200==0){unsigned modes[]={0,1,2,3,4,5,6,0,4};stageSetter(modes[frame/200]);}
   else if(!cycle&&frame==0)stageSetter(static_cast<unsigned>(wcstoul(argv[3]+5,nullptr,10)));
  }''')
p.write_text(s)
# Build a bounded runner from the existing actual-DLL fixture without retaining
# SR success assertions in observe/state-only diagnostic modes.
p=r/'test-sr.py';s=p.read_text()
s=s.replace("out=r/'sr-test'", "out=r/'stage-test'")
s=s.replace("for mode in (sys.argv[1:] or ['stress']):", "stage_rva=int(re.search(r'\\?SetDiagnosticStage@WildlandsSr@DlssNr@@YAXI@Z\\s+([0-9a-fA-F]+)',linkmap)[1],16)-base\nfor mode in (sys.argv[1:] or ['stage0','stage1','stage2','stage3','stage4','stage5','stage6','stage-cycle']):")
s=s.replace("(d/'D18WildlandsSR.enabled').write_text('private fixture')", "(d/'D18WildlandsSR.enabled').write_text('private fixture')\n (d/'D18SrDiagnostics.panel.enabled').write_text('stage host')")
s=s.replace("format(sr_rva,'x')]", "format(sr_rva,'x'),format(stage_rva,'x')]")
start=s.index(" if not isolation and mode in")
end=s.index(" debug=(d/'debug.txt').read_text();",start)
s=s[:start]+''' trace=[json.loads(l) for l in (d/'D18ExecutionTrace.jsonl').read_text().splitlines()]
 assert not any(e['code'] for e in events+trace)
 applied=[e['a'] for e in trace if e['stage']=='sr_diagnostic_mode_applied']
 cpu={i:max((e['b'] for e in trace if e['stage']=='sr_diagnostic_cpu_completed' and e['a']==i),default=0) for i in range(7)}
 gpu={i:max((e['b'] for e in trace if e['stage']=='sr_diagnostic_gpu_completed' and e['a']==i),default=0) for i in range(7)}
 row.update(applied=applied,cpu=cpu,gpu=gpu)
 if mode=='stage-cycle':
  assert applied==[1,2,3,4,5,6,0,4],applied
  assert all(cpu[i]>0 for i in range(7)),cpu
  assert all(gpu[i]>0 for i in range(2,7)),gpu
  assert 'menu_toggles=6 present_state_checks=1800' in run.stdout
  ui=[json.loads(l) for l in (d/'D18UiDiagnostics.jsonl').read_text().splitlines()]
  assert any(e['visible'] and e['vertices']>1000 for e in ui) and not any(e['code'] for e in ui)
 else:
  stage=int(mode[5:]);assert cpu[stage]>0,cpu
  if stage:assert applied==[stage]
  if stage>=2:assert gpu[stage]>0,gpu
  if stage in (0,1,2,5):assert not any(e['stage']=='created' for e in events)
  if stage<4 or stage==5:assert not any(e.get('evaluate_done',0) for e in events)
  if stage!=6:assert not any(e.get('composed',0) or e.get('compose_done',0) or e.get('completed',0) for e in events)
  if stage==6:assert max(e.get('completed',0) for e in events)>=50
 print(json.dumps(dict(mode=mode,applied=applied,cpu=cpu,gpu=gpu)))
'''+s[end:]
(r/'test-stages.py').write_text(s)
