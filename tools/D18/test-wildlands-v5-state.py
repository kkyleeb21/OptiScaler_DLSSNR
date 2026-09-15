from pathlib import Path
import subprocess,json
r=Path('E:/DLSSNR/builds/D18_Wildlands_SRContractsV5_20260913')
s=(r/'core-source/OptiScaler/hooks/D18WildlandsSr.inl').read_text(encoding='utf-8')
def function(name):
 a=s.index('inline ',s.index(name)-30);start=s.index('{',s.index(name));level=1;b=start+1
 while level:
  if s[b]=='{':level+=1
  elif s[b]=='}':level-=1
  b+=1
 return s[a:b]
code=r'''#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <cstdio>
#pragma comment(lib,"d3d11.lib")
using Microsoft::WRL::ComPtr;
namespace Status {std::atomic<bool> failed{false};std::atomic<long> code{0};std::atomic<unsigned long long> gpuCompleted{0},lastGpuTick{0};}
bool fault=false,reset=false,shadowValid=true,initialized=false;std::mutex guard;
void Event(const char*,long=0){} void GpuEvent(const char*,long=0){}
ComPtr<ID3D11DeviceContext> context;
unsigned long long gpuComplete=0,gpuDrained=0,gpuStages[3]{};
const void* presentOwner=nullptr;
'''
code+=s[s.index('struct GpuFrame'):s.index('inline unsigned long long gpuSubmitted')].replace('inline GpuFrame','GpuFrame')
code+='\n'+function('void Fail(')+'\n'+function('void PollGpu(')+'\n'+function('bool AcceptPresent(')+'\n'+function('void ReleasePresent(')
code+=r'''
bool drain(GpuFrame& f){for(int i=0;i<1000&&f.pending;++i){PollGpu();Sleep(1);}return !f.pending;}
int main(){ComPtr<ID3D11Device> d;D3D_FEATURE_LEVEL fl;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&fl,&context)))return 1;
 auto& f=gpuFrames[0];D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};
 for(auto& q:f.stage){if(FAILED(d->CreateQuery(&desc,&q)))return 2;context->End(q.Get());}
 f.pending=true;f.issued=3;f.tick=GetTickCount64();context->Flush();if(!drain(f)||gpuComplete!=1)return 3;
 f.pending=true;f.done=0;f.issued=2;f.tick=GetTickCount64();context->End(f.stage[0].Get());context->End(f.stage[1].Get());
 Fail("evaluate",E_FAIL);context->Flush();if(!drain(f)||gpuComplete!=1||gpuDrained!=2||gpuStages[2]!=1)return 4;
 Fail("later_error",E_ABORT);if(Status::code!=E_FAIL)return 5;
 auto& fresh=gpuFrames[1];if(FAILED(d->CreateQuery(&desc,&fresh.stage[0])))return 6;
 context->End(fresh.stage[0].Get());fresh.pending=true;fresh.issued=1;fresh.tick=GetTickCount64();context->Flush();
 if(!drain(fresh)||gpuComplete!=1||gpuDrained!=3)return 7;
 int a=0,b=0;if(AcceptPresent(&b,false)||!AcceptPresent(&a,true)||AcceptPresent(&b,true)||!AcceptPresent(&a,false))return 8;
 ReleasePresent(&b);if(presentOwner!=&a)return 9;ReleasePresent(&a);if(presentOwner||shadowValid)return 10;
 if(!AcceptPresent(&b,true)||!reset)return 11;
 printf("old_query_not_recounted=1 fresh_unissued_not_polled=1 first_error_preserved=1 owner_pinned=1 owner_release_invalidates=1\n");return 0;}
'''
out=r/'state-test';out.mkdir(exist_ok=True);(out/'host.cpp').write_text(code)
env={}
for line in subprocess.check_output(['cmd','/c',r'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64 >nul && set'],text=True).splitlines():
 k,sep,v=line.partition('=')
 if k and sep:env[k.upper()]=v
cl=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe'
p=subprocess.run([str(cl),'/nologo','/W4','/WX','/EHsc','/std:c++17',str(out/'host.cpp'),'/Fe:'+str(out/'host.exe'),'/Fo:'+str(out/'host.obj')],env=env,capture_output=True,text=True)
(out/'build.log').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stdout+p.stderr
p=subprocess.run([str(out/'host.exe')],capture_output=True,text=True,timeout=30)
(out/'results.json').write_text(json.dumps(dict(exit=p.returncode,stdout=p.stdout,stderr=p.stderr),indent=2));print(p.stdout,p.stderr);assert p.returncode==0,p.returncode
