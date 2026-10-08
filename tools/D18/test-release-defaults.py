"""CPU-only production INI reader fixture; optional installed INI is read-only."""
from pathlib import Path
import argparse,re,subprocess,json
ap=argparse.ArgumentParser()
for name in ['source','build','report']:ap.add_argument('--'+name,required=True,type=Path)
ap.add_argument('--dependency-root',type=Path,default=Path('E:/DLSSNR/workspace/dlss5/worktrees/optiscaler-internal-scaling'))
ap.add_argument('--ini',type=Path)
a=ap.parse_args();out=a.build/('cpu-upgrade-defaults' if a.ini else 'cpu-release-defaults');out.mkdir(exist_ok=True)
cfg=(a.source/'OptiScaler/Config.h').read_text(encoding='utf-8');cpp=(a.source/'OptiScaler/Config.cpp').read_text(encoding='utf-8')
def fn(t,name):
 start=t.index(name);b=t.index('{',start);e=b+1;depth=1
 while depth:depth+=(t[e]=='{')-(t[e]=='}');e+=1
 return t[start:e]
optional=cfg[cfg.index('enum HasDefaultValue'):cfg.index('constexpr inline int UnboundKey')]
fields='\n'.join(l for l in cfg.splitlines() if re.search(r'CustomOptional<.*> DlssNr(Grade\w+|MaxDarken|PauseWhenFgOff)\b',l))
slots=cfg[cfg.index('    struct GradePresetSlot {'):cfg.index('    CustomOptional<bool> DlssNrGradeEnabled')]
load=cpp[cpp.index('            // Migrate the retired experiment once;'):cpp.index('            DlssNrNativeSrEnabled.set_from_config')]
load+=cpp[cpp.index('            const float darken='):cpp.index('            DlssNrHighlightEncoding.set_from_config')]
load+=cpp[cpp.index('            for(unsigned i=0;i<3;++i) {\n                const auto key="GradePreset'):cpp.index('            DlssNrSkinStructure.set_from_config')]
helpers=fn(cpp,'static inline bool isFloat(')
readers='\n'.join(fn(cpp,name) for name in ['std::optional<std::string> Config::readString(','std::optional<float> Config::readFloat(','std::optional<uint32_t> Config::readUInt(','std::optional<bool> Config::readBool('])
readers=readers.replace('bool lowercase)','bool lowercase=true)')
source=r'''
#include <optional>
#include <string>
#include <array>
#include <concepts>
#include <vector>
#include <format>
#include <ranges>
#include <sstream>
#include <cctype>
#include <fstream>
#include <cassert>
#include <cstdio>
#include <SimpleIni.h>
#include <dlssnr/GradePresets.h>
#include <dlssnr/ComposeLimits.h>
#include <dlssnr/FgPauseSignal.h>
static CSimpleIniA ini;
'''+optional+'\n'+helpers+r'''
struct Config {
std::vector<std::string> _log;
'''+slots+fields+'\n'+readers.replace('Config::','')+'\nvoid Load(){\n'+load+r'''
}
};
static void check(const std::string& text,bool pause){
 ini.Reset();assert(ini.LoadData(text.c_str(),text.size())>=0);Config c;c.Load();
 assert(c.DlssNrPauseWhenFgOff.value_or_default()==pause);
 assert(!c.DlssNrGradeEnabled.value_or_default() && !c.DlssNrMaxDarken.has_value());
 const DlssNr::Grade::Values values={c.DlssNrGradeBlack.value_or_default(),c.DlssNrGradeWhite.value_or_default(),c.DlssNrGradeExposure.value_or_default(),c.DlssNrGradeGamma.value_or_default(),c.DlssNrGradeContrast.value_or_default(),c.DlssNrGradeSaturation.value_or_default(),c.DlssNrGradeSaturationGamma.value_or_default(),c.DlssNrGradeTintA.value_or_default(),c.DlssNrGradeTintB.value_or_default(),c.DlssNrGradeCurve1.value_or_default(),c.DlssNrGradeCurve2.value_or_default(),c.DlssNrGradeCurve3.value_or_default(),c.DlssNrGradeCurve4.value_or_default(),c.DlssNrGradeCurve5.value_or_default()};
 assert(values==DlssNr::Grade::Defaults);
 for(auto& p:c.DlssNrGradePresets)assert(p.Values.value_or_default().empty() && p.Name.value_or_default().empty());
 assert(ini.GetValue("DlssNr","FgOffMode",nullptr)==nullptr);
 std::string saved;assert(ini.Save(saved)>=0);assert(saved.find("FgOffMode")==std::string::npos);
}
static std::string read(const char* path){std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){
 check("",false);check(read(argv[1]),false);check(read(argv[2]),false);
 check("[DlssNr]\nPauseWhenFgOff=false\nFgOffMode=1\n",false);
 check("[DlssNr]\nPauseWhenFgOff=false\nFgOffMode=2\n",false);
 check("[DlssNr]\nPauseWhenFgOff=true\nFgOffMode=2\n",true);
 check("[DlssNr]\nFgOffMode=1\n",true);
 check("[DlssNr]\nPauseWhenFgOff=auto\nFgOffMode=1\n",true);
 check("[DlssNr]\nPauseWhenFgOff=true\nFgOffMode=1\n",true);
 check("[DlssNr]\nPauseWhenFgOff=garbage\nFgOffMode=1\n",false);
 check("[DlssNr]\nFgOffMode=0\n",false);
 check("[DlssNr]\nFgOffMode=garbage\n",false);
 check("[DlssNr]\nFgOffMode=auto\n",false);
 if(argc>3)check(read(argv[3]),false);
 using namespace DlssNr::FgPause;
 Reset();installation=Installation::Ready;auto before=signal.load();Accept(before,7,0,true,true);
 assert(Read(true,true,true)==Status::NoSignal);
 Accept(before,7,1,true,true);Accept(before,7,0,true,false);assert(Read(true,true,true)==Status::Running);
 Accept(before,7,0,true,true);assert(Read(true,true,true)==Status::Paused);
 assert(Read(false,true,true)==Status::OptionOff && Read(true,false,true)==Status::NoSignal && Read(true,true,false)==Status::Running);
 Accept(before,7,1,true,true);assert(Read(true,true,true)==Status::Running);
 Reset();Accept(before,7,0,true,true);assert(Read(true,true,true)==Status::NoSignal);
 before=signal.load();Accept(before,8,1,true,true);Accept(before,9,0,true,true);assert(Read(true,true,true)==Status::NoSignal);
 // Audit sequences: A on, B on/off; then A off. Both must remain no-signal.
 Reset();before=signal.load();Accept(before,100,1,true,true);Accept(before,200,1,true,true);
 Accept(before,200,0,true,true);assert(MultipleViewports() && Read(true,true,true)==Status::NoSignal);
 Accept(before,100,0,true,true);assert(MultipleViewports() && Read(true,true,true)==Status::NoSignal);
 // Invalid/failed observations do not poison single-viewport ownership.
 Reset();before=signal.load();Accept(before,100,1,true,true);Accept(before,200,1,true,false);
 Accept(before,200,1,false,true);Accept(before,100,0,true,true);assert(Read(true,true,true)==Status::Paused);
 Reset();before=signal.load();Accept(before,0xffffffffu,1,true,true);Accept(before,0xffffffffu,0,true,true);
 assert(Read(true,true,true)==Status::Paused);Accept(before,0xfffffffeu,0,true,true);
 assert(MultipleViewports() && Read(true,true,true)==Status::NoSignal);
 puts("PASS G1 audit A on, B on/off, A off stays no-signal; G2 old=1/new=false respects false");
 puts("PASS production CPU INI: empty, 0.3.1, fresh 0.4.0, legacy modes 1/2/0/invalid/auto; disabled grade, 14 defaults, auto darken, empty slots; retired key not saved; pause/resume/lifecycle/viewport guards");
}
'''
(out/'test.cpp').write_text(source,encoding='utf-8')
old=Path('E:/DLSSNR/builds/D18_031_RELEASE_20261006/DLSSNR_D18_0.3.1_release/D18/payload/OptiScaler.ini.d18')
args=[str(out/'test.exe'),str(old),str(a.source/'community/d18-installer/OptiScaler.ini.d18')]+([str(a.ini)] if a.ini else [])
command='@echo off\ncall "C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\ncl /nologo /std:c++20 /EHsc /utf-8 /fp:precise /I"'+str(a.source/'OptiScaler')+'" /I"'+str(a.dependency_root/'external/simpleini')+'" test.cpp /Fe:test.exe\nif errorlevel 1 exit /b 1\n'+subprocess.list2cmdline(args)+'\n'
(out/'run.cmd').write_text(command,encoding='utf-8')
result=subprocess.run(['cmd','/d','/c',str(out/'run.cmd')],cwd=out,capture_output=True,text=True)
name='upgrade-defaults' if a.ini else 'release-defaults'
(a.report/(name+'.command.json')).write_text(json.dumps(command,indent=2),encoding='utf-8')
(a.report/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
(a.report/(name+'.exit.json')).write_text(json.dumps({'exit_code':result.returncode}),encoding='utf-8')
print(result.stdout+result.stderr);raise SystemExit(result.returncode)
