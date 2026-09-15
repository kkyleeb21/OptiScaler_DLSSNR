"""CPU regression of production presentation gates, plus shared startup summary decoding.
No game, runtime, GPU or settings are loaded or modified.
"""
import argparse, re, subprocess, os, json, importlib.util
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--candidate',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
r=a.candidate.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);src=r/'core-source/OptiScaler'
factory=(src/'hooks/DxgiFactory_WrappedCalls.cpp').read_text(encoding='utf-8-sig')
gate='static bool ShouldCreateDx11wDx12Swapchain(){return DlssNr::PrepareDx11FgPresentation(State::Instance());}'
for name in ['DxgiFactory_WrappedCalls.cpp','DxgiFactory_Hooks.cpp']:
 text=(src/'hooks'/name).read_text(encoding='utf-8-sig')
 assert text.count('DlssNr::PrepareDx11FgPresentation(State::Instance())')==2
 assert 'FGEnabled' not in text

header=(src/'dlssnr/PresentationCapabilities.h').read_text(encoding='utf-8-sig').replace('#pragma once','').replace('#include <State.h>','')
menu=(src/'menu/menu_common.cpp').read_text(encoding='utf-8-sig');menu=menu[menu.index('void MenuCommon::RenderD18DlssFgSettings'):]
selected=re.search(r'const bool selectedDx11DlssgRoute =.*?;',menu,re.S).group()
nextstart=re.search(r'const bool configureNextStartup =.*?;',menu,re.S).group()
assert 'if(!configureNextStartup && DlssNr::NativeFgDx11::IsRoute()) DlssNr::NativeFgDx11::SetEnabled(fgEnabled);\n            else config->FGEnabled = fgEnabled;' in menu
present=(src/'hooks/FG_Hooks.cpp').read_text(encoding='utf-8-sig')
assert present.index('DlssNr::NativeFgDx11::ApplyControl();',present.index('HRESULT FGHooks::FGPresent')) < present.index('_lastPresentFlags = Flags;',present.index('HRESULT FGHooks::FGPresent'))
code="""
#include <cassert>
#include <initializer_list>
enum class API {NotSelected, DX11, DX12, Vulkan};
enum class FGInput {NoFG, Upscaler, NvngxFG};
enum class FGOutput {NoFG, DLSSG, FSRFG, XeFG};
enum class SwapchainInteropApi {None, Dx11wDx12};
struct State {API api=API::NotSelected,swapchainApi=API::NotSelected;SwapchainInteropApi swapchainInteropApi=SwapchainInteropApi::None;FGInput activeFgInput=FGInput::NoFG;FGOutput activeFgOutput=FGOutput::NoFG;bool fgSettingsChanged=false;static State& Instance(){static State s;return s;}};
template<class T> struct Setting{T value;T value_or_default()const{return value;}};
struct Settings{Setting<FGInput> FGInput{FGInput::NoFG};Setting<FGOutput> FGOutput{FGOutput::NoFG};Setting<bool> FGEnabled{false};};
"""+header+gate+"""
bool nextStartup(State& state, Settings* config){
const bool dx11Presentation=DlssNr::HasDx11Presentation(state);
const bool dx11DlssgRoute=dx11Presentation && state.activeFgInput==FGInput::Upscaler && state.activeFgOutput==FGOutput::DLSSG;
"""+selected+nextstart+"""
return configureNextStartup;
}
int main(){
auto& state=State::Instance();Settings config;
for(auto sr:{API::NotSelected,API::DX11,API::DX12}){
 state.api=sr;state.swapchainApi=API::DX11;state.swapchainInteropApi=SwapchainInteropApi::None;
 assert(DlssNr::HasDx11Presentation(state));
 config.FGInput.value=FGInput::Upscaler;config.FGOutput.value=FGOutput::DLSSG;
 state.activeFgInput=FGInput::NoFG;state.activeFgOutput=FGOutput::NoFG;
 assert(nextStartup(state,&config));assert(!ShouldCreateDx11wDx12Swapchain());
 state.activeFgInput=FGInput::Upscaler;state.activeFgOutput=FGOutput::DLSSG;
 for(bool on:{false,true}){config.FGEnabled.value=on;assert(ShouldCreateDx11wDx12Swapchain());assert(nextStartup(state,&config));}
 state.swapchainInteropApi=SwapchainInteropApi::Dx11wDx12;
 assert(!nextStartup(state,&config));state.fgSettingsChanged=true;assert(nextStartup(state,&config));state.fgSettingsChanged=false;
}
state.swapchainInteropApi=SwapchainInteropApi::None;
for(auto api:{API::DX12,API::Vulkan,API::NotSelected}){state.swapchainApi=api;assert(!DlssNr::HasDx11Presentation(state));}
state.activeFgInput=FGInput::NvngxFG;assert(!ShouldCreateDx11wDx12Swapchain());
state.activeFgInput=FGInput::Upscaler;state.activeFgOutput=FGOutput::NoFG;assert(!ShouldCreateDx11wDx12Swapchain());
}
"""
cpp=out/'native-controls.cpp';cpp.write_text(code,encoding='utf-8');exe=out/'native-controls.exe'
cmd=out/'test.cmd';cmd.write_text(f'@echo off\ncall "C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul && cl /nologo /std:c++20 /EHsc "{cpp}" /Fo"{out}/native-controls.obj" /Fe"{exe}" && "{exe}"\n',encoding='utf-8')
env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['Path']=os.environ['PATH']
run=subprocess.run(['cmd.exe','/d','/c',str(cmd)],capture_output=True,env=env);(out/'build.log').write_bytes(run.stdout+run.stderr);assert run.returncode==0,(run.stdout+run.stderr).decode(errors='replace')
spec=importlib.util.spec_from_file_location('diag',r/'core-source/tools/D18/summarize-diagnostics.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
header=m.HEADER.pack(b'D18DIAG',1,m.HEADER.size,m.RECORD.size,1,1,2,0,0,b'observed',b'legacy',b'fixture')
record=m.RECORD.pack(1,10,12,2,0,0,0,0,3840,2160,3840,2160,0,0,0,3,1,1,1,0,0,b'native_sr_progress',b'gate=7;age_ms=1;eval=0;gpu=0;handoff=0',1)
ring=out/'fixture.ring';ring.write_bytes(header+record);h,rows=m.read_ring(ring);result=m.summarize(h,rows)
assert len(result['native_sr_progress'])==1 and result['first_anomaly'] is None
md=m.markdown(result);assert 'gate=7' in md and 'scale generation 2' in md
assert 'Unobserved.' in m.markdown(m.summarize(h,[]))
(out/'result.json').write_text(json.dumps({'passed':True,'scope':'CPU production gates and binary shared summary; no GPU/game execution'},indent=2),encoding='utf-8')
print('PASS native presentation selection before SR, prepared-off FG route, startup commit path, Present consumption, and shared startup evidence')
