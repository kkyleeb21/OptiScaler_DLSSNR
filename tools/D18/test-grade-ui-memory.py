from pathlib import Path
import subprocess,re,os
import argparse
ap=argparse.ArgumentParser()
ap.add_argument('--source',required=True,type=Path)
ap.add_argument('--build',required=True,type=Path)
ap.add_argument('--report',required=True,type=Path)
ap.add_argument('--dependency-root',type=Path,default=Path('E:/DLSSNR/workspace/dlss5/worktrees/optiscaler-internal-scaling'))
a=ap.parse_args()
report=a.report;repo=a.source;out=a.build/'cpu-grade-ui-memory';out.mkdir(exist_ok=True)
dep=a.dependency_root

def function(text,name):
 start=text.index(name);brace=text.index('{',start);depth=1;end=brace+1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
cfg=(repo/'OptiScaler/Config.h').read_text(encoding='utf-8')
optional=cfg[cfg.index('enum HasDefaultValue'):cfg.index('constexpr inline int UnboundKey')]
fields='\n'.join(line for line in cfg.splitlines() if re.search(r'CustomOptional<.*> DlssNr(Grade\w+|LocalTone|PassCount|Style|MaxRatio|MaxDarken|PauseWhenFgOff)\b',line))
(out/'Config.h').write_text('#pragma once\n#include <optional>\n#include <string>\n#include <concepts>\n#include <cstdint>\n#include <array>\n'+optional+'\nclass Config{public:\n'+'struct GradePresetSlot { CustomOptional<std::string, SoftDefault> Name{""}; CustomOptional<unsigned, SoftDefault> Style{0}; CustomOptional<std::string, SoftDefault> Values{""}; }; std::array<GradePresetSlot,3> DlssNrGradePresets;\n'+fields+'\n};\n',encoding='utf-8')
menu=(repo/'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')
backend=(repo/'OptiScaler/dlssnr/NrGradeTable.cpp').read_text(encoding='utf-8')
helpers=function(backend,'Values ReadValues(')+'\n'+function(backend,'void SetValues(')
grade=function(menu,'static std::string GradePresetName(')+'\n'+function(menu,'static void RenderComposition(')+'\n'+function(menu,'static void RenderGrade(')
help=function(menu,'static void HelpMarker(')
pause=menu[menu.index('        bool pause=config->DlssNrPauseWhenFgOff'):menu.index('        RenderComposition(config')]
pause='static void RenderPause(Config* config) {\n'+pause+'\n}'
src=r'''
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <map>
#include <string>
#include <cassert>
#include "Config.h"
#include <dlssnr/NrGradeTable.h>
#include <dlssnr/GradePresets.h>
#include <dlssnr/ComposeLimits.h>
#include <dlssnr/FgPauseSignal.h>
#include <menu/D18NrHints.h>
#include <menu/D18Layout.h>
enum class API{DX11,DX12,Vulkan};
struct State{API api=API::DX12;static State& Instance(){static State s;return s;}};
namespace DlssNr::Grade{
std::atomic<Status> published{Status::Disabled};
void RestoreIfDisabled(const Config&){}
Status CurrentStatus(){return published.load();}
'''+helpers+r'''
}
namespace StreamlineHooks{static unsigned calls=0;void enableNativeFgPause(){++calls;}}
namespace DlssNr{
namespace FgPause{Status Current(){return Status::NoSignal;}const char* StatusText(){return "fixture";}}
struct PauseSnapshot{FgPause::Status fgPause=FgPause::Status::Running;};
PauseSnapshot ReadUiSnapshot(){return {};}
'''+help+'\n'+pause+'\n'+grade+r'''
}
struct Item{ImRect rect;std::string label;};
static std::map<ImGuiID,ImRect> rects;
static std::map<std::string,Item> items;
static unsigned sliders=0,storeCount=0;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*,ImGuiID id,const ImRect& bb,const ImGuiLastItemData*){rects[id]=bb;}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*,ImGuiID id,const char* label,ImGuiItemStatusFlags){
 std::string key=label;auto pos=key.rfind("###");if(pos!=std::string::npos)key=key.substr(pos+3);
 if(key=="Store")key="Store"+std::to_string(storeCount++);
 if(key=="##value") {
  ++sliders;auto& stack=ImGui::GetCurrentWindow()->IDStack;auto parent=stack[stack.Size-2];
  for(const char* name:{"Brighten limit","Darken limit"})if(id==ImHashStr("##value",0,ImHashStr(name,0,parent)))key=std::string(name)+" slider";
 }
 items[key]={rects[id],label};
}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...){}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID){return "fixture";}
static void frame(Config& cfg){
 items.clear();rects.clear();sliders=0;storeCount=0;
 ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({720,2000});
 ImGui::Begin("grade",nullptr,ImGuiWindowFlags_NoSavedSettings);DlssNr::RenderPause(&cfg);DlssNr::RenderGrade(&cfg,1);DlssNr::RenderComposition(&cfg,1,false);ImGui::End();ImGui::Render();
}
static void click(Config& cfg,const char* label,float fraction=.5f){
 assert(items.count(label));auto center=items.at(label).rect.GetCenter();auto r=items.at(label).rect;center.x=r.Min.x+(r.Max.x-r.Min.x)*fraction;
 auto& io=ImGui::GetIO();io.AddMousePosEvent(center.x,center.y);io.AddMouseButtonEvent(0,true);frame(cfg);
 io.AddMouseButtonEvent(0,false);frame(cfg);frame(cfg);
}
int main(){
 using namespace DlssNr::Grade;
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={720,2000};io.DeltaTime=1.f/60;
 io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->AddFontDefault();
 ImGui::GetCurrentContext()->TestEngineHookItems=true;
 Config cfg;assert(!cfg.DlssNrGradeEnabled.value_or_default() && ReadValues(cfg)==Defaults);
 frame(cfg);frame(cfg);assert(sliders==0 && !items.count("Use custom colour grade"));
 assert(!items.count("NR while game FG is off") && !items.count("Alternate-frame reuse (experimental)"));
 click(cfg,"Pause NR while the game turns frame generation off");assert(cfg.DlssNrPauseWhenFgOff.value_or_default() && StreamlineHooks::calls==1);
 click(cfg,"Pause NR while the game turns frame generation off");assert(!cfg.DlssNrPauseWhenFgOff.value_or_default() && StreamlineHooks::calls==1);
 click(cfg,"Colour grade");assert(sliders==3 && items.count("Use custom colour grade"));
 click(cfg,"GradePreset1");assert(!cfg.DlssNrGradeEnabled.value_or_default());
 click(cfg,"Natural colour grade");cfg.DlssNrStyle=1u;
 click(cfg,"Store0");assert(ReadPreset(cfg,0) && ReadPreset(cfg,0)->style==1 && MatchesPreset(cfg,*ReadPreset(cfg,0)));
 click(cfg,"No grade");cfg.DlssNrStyle=0u;
 click(cfg,"GradePreset1");assert(ReadValues(cfg)==Preset(1) && cfg.DlssNrStyle.value_or_default()==1);
 click(cfg,"Composition");assert(sliders==5 && items.count("Brighten limit slider") && items.count("Darken limit slider"));
 click(cfg,"Brighten limit slider",.7f);assert(cfg.DlssNrMaxRatio.value_or_default()>2.f);
 click(cfg,"Same as brighten limit");assert(cfg.DlssNrMaxDarken.has_value());
 const float previous=cfg.DlssNrMaxDarken.value();click(cfg,"Darken limit slider",.25f);assert(cfg.DlssNrMaxDarken.value()!=previous && cfg.DlssNrMaxDarken.value()>=1 && cfg.DlssNrMaxDarken.value()<=8);
 click(cfg,"Same as brighten limit");assert(!cfg.DlssNrMaxDarken.has_value());
 click(cfg,"Composition"); // return to three grade sliders
 click(cfg,"Natural colour grade");assert(cfg.DlssNrGradeEnabled.value_or_default() && ReadValues(cfg)==Preset(1));
 click(cfg,"Cinematic colour grade");cfg.DlssNrStyle=2u;click(cfg,"Store1");
 click(cfg,"No grade");cfg.DlssNrStyle=0u;click(cfg,"Store2");
 click(cfg,"GradePreset2");assert(ReadValues(cfg)==Preset(2) && cfg.DlssNrStyle.value_or_default()==2);
 click(cfg,"GradePreset3");assert(ReadValues(cfg)==Defaults && cfg.DlssNrStyle.value_or_default()==0);
 click(cfg,"Cinematic colour grade");assert(ReadValues(cfg)==Preset(2));
 click(cfg,"No grade");assert(cfg.DlssNrGradeEnabled.value_or_default() && ReadValues(cfg)==Defaults);
 click(cfg,"More grade controls");assert(sliders==14);
 click(cfg,"Reset all grades");assert(!cfg.DlssNrGradeEnabled.value_or_default() && ReadValues(cfg)==Defaults);
 D18Ui::chineseFontAvailable=true;D18Ui::SetLanguage(1);frame(cfg);
 assert(items.at("Colour grade").label.find("Colour grade")==std::string::npos || items.at("Colour grade").label.find("###")!=std::string::npos);
 assert(std::string(D18Ui::Tr("Colour grade"))!="Colour grade");assert(sliders==14);
 click(cfg,"Natural colour grade");assert(ReadValues(cfg)==Preset(1) && cfg.DlssNrGradeEnabled.value_or_default());
 click(cfg,"Pause NR while the game turns frame generation off");assert(cfg.DlssNrPauseWhenFgOff.value_or_default());
 click(cfg,"Pause NR while the game turns frame generation off");assert(!cfg.DlssNrPauseWhenFgOff.value_or_default());
 ImGui::DestroyContext();puts("PASS G2 CPU ImGui: three personal save/load/empty buttons, style switching, two clickable limit sliders, auto toggle,  default collapsed, common/advanced 14 sliders, actual preset clicks, reset, English/Chinese stable IDs; no renderer/GPU backend");
}
'''
(out/'grade-ui.cpp').write_text(src,encoding='utf-8')
# Extract the production Windows memory adapter unchanged; use only our own CPU allocation.
memory=backend[backend.index('struct Memory {'):backend.index('} memory;')]+ '};'
src=r'''
#define NOMINMAX
#include <windows.h>
#include <dlssnr/NrGradeTable.h>
#include <dlssnr/GradePresets.h>
#include <dlssnr/ComposeLimits.h>
#include <dlssnr/NativeSampler.h>
#include <cassert>
#include <cstdio>
static unsigned protectCalls=0,failAt=0,failCount=0;
static BOOL protection(void* p,SIZE_T size,DWORD wanted,DWORD* old){
 ++protectCalls;if(failAt && protectCalls>=failAt && failCount){--failCount;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 return ::VirtualProtect(p,size,wanted,old);
}
#define VirtualProtect protection
using namespace DlssNr::Grade;
'''+memory+r'''
static DWORD protectOf(void* p){MEMORY_BASIC_INFORMATION m{};assert(VirtualQuery(p,&m,sizeof(m)));return m.Protect;}
int main(){
 auto* base=static_cast<unsigned char*>(VirtualAlloc(nullptr,0xb2000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(base);
 const auto orig=Original();memcpy(base+TableRva,orig.data(),sizeof(orig));
 auto* first=base+0xb0000;auto* second=base+0xb1000;DWORD old=0;
 assert(::VirtualProtect(first,0x1000,PAGE_READONLY,&old));
 // Keep distinct original protections to catch accidentally restoring a single value to both pages.
 assert(protectOf(first)==PAGE_READONLY && protectOf(second)==PAGE_READWRITE);
 Memory m;const auto want=Pack(Preset(1),.5f);
 assert(m.Write(reinterpret_cast<uintptr_t>(base),want));assert(memcmp(base+TableRva,want.data(),sizeof(want))==0);
 assert(protectOf(first)==PAGE_READONLY && protectOf(second)==PAGE_READWRITE);
 assert(m.Write(reinterpret_cast<uintptr_t>(base),orig));assert(memcmp(base+TableRva,orig.data(),sizeof(orig))==0);
 protectCalls=0;failAt=2;failCount=1;
 assert(!m.Write(reinterpret_cast<uintptr_t>(base),want));assert(memcmp(base+TableRva,orig.data(),sizeof(orig))==0);
 assert(protectOf(first)==PAGE_READONLY && protectOf(second)==PAGE_READWRITE);
 protectCalls=0;failAt=3;failCount=2;
 assert(!m.Write(reinterpret_cast<uintptr_t>(base),want));assert(m.pages[0].pending);
 failAt=0;assert(m.Write(reinterpret_cast<uintptr_t>(base),orig));assert(!m.pages[0].pending && !m.pages[1].pending);
 assert(protectOf(first)==PAGE_READONLY && protectOf(second)==PAGE_READWRITE);
 assert(memcmp(base+TableRva,orig.data(),sizeof(orig))==0);assert(VirtualFree(base,0,MEM_RELEASE));
 puts("PASS CPU Windows adapter: separate page protections, exact original restore, partial protect rollback, failed protection restore retry; own allocation only");
}
'''
(out/'grade-memory.cpp').write_text(src,encoding='utf-8')
imgui=repo/'OptiScaler/include/imgui'
sources=[out/'grade-ui.cpp']+[imgui/(name+'.cpp') for name in ['imgui','imgui_draw','imgui_widgets','imgui_tables']]+[imgui/'misc/freetype/imgui_freetype.cpp']
include=[repo/'OptiScaler',repo/'OptiScaler/include',dep/'external/freetype',out]
incs=' '.join('/I"'+str(p)+'"' for p in include)
commands=['@echo off','call "C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul',
 'cl /nologo /std:c++20 /EHsc /utf-8 /fp:precise /O1 /DUNICODE /D_UNICODE /DIMGUI_ENABLE_TEST_ENGINE '+incs+' '+' '.join('"'+str(p)+'"' for p in sources)+' /Fe:"'+str(out/'grade-ui.exe')+'" /link "'+str(dep/'external/freetype/freetype.lib')+'" user32.lib',
 'if errorlevel 1 exit /b 1',
 'cl /nologo /std:c++20 /EHsc /utf-8 /fp:precise /W4 /WX /DUNICODE /D_UNICODE '+incs+' "'+str(out/'grade-memory.cpp')+'" /Fe:"'+str(out/'grade-memory.exe')+'"',
 'if errorlevel 1 exit /b 1']
(out/'build.cmd').write_text('\n'.join(commands)+'\n',encoding='utf-8')
with (report/'cpu-ui-memory-build.log').open('w',encoding='utf-8') as log:
 r=subprocess.run(['cmd','/d','/c',str(out/'build.cmd')],cwd=out,stdout=log,stderr=subprocess.STDOUT)
assert r.returncode==0,(report/'cpu-ui-memory-build.log').read_text()
with (report/'cpu-ui-memory-tests.log').open('w',encoding='utf-8') as log:
 for name in ['grade-ui.exe','grade-memory.exe']:
  r=subprocess.run([str(out/name)],cwd=out,capture_output=True,text=True)
  log.write(r.stdout+r.stderr);print(r.stdout+r.stderr);assert r.returncode==0,(name,r.returncode)
