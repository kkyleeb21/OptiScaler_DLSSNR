"""Compile the production DX12 admission expressions with native/standard SR selector states.
CPU contract regression only: no game, runtime or GPU execution.
"""
from pathlib import Path
import argparse, subprocess, os, re, json
p=argparse.ArgumentParser();p.add_argument('--candidate',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
src=a.candidate/'core-source/OptiScaler'
t=(src/'shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8-sig')
block=re.search(r'    const bool multipassFormatSupported=.*?\n    RecordMultipassContract',t,re.S).group(0).rsplit('\n',1)[0]
code='''#define NOMINMAX
#include <dxgiformat.h>
#include <dlssnr/MultipassPolicy.h>
#include <dlssnr/BuildProfile.h>
#include <dlssnr/MultipassFormats.h>
#include <cassert>
enum class API {NotSelected,DX11,DX12};
struct State {API api=API::NotSelected;static State& Instance(){static State s;return s;}};
struct Setting {unsigned value;unsigned value_or_default()const{return value;}};
struct Config {Setting DlssNrPassCount;};
unsigned admission(unsigned passes,bool highRequested,DXGI_FORMAT format){
 Config cfg{{passes}};struct {DXGI_FORMAT Format;}desc{format};
 const bool _multipassFormatSupported=DlssNr::Multipass::ColorFormat(format);
'''+block+'''
 return requestedPassCount;
}
int main(){
 for(auto api:{API::NotSelected,API::DX11,API::DX12}){
  State::Instance().api=api;
  for(unsigned p=1;p<=4;++p){
   assert(admission(p,false,DXGI_FORMAT_R16G16B16A16_FLOAT)==p);
   assert(admission(p,true,DXGI_FORMAT_R16G16B16A16_FLOAT)==1);
   for(auto f:{DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM})assert(admission(p,false,f)==p);
   for(auto f:{DXGI_FORMAT_UNKNOWN,DXGI_FORMAT_R8G8B8A8_TYPELESS,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R16G16B16A16_UINT})assert(admission(p,false,f)==1);
  }
 }
 static_assert(DlssNr::BuildProfile::SharedHistoryResearch);
 static_assert(DlssNr::BuildProfile::PixelCapture==DlssNr::BuildProfile::Diagnostic);
 static_assert(DlssNr::BuildProfile::ModelBypass==DlssNr::BuildProfile::Diagnostic);
 static_assert(DlssNr::BuildProfile::Capabilities==(DlssNr::BuildProfile::Diagnostic?15:9));
}
'''
cpp=a.output/'contract.cpp';cpp.write_text(code,encoding='utf-8')
env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['Path']=os.environ['PATH']
for mode in [0,1]:
 exe=a.output/f'contract-{mode}.exe';obj=a.output/f'contract-{mode}.obj'
 command=f'call "C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul && cl /nologo /std:c++20 /EHsc /MD /DD18_DIAGNOSTIC_BUILD={mode} /I"{src}" "{cpp}" /Fo"{obj}" /Fe"{exe}" && "{exe}"'
 script=a.output/f'contract-{mode}.cmd';script.write_text('@echo off\n'+command+'\n',encoding='utf-8')
 result=subprocess.run(['cmd.exe','/d','/c',str(script)],capture_output=True,env=env)
 (a.output/f'contract-{mode}.log').write_bytes(result.stdout+result.stderr)
 assert result.returncode==0,result.stdout.decode(errors='replace')+result.stderr.decode(errors='replace')
(a.output/'result.json').write_text(json.dumps(dict(passed=True,profiles=[0,1],scope='CPU production admission expressions; native selector unset vs standard selector, format restrictions, high-resolution remains single, capture isolation',gameplay_verified=False),indent=2),encoding='utf-8')
print('PASS both profiles: native/global API selector cannot disable actual DX12 multipass; format/high-res restrictions and capture isolation retained')
