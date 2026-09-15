"""Prepare an isolated host-only diagnostic from explicit complete and probe baselines."""
from pathlib import Path
import argparse,shutil,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--probe-source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
assert not a.output.exists();a.output.mkdir(parents=True)
shutil.copytree(a.baseline/'core-source',a.output/'core-source')
s=a.probe_source.read_text(encoding='utf-8-sig')
def rep(old,new):
 global s
 assert s.count(old)==1,(old,s.count(old));s=s.replace(old,new)
rep('require(argc==7,"forwarder runtime data-path driver-core mode output-directory");','require(argc==8,"forwarder runtime data-path driver-core mode output-directory style");\n    const int style=_wtoi(argv[7]);require(style==0||style==2,"style 0 or 2");')
rep('mode==L"shared_batch"||mode==L"shared_split"||mode==L"independent_batch"','mode==L"shared_batch"||mode==L"shared_same_input"||mode==L"independent_batch"')
rep('const bool shared=mode!=L"independent_batch",split=mode==L"shared_split";','const bool shared=mode!=L"independent_batch",sameInput=mode==L"shared_same_input";')
rep('if(dbg)debug->EnableDebugLayer();','if(dbg)debug->EnableDebugLayer();printf("debug_layer=%u style=%d warmup=48 measured=16\\n",unsigned(dbg),style);')
rep('w,h,0,1.f,0,1,1,-1,1,1,ratios[i]','w,h,0,1.f,style,1,1,-1,1,1,ratios[i]')
rep('for(unsigned frame=0;frame<16;++frame)','for(unsigned frame=0;frame<64;++frame)')
rep('params[instance],previous,depth.Get()','params[instance],sameInput?color.Get():previous,depth.Get()')
rep('w,h,gw,gh,0,reset,1,0,1,1,-1,1,float(gw),float(gh)','w,h,gw,gh,0,reset,1,style,1,1,-1,1,float(gw),float(gh)')
rep('            if(split&&pass==0)submit();','')
rep('submit();dump(firstReadback.Get(),L"pass1",frame);dump(secondReadback.Get(),L"pass2",frame);fflush(stdout);','submit();if(frame>=48){dump(firstReadback.Get(),L"pass1",frame);dump(secondReadback.Get(),L"pass2",frame);}fflush(stdout);')
rep('640x384 16 frames; synthetic input','640x384 64 frames (last 16 saved); synthetic input')
target=a.output/'core-source/tools/D18/probe-shared-feedback-runtime.cpp';target.write_text(s,encoding='utf-8')
shutil.copy2(a.baseline/'run-multipass-checks.py',a.output/'run-multipass-checks.py')
(a.output/'preparation.json').write_text(json.dumps({'baseline':str(a.baseline.resolve()),'probe_source':str(a.probe_source.resolve()),'probe_source_sha256':hashlib.sha256(a.probe_source.read_bytes()).hexdigest().upper(),'scope':'host-only static synthetic input; game core unchanged'},indent=2))
print(target)
