from pathlib import Path
import re,subprocess
import argparse
ap=argparse.ArgumentParser()
ap.add_argument('--source',required=True,type=Path)
ap.add_argument('--build',required=True,type=Path)
ap.add_argument('--report',required=True,type=Path)
ap.add_argument('--dependency-root',type=Path,default=Path('E:/DLSSNR/workspace/dlss5/worktrees/optiscaler-internal-scaling'))
a=ap.parse_args()
report=a.report;repo=a.source;out=a.build/'cpu-grade-presets';out.mkdir(exist_ok=True)

def fn(text,name):
 a=text.index(name);b=text.index('{',a);depth=1;e=b+1
 while depth:depth+=(text[e]=='{')-(text[e]=='}');e+=1
 return text[a:e]
cfg=(repo/'OptiScaler/Config.h').read_text(encoding='utf-8');cpp=(repo/'OptiScaler/Config.cpp').read_text(encoding='utf-8')
optional=cfg[cfg.index('enum HasDefaultValue'):cfg.index('constexpr inline int UnboundKey')]
fields='\n'.join(line for line in cfg.splitlines() if re.search(r'CustomOptional<.*> DlssNr(Grade\w+|Style|MaxDarken|MaxRatio)\b',line))
slots=cfg[cfg.index('    struct GradePresetSlot {'):cfg.index('    CustomOptional<bool> DlssNrGradeEnabled')]
load=cpp[cpp.index('            for(unsigned i=0;i<3;++i) {\n                const auto key="GradePreset'):cpp.index('            DlssNrGradeEnabled.set_from_config')]
save=cpp[cpp.index('    for(unsigned i=0;i<3;++i) {\n        const auto key="GradePreset'):cpp.index('    ini.SetValue("DlssNr", "GradeEnabled"')]
backend=(repo/'OptiScaler/dlssnr/NrGradeTable.cpp').read_text(encoding='utf-8')
helpers=fn(backend,'Values ReadValues(')+'\n'+fn(backend,'void SetValues(')
source='''#define NOMINMAX
#include <optional>
#include <string>
#include <concepts>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <limits>
#include <SimpleIni.h>
#include <dlssnr/GradePresets.h>
#include <dlssnr/ComposeLimits.h>
static CSimpleIniA ini;
'''+optional+'''\nclass Config { public:
'''+slots+fields+'''
static Config* current;static Config* Instance(){return current;}
static std::optional<std::string> readString(std::string section,std::string key,bool lowercase=true){
 const char* value=ini.GetValue(section.c_str(),key.c_str(),"");return std::string(value);
}
void Load(){
'''+load+'''}
void Save(){
'''+save+'''}
};
Config* Config::current=nullptr;
namespace DlssNr::Grade { std::atomic<Status> published{};
void RequestUpdate(){}
'''+helpers+'''\n}
int main(){
 using namespace DlssNr;using namespace DlssNr::Grade;
 const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
 assert(ComposeLimits::Explicit({})==0 && ComposeLimits::Explicit(nan)==0 && ComposeLimits::Explicit(inf)==0);
 assert(ComposeLimits::Explicit(-4)==1 && ComposeLimits::Explicit(99)==8);
 for(float bright:{1.f,1.5f,2.f,8.f})for(float ratio:{0.f,.1f,.52f,.85f,1.f,1.5f,2.f,10.f}) {
  float dark=ComposeLimits::Darken(bright,{});
  const auto a=std::clamp(ratio,1.f/dark,bright),b=std::clamp(ratio,1.f/bright,bright);
  assert(std::bit_cast<uint32_t>(a)==std::bit_cast<uint32_t>(b));
 }
 const auto dark=ComposeLimits::Darken(1.5f,1.f/.85f);
 assert(std::abs(std::clamp(.52f,1.f/dark,1.5f)-.85f)<1e-6f);
 assert(std::clamp(9.f,1.f/dark,1.5f)==1.5f);
 Config a;Config::current=&a;assert(!LoadPreset(a,0));
 SetValues(a,Maximum);a.DlssNrStyle=2u;a.DlssNrGradeEnabled=true;a.DlssNrGradePresets[0].Name="角色夜景";
 SavePreset(a,0);auto p=ReadPreset(a,0);assert(p && p->values==Maximum && p->style==2 && MatchesPreset(a,*p));
 a.Save();std::string text;assert(ini.Save(text)>=0);assert(text.find("GradePreset1Values")!=text.npos);
 ini.Reset();assert(ini.LoadData(text.c_str(),text.size())>=0);
 Config b;Config::current=&b;b.Load();assert(ReadPreset(b,0)->name=="角色夜景");assert(LoadPreset(b,0));
 assert(ReadValues(b)==Maximum && b.DlssNrStyle.value_or_default()==2 && b.DlssNrGradeEnabled.value_or_default());
 assert(!ReadPreset(b,1) && !LoadPreset(b,2));
 for(auto values:{Defaults,Minimum,Maximum,Preset(1),Preset(2)}) {
  auto round=ParsePreset("",1,SerializePreset(values));assert(round && std::memcmp(values.data(),round->values.data(),sizeof(values))==0);
 }
 for(const char* bad:{"","1,2","1,2,3,4,5,6,7,8,9,10,11,12,13,14,15","nan,1,0,0,0,0,0,0,0,0,0,0,0,0","inf,1,0,0,0,0,0,0,0,0,0,0,0,0","abc,1,0,0,0,0,0,0,0,0,0,0,0,0","0,1,0,0,0,0,0,0,0,0,0,0,0,0,","0,1,0,0,0,0,0,0,0,0,0,0,0,","0,1,0,0,0,0,0,0,0,0,0,0,0,0x","0,1e9999,0,0,0,0,0,0,0,0,0,0,0,0","0,9,0,0,0,0,0,0,0,0,0,0,0,0"}) {
  assert(!ParsePreset("",1,bad));ini.SetValue("DlssNr","GradePreset1Values",bad);
  Config invalid;invalid.Load();assert(!ReadPreset(invalid,0));
 }
 for(const char* style:{"-1","3","nan","abc","","1x"}) {
  ini.SetValue("DlssNr","GradePreset1Values",SerializePreset(Defaults).c_str());ini.SetValue("DlssNr","GradePreset1Style",style);
  Config invalid;invalid.Load();assert(!ReadPreset(invalid,0));
 }
 puts("PASS G2: asymmetric clamp endpoints, auto bitwise CPU results, finite limits, store/load/style/empty, exact 14-float roundtrip, production INI read/write loops, malformed/count/NaN/Inf/style/range rejection");
}
'''
(out/'test-g2.cpp').write_text(source,encoding='utf-8')
for mode in ['precise','fast']:
 cmd='@echo off\ncall "C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\ncl /nologo /std:c++20 /EHsc /utf-8 /O2 /fp:'+mode+' /I"'+str(repo/'OptiScaler')+'" /I"'+str(repo/'OptiScaler/include')+'" /I"'+str(a.dependency_root/'external/simpleini')+'" test-g2.cpp /Fe:test-g2-'+mode+'.exe\nif errorlevel 1 exit /b 1\n.\\test-g2-'+mode+'.exe\n'
 (out/'run.cmd').write_text(cmd)
 r=subprocess.run(['cmd','/d','/c',str(out/'run.cmd')],cwd=out,capture_output=True,text=True);(report/('cpu-g2-'+mode+'.log')).write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);assert r.returncode==0
