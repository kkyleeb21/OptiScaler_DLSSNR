"""CPU-only regression of the production grade synchronization entry points."""
from pathlib import Path
import argparse, subprocess

ap = argparse.ArgumentParser()
for name in ['source', 'build', 'report']:
    ap.add_argument('--'+name, required=True, type=Path)
a = ap.parse_args()
out = a.build/'cpu-grade-sync'
out.mkdir(exist_ok=True)
backend = (a.source/'OptiScaler/dlssnr/NrGradeTable.cpp').read_text(encoding='utf-8')
globals = backend[backend.index('Session session;'):backend.index('struct Memory {')]
functions = backend[backend.index('std::unique_lock<std::mutex> BeforeEvaluate('):backend.rindex('} // namespace')]
vk = (a.source/'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp').read_text(encoding='utf-8')
def function(text, name):
    start=text.index(name);end=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
snapshot=vk[vk.index('struct VkUiSnapshot {'):vk.index('std::vector<std::unique_ptr<VkState>>')]
readers='\n'.join(function(vk,n) for n in ['bool IsRunningVk(', 'std::optional<double> LastGpuTimeVk(', 'const char* GpuTimingStatusVk(', 'DlssNrNative::AdvancedStatus ReadAdvancedStatusVk('])
assert 'g_vkMutex' not in readers
shutdown=function(vk,'void ShutdownVk(')
assert shutdown.index('lock(g_vkMutex)')<shutdown.index('Grade::Shutdown()')
# Retain the real grade lock and counter statements at both DX12 call sites;
# replace only the GPU call/arguments with a CPU stand-in.
callscopes=[]
for path,name in [('OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp','const int result = [&] {'),('OptiScaler/shaders/dlssnr/Multipass_Execute_Dx12.inl','const int passResult = [&] {')]:
    text=(a.source/path).read_text(encoding='utf-8')
    body=function(text,name)
    assert body.index('BeforeEvaluate')<body.index('++outcome.value.modelCalls')<body.index('return g_nr.evaluate')
    prefix=body[body.index('{')+1:body.index('return g_nr.evaluate')]
    callscopes.append('[&] {'+prefix+'return g_nr.evaluate();}()')
