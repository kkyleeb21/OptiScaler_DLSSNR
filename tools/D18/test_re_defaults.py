import os, pathlib, subprocess, tempfile, unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
STUB = '#include <cassert>\n#include <optional>\n#include <string>\n#include "dlssnr/ReGameProfile.h"\n#include "dlssnr/ConfigLocation.h"\n#define LOG_INFO(...) ((void)0)\nstruct Opt { std::optional<bool> explicitValue; bool effective=false; bool has_value(){return explicitValue.has_value();} void set_volatile_value(bool b){effective=b;} bool value_or_default(){return explicitValue.value_or(effective);} };\nstruct Config { Opt NgxOnlyMode,SkipStreamlineHooks,RestoreComputeSignature,ExtendedStateRestore; static Config* Instance(){static Config c;return &c;} };\nstruct State {std::string gameExe; static State& Instance(){static State s;return s;} };\n'
MAIN = 'int main(){\n assert(DlssNr::ConfigLocation(L"C:/game/d3d12.dll",L"OptiScaler.ini")==std::filesystem::path(L"C:/game/OptiScaler.ini"));\n assert(DlssNr::ConfigLocation(L"C:/game/_STORAGE_/d3d12.dll",L"OptiScaler.ini")==std::filesystem::path(L"C:/game/OptiScaler.ini"));\n assert(DlssNr::ConfigLocation(L"C:/game/mods/d3d12.dll",L"OptiScaler.ini")==std::filesystem::path(L"C:/game/mods/OptiScaler.ini"));\n for(auto exe:{"PRAGMATA.exe","MonsterHunterWilds.exe","re9.exe","OnimushaWotS.exe"}){\n  *Config::Instance()={}; State::Instance().gameExe=exe;apply();\n  assert(Config::Instance()->NgxOnlyMode.value_or_default());assert(Config::Instance()->RestoreComputeSignature.value_or_default());assert(Config::Instance()->ExtendedStateRestore.value_or_default());\n }\n *Config::Instance()={};State::Instance().gameExe="unknown.exe";apply();assert(!Config::Instance()->ExtendedStateRestore.value_or_default());\n *Config::Instance()={};State::Instance().gameExe="PRAGMATA.exe";Config::Instance()->ExtendedStateRestore.explicitValue=false;apply();assert(!Config::Instance()->ExtendedStateRestore.value_or_default());\n *Config::Instance()={};Config::Instance()->NgxOnlyMode.explicitValue=false;apply();assert(!Config::Instance()->ExtendedStateRestore.value_or_default());\n}\n'
class ReDefaults(unittest.TestCase):
    @unittest.skipUnless(os.name == 'nt', 'Windows production config paths')
    def test_production_defaults_and_paths(self):
        src=(ROOT/'OptiScaler/dllmain.cpp').read_text(encoding='utf-8')
        start=src.index('    if (DlssNr::ReProfile::Known',src.index('static void CheckQuirks'))
        end=src.index('    LOG_INFO("Game\'s Exe:',start)
        with tempfile.TemporaryDirectory(prefix='d18-defaults-', dir=ROOT) as tmp:
            path=pathlib.Path(tmp)
            (path/'test.cpp').write_text(STUB+'void apply(){'+src[start:end]+'}\n'+MAIN,encoding='utf-8')
            vc=pathlib.Path(os.environ.get('D18_VCVARS',r'C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat'))
            self.assertTrue(vc.exists(), 'Set D18_VCVARS to vcvars64.bat')
            (path/'run.cmd').write_text('@echo off\ncall "'+str(vc)+'" >nul\ncl /nologo /std:c++20 /EHsc /I"'+str(ROOT/'OptiScaler')+'" test.cpp /Fe:test.exe\nif errorlevel 1 exit /b 1\ntest.exe\n')
            r=subprocess.run(['cmd','/c',str(path/'run.cmd')],cwd=tmp,capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    @unittest.skipUnless(os.name == 'nt', 'Windows D3D12 WARP')
    def test_native_submission(self):
        with tempfile.TemporaryDirectory(prefix='d18-warp-', dir=ROOT) as tmp:
            path=pathlib.Path(tmp)
            vc=pathlib.Path(os.environ.get('D18_VCVARS',r'C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat'))
            command='@echo off\ncall "'+str(vc)+'" >nul\ncl /nologo /std:c++20 /EHsc "'+str(ROOT/'tools/D18/test-native-submission.cpp')+'" /Fe:test.exe /link d3d12.lib dxgi.lib dxguid.lib\nif errorlevel 1 exit /b 1\ntest.exe\n'
            (path/'run.cmd').write_text(command)
            r=subprocess.run(['cmd','/c',str(path/'run.cmd')],cwd=tmp,capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)

STUB += '\n#include "dlssnr/SrQueuePolicy.h"\n'
MAIN = MAIN.replace('int main(){', 'int main(){\n using DlssNr::Submission::AcceptSrQueueHistory;\n assert(AcceptSrQueueHistory(true,false,false,100,1600));\n assert(!AcceptSrQueueHistory(true,false,false,100,1601));\n assert(AcceptSrQueueHistory(true,false,true,100,7700));\n assert(!AcceptSrQueueHistory(false,false,true,100,101));\n assert(!AcceptSrQueueHistory(true,true,true,100,101));\n assert(!AcceptSrQueueHistory(true,false,true,100,99));\n for(unsigned fps : {30u,120u}) {\n   unsigned long long ticks[64]{};\n   for(unsigned frame=0;frame<1024;++frame){\n     auto now=1000ull+frame*1000ull/fps;auto& previous=ticks[frame%64];\n     if(previous) assert(AcceptSrQueueHistory(true,false,true,previous,now));\n     previous=now;\n   }\n }\n')
