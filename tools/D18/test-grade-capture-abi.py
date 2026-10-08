from pathlib import Path
import subprocess,json,importlib.util,struct,copy,sys
import argparse
ap=argparse.ArgumentParser()
ap.add_argument('--source',required=True,type=Path)
ap.add_argument('--build',required=True,type=Path)
ap.add_argument('--report',required=True,type=Path)
ap.add_argument('--dependency-root',type=Path,default=Path('E:/DLSSNR/workspace/dlss5/worktrees/optiscaler-internal-scaling'))
a=ap.parse_args()
R=a.source;P=a.report;out=a.build/'cpu-grade-capture';out.mkdir(exist_ok=True)

s='''#define NOMINMAX
#include <dlssnr/CaptureEvidence.h>
#include <dlssnr/CaptureContract.h>
#include <cassert>
int main(int argc,char**argv){
 capture::FrameEvidence e{};e.resolve.WhitePoint=2.5f;e.resolve.MaxRatio=1.5f;e.resolve.MaxDarken=1.f/.85f;
 e.resolve.Width=16;e.resolve.Height=12;e.resolve.SourceWidth=16;e.resolve.SourceHeight=12;e.resolve.Transfer=1;e.resolve.TransferStrength=1;e.resolve.ColourStrength=.5f;
 e.rects.output={0,0,16,12};e.rects.color=e.rects.output;e.rects.depth=e.rects.output;e.rects.motion=e.rects.output;
 e.inputWidth=e.networkWidth=16;e.inputHeight=e.networkHeight=12;e.successfulSinceReset=40;
 auto* f=fopen(argv[1],"wb");assert(f);capture::writeEvidence(f,e);fclose(f);
 std::string json=R"({"test":0})";DlssNr::CaptureContract::Constants(json,e.resolve,e.resolve);
 f=fopen(argv[2],"wb");assert(f);fwrite(json.data(),1,json.size(),f);fclose(f);
}
'''
(out/'test.cpp').write_text(s)
cmd='@echo off\ncall "C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\ncl /nologo /std:c++20 /EHsc /I"'+str(R/'OptiScaler')+'" test.cpp /Fe:test.exe\nif errorlevel 1 exit /b 1\n"'+str(out/'test.exe')+'" "'+str(P/'evidence_00.json')+'" "'+str(P/'constant-contract-g2.json')+'"\n'
(out/'run.cmd').write_text(cmd)
r=subprocess.run(['cmd','/d','/c',str(out/'run.cmd')],cwd=out,capture_output=True,text=True);(P/'capture-abi-build.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
e=json.loads((P/'evidence_00.json').read_text());assert e['constant_abi']=='DlssNrConstants-named-180-v2' and e['schema']=='d18-capture-evidence-v3' and len(bytes.fromhex(e['resolve_constants_hex']))==180
assert struct.pack('<f',e['resolve_constants_named']['MaxDarken'])==bytes.fromhex(e['resolve_constants_hex'])[176:180]
c=json.loads((P/'constant-contract-g2.json').read_text());assert c['constant_abi']==e['constant_abi'] and c['resolve_constants_bytes']==180
sys.path.insert(0,str(Path(__file__).parent));spec=importlib.util.spec_from_file_location('review',Path(__file__).with_name('analyse-capture-evidence.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.evidence(P,0,(16,12,2,256))
for change in [dict(constant_abi='DlssNrConstants-named-176-v1'),dict(resolve_constants_bytes=176),dict(resolve_constants_hex=e['resolve_constants_hex'][:-8]),dict(resolve_constants_named={'MaxDarken':float('nan')})]:
 bad=copy.deepcopy(e);bad.update(change);(P/'evidence_00.json').write_text(json.dumps(bad))
 try:m.evidence(P,0,(16,12,2,256));assert False,'invalid ABI accepted'
 except (ValueError,TypeError):pass
(P/'evidence_00.json').write_text(json.dumps(e,indent=2));(P/'capture-abi-test.log').write_text('PASS production CPU serializers: old offsets retained, sizeof 256, named 180-v2, evidence v3, matching offset-176 MaxDarken, v3 consumer validation, invalid ABI rejected. No GPU.\n');print('PASS capture ABI')