menu=(a.source/'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')
assert 'Grade::RestoreIfDisabled' not in menu and 'Grade::BeforeEvaluate' not in menu
code = r'''
#include <dlssnr/NrGradeTable.h>
#include <dlssnr/PublishedSnapshot.h>
#include <dlssnr/NativeControlAbi.h>
#include <dlssnr/NrOutcome.h>
#include <optional>
#include <string>
#include <atomic>
#include <future>
#include <cassert>
#include <cstdio>
using namespace DlssNr::Grade;
struct Option { bool v; bool value_or_default() const {return v;} };
class Config {public: Option DlssNrGradeEnabled{true},DlssNrEnabled{true}; Values values=Preset(1);};
namespace DlssNr::Grade {
Values ReadValues(const Config& c){return c.values;}
''' + globals + r'''
struct Memory {
    const wchar_t* path=nullptr; Table table=Original(); unsigned stores=0;
    uintptr_t Current(){return 1;} bool Pin(uintptr_t){return true;}
    void Unpin(uintptr_t){} bool Identity(uintptr_t){return true;}
    bool Read(uintptr_t,Table& t){t=table;return true;}
    bool Write(uintptr_t,const Table& t){table=t;++stores;return true;}
} memory;
''' + functions + r'''
}
static uint64_t fakeTick=100;
static uint64_t GetTickCount64(){return fakeTick;}
namespace DlssNr {
struct Timing {std::optional<double> Value(){return 3.;}uint64_t lastRead=100;bool unavailable=false;};
std::mutex g_vkMutex;std::atomic<bool> nativeExposureReady{false};
void RetireCurrentState(const char*){}
void CollectRetired(){}
struct VkFixture {bool active=true,advancedMode=false,failed=false;void* feature=(void*)1;Timing* timing=nullptr;DlssNrNative::AdvancedStatus advancedStatus{};} g_vk;
@SNAPSHOT@
@READERS@
@SHUTDOWN@
}
struct CpuApi {std::wstring gradeRuntimePath=L"fixture";unsigned calls=0,failAt=0;int evaluate(){return ++calls==failAt?-1:1;}} g_nr;
static void counts(Config& cfg){
    struct {DlssNr::NrOutcome value;} outcome;
    const float evaluateLocalTone=1;
    for(unsigned fail:{0u,1u,2u,3u,4u}){
        outcome={};g_nr.calls=0;g_nr.failAt=fail;
        int result=@FIRST@;
        if(result==1)++outcome.value.evaluated;
        for(unsigned i=1;result==1 && i<4;++i){result=@ADDITIONAL@;if(result==1)++outcome.value.evaluated;}
        assert(outcome.value.modelCalls==(fail?fail:4) && outcome.value.evaluated==(fail?fail-1:4));
        DlssNr::NrOutcomeSummary summary;
        summary.Add(outcome.value,DlssNr::Diagnostics::Mode::Trace,0,[&](auto,const auto& event){
            assert((event.flags>>28)==outcome.value.modelCalls);
        });
    }
    puts("PASS G7 production call prefixes: four calls on success; failure on each pass counted as an attempted call; outcome diagnostic flags agree");
}
int main(){
    Config c;
    DlssNr::Timing timing;DlssNr::g_vk.timing=&timing;
    {DlssNr::PublishVkUiOnExit publish;}
    // Readers use only a completed CPU snapshot, even while the backend is busy.
    auto vkMenu=std::async(std::launch::async,[]{
        assert(DlssNr::IsRunningVk() && DlssNr::LastGpuTimeVk()==3.);
        assert(std::string(DlssNr::GpuTimingStatusVk())=="timing pending");
        (void)DlssNr::ReadAdvancedStatusVk();
    });
    assert(vkMenu.wait_for(std::chrono::seconds(2))==std::future_status::ready);vkMenu.get();
    fakeTick=5101;assert(!DlssNr::LastGpuTimeVk());fakeTick=100;
    std::promise<void> entered,release;auto releaseFuture=release.get_future();
    // Stand-in Evaluate reads both fields while retaining the actual production lock.
    auto evaluator=std::async(std::launch::async,[&]{
        std::unique_lock backend(DlssNr::g_vkMutex);
        auto gradeLock=BeforeEvaluate(c,1);
        const auto exposure=memory.table[0].values[2];entered.set_value();releaseFuture.wait();
        assert(memory.table[0].values[2]==exposure && memory.table[0].values[5]==-.1f);
    });
    entered.get_future().wait();
    const auto stores=memory.stores;
    // Menu request completes while Evaluate remains blocked, and performs no write.
    auto menu=std::async(std::launch::async,[]{
        RequestUpdate();assert(DlssNr::IsRunningVk() && DlssNr::LastGpuTimeVk()==3.);
        (void)DlssNr::GpuTimingStatusVk();(void)DlssNr::ReadAdvancedStatusVk();
    });
    assert(menu.wait_for(std::chrono::seconds(2))==std::future_status::ready);menu.get();
    assert(memory.stores==stores && CurrentStatus()==Status::Waiting);
    std::promise<void> trying;auto tried=trying.get_future();
    auto next=std::async(std::launch::async,[&]{trying.set_value();auto lock=BeforeEvaluate(c,.5f);});
    tried.wait();assert(next.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
    release.set_value();evaluator.get();next.get();
    assert(memory.table[0].values[2]==-.2f && CurrentStatus()==Status::Applied);
    // Close request is applied at next Evaluate entry.
    c.DlssNrGradeEnabled.v=false;RequestUpdate();assert(session.active);
    {auto lock=BeforeEvaluate(c,1);assert(!session.active && !session.base);}
    assert(std::memcmp(memory.table.data(),ExpectedWords.data(),sizeof(Table))==0);
    // No further Evaluate: NR-disabled early entry must restore even with grade enabled.
    c.DlssNrGradeEnabled.v=true;{auto lock=BeforeEvaluate(c,1);}
    c.DlssNrEnabled.v=false;RestoreIfDisabled(c);assert(!session.active && !session.base);
    // Grade off, Shutdown and process-exit fallback are also actual production paths.
    c.DlssNrEnabled.v=true;{auto lock=BeforeEvaluate(c,1);}
    c.DlssNrGradeEnabled.v=false;RestoreIfDisabled(c);assert(!session.active);
    c.DlssNrGradeEnabled.v=true;{auto lock=BeforeEvaluate(c,1);}Shutdown();assert(!session.active);
    {auto lock=BeforeEvaluate(c,1);}Shutdown(true);assert(!session.active);
    assert(std::memcmp(memory.table.data(),ExpectedWords.data(),sizeof(Table))==0);
    Shutdown();counts(c);
    // Regression: an Evaluate already owning the Vulkan backend must finish before
    // Shutdown restores, so it cannot reapply a table after Shutdown's restoration.
    std::promise<void> inVk,leaveVk;auto leaving=leaveVk.get_future();
    auto vkEvaluate=std::async(std::launch::async,[&]{
        std::unique_lock backend(DlssNr::g_vkMutex);auto gradeLock=BeforeEvaluate(c,1);
        inVk.set_value();leaving.wait();
    });inVk.get_future().wait();
    auto shutdownVk=std::async(std::launch::async,[]{DlssNr::ShutdownVk();});
    assert(shutdownVk.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
    leaveVk.set_value();vkEvaluate.get();shutdownVk.get();
    assert(!session.active && !session.base && std::memcmp(memory.table.data(),ExpectedWords.data(),sizeof(Table))==0);
    puts("PASS G3 Vulkan completed-state menu reads during Evaluate; backend -> grade Shutdown order prevents reapplication after restore");
    puts("PASS G3 CPU: menu request returns while Evaluate holds lock; update/evaluate serialization; close at next entry; no-Evaluate NR-off restore; grade-off, Shutdown, process-exit restore");
}
'''
code=code.replace('@SNAPSHOT@',snapshot).replace('@READERS@',readers).replace('@SHUTDOWN@',shutdown).replace('@FIRST@',callscopes[0]).replace('@ADDITIONAL@',callscopes[1])
(out/'sync.cpp').write_text(code, encoding='utf-8')
cmd = '@echo off\ncall "C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\ncl /nologo /std:c++20 /EHsc /W4 /WX /fp:precise /I"'+str(a.source/'OptiScaler')+'" sync.cpp /Fe:sync.exe\nif errorlevel 1 exit /b 1\n.\\sync.exe\n'
(out/'run.cmd').write_text(cmd, encoding='utf-8')
r = subprocess.run(['cmd','/d','/c',str(out/'run.cmd')],cwd=out,capture_output=True,text=True)
(a.report/'cpu-grade-sync.log').write_text(r.stdout+r.stderr,encoding='utf-8')
print(r.stdout+r.stderr)
raise SystemExit(r.returncode)
