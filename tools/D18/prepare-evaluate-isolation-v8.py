from pathlib import Path
r=Path('E:/DLSSNR/builds/D18_Wildlands_EvaluateIsolationV8_20260913');src=r/'core-source/OptiScaler'
p=src/'dlssnr/WildlandsSrStatus.h';s=p.read_text();s=s.replace('inline std::atomic<unsigned> coverageReason','inline std::atomic<bool> evaluateOnly{false};\ninline std::atomic<unsigned long long> evaluations{0};\ninline std::atomic<unsigned> coverageReason');p.write_text(s)
p=src/'hooks/D18WildlandsSr.inl';s=p.read_text()
s=s.replace('if(enabled){Status::available=true;', 'Status::evaluateOnly=GetFileAttributesW((root/L"D18SrDiagnostics.evaluate-only").c_str())!=INVALID_FILE_ATTRIBUTES;\n if(enabled){Status::available=true;')
s=s.replace('Event(Status::enabled?"enabled_dlaa_preview":"armed_start_disabled");','Event(Status::enabled?"enabled_dlaa_preview":"armed_start_disabled");if(Status::evaluateOnly)Event("diagnostic_evaluate_only_armed");')
needle='c->ClearState();geometry.Apply(context.Get());'
assert needle in s
s=s.replace(needle,'''if(Status::evaluateOnly){
  ++Status::evaluations;reset=false;
  if(Status::evaluations==1||GetTickCount64()-lastLog>1000){lastLog=GetTickCount64();Event("diagnostic_evaluate_only_no_writeback");}
  // Leave issued=2. Poll only the submitted prepare/evaluate queries and
  // drain the slot without incrementing compose/completed-frame counters.
  return;
 }
 '''+needle)
p.write_text(s)
p=src/'menu/menu_common.cpp';s=p.read_text(encoding='utf-8-sig')
needle='if(W::enabled && !W::failed && W::coverageBlocked)srDetail='
s=s.replace(needle,'if(W::evaluateOnly && !W::failed)srDetail=D18Ui::Format("Diagnostic: SR evaluation only, no image writeback | evaluations %llu",W::evaluations.load());\n        '+needle)
s=s.replace('D18Ui::TextWrapped("Same-resolution DLSS preview.', 'if(W::evaluateOnly){D18Ui::TextWrapped("Diagnostic mode: SR evaluation only. Original image is displayed; SR output is deliberately not composed.");return;}\n        D18Ui::TextWrapped("Same-resolution DLSS preview.',1)
p.write_text(s,encoding='utf-8-sig')
# Reuse UI-transition host. A runner argument enables only the isolation marker;
# ordinary modes still exercise the production replay path.
p=r/'test-sr.py';s=p.read_text()
s=s.replace("results=[]", "isolation='--evaluate-only' in sys.argv\nif isolation:sys.argv.remove('--evaluate-only')\nresults=[]")
s=s.replace("(d/'D18WildlandsSR.enabled').write_text('private fixture')", "(d/'D18WildlandsSR.enabled').write_text('private fixture')\n marker=d/'D18SrDiagnostics.evaluate-only'\n if isolation:marker.write_text('isolated host')\n elif marker.exists():marker.unlink()")
s=s.replace("if mode in ('draw6','normal','dynamic','4k'", "if not isolation and mode in ('draw6','normal','dynamic','4k'")
s=s.replace("if mode=='draw6':", "if mode=='draw6' and not isolation:")
s=s.replace("else:assert max(e.get('completed',0)", "elif not isolation:assert max(e.get('completed',0)")
s=s.replace("row=dict(mode=mode,", "row=dict(isolation=isolation,mode=mode,")
s=s.replace(" debug=(d/'debug.txt').read_text();", " if isolation:\n  assert any(e['stage']=='diagnostic_evaluate_only_no_writeback' for e in events)\n  assert max(e.get('evaluate_done',0) for e in events)>=1500\n  assert not any(e.get('composed',0) or e.get('compose_done',0) or e.get('completed',0) for e in events)\n  assert any(e['stage']=='retired' for e in events)\n debug=(d/'debug.txt').read_text();")
p.write_text(s)
# Host final pixel equality and composed checks were designed for visible SR.
# The neutral host image already has identical RGB; retain alpha/state checks.
p=r/'sr-host.cpp';s=p.read_text();p.write_text(s)
