#include <dlssnr/NrMemoryPolicy.h>
#include <cassert>
#include <memory>
#include <string>
#include <vector>
#include <limits>
struct Adapter {unsigned serial;};
struct Ticket {
    bool submitted=false;uint64_t completed=0;
    bool Complete() const {return submitted && completed!=UINT64_MAX && completed>=1;}
};
int main(){
    using namespace DlssNr;
    MemoryAdapterCache<std::shared_ptr<Adapter>> cache;
    unsigned resolutions=0,queries=0;bool resolveOk=true,queryOk=true;
    uint64_t usage=400;
    auto resolve=[&]{++resolutions;return resolveOk?std::make_shared<Adapter>(Adapter{resolutions}):nullptr;};
    auto query=[&](auto){++queries;return MemorySample{1000,usage,queryOk?0u:0x80004005u,queryOk};};
    MemoryIdentity id{1,10};
    auto s=cache.Read(id,1000,resolve,query);assert(s.Allows(300,300));assert(!s.Allows(301,300));
    usage=900;s=cache.Read(id,1001,resolve,query);assert(!s.Allows(100,1));assert(resolutions==1 && queries==2);
    // Same adapter LUID, new pinned device; same device address, new LUID.
    cache.Read({2,10},1002,resolve,query);cache.Read({2,20},1003,resolve,query);assert(resolutions==3);
    queryOk=false;assert(!cache.Read({2,20},1004,resolve,query).valid);
    queryOk=true;assert(!cache.Read({2,20},1005,resolve,query).valid);assert(resolutions==3);
    assert(cache.Read({2,20},2004,resolve,query).valid);assert(resolutions==4);
    cache.Reset();resolveOk=false;assert(!cache.Read(id,3000,resolve,query).valid);
    resolveOk=true;assert(!cache.Read(id,3999,resolve,query).valid);assert(cache.Read(id,4000,resolve,query).valid);
    MemorySample over{1,2,0,true};assert(!over.Allows(0,0));
    MemorySample huge{UINT64_MAX,0,0,true};assert(!huge.Allows(UINT64_MAX,1));assert(huge.Allows(UINT64_MAX,0));
    assert(!MemorySample{}.Allows(0,0));
    CompletedInitialization<std::shared_ptr<Ticket>> zero;
    assert(!zero.Ready());auto ticket=std::make_shared<Ticket>();zero.Recorded(ticket);
    assert(!zero.Ready());ticket->submitted=true;assert(!zero.Ready());ticket->completed=1;assert(zero.Ready());
    ticket->completed=UINT64_MAX;assert(!zero.Ready()); // device removed sentinel
    ticket->completed=1;zero.Reset();assert(!zero.Ready()); // resize/new resource
    auto discarded=std::make_shared<Ticket>();zero.Recorded(discarded);discarded->completed=1;
    assert(!zero.Ready()); // a CPU recording or fence value alone never authorizes reuse
    MemoryDiagnostics diag;unsigned emitted=0;std::vector<std::string> reasons;
    auto emit=[&](auto,const Diagnostics::Event& e){++emitted;reasons.emplace_back(e.reason);
        assert(reasons.back().size()<96);assert(e.fenceTarget==0 && e.fenceCompleted==0);};
    Diagnostics::Event event{};event.flags=2<<8;event.queue=10;event.commandList=1;
    diag.Record(Diagnostics::Mode::Off,1000,event,"before",s,0,0,false,emit);assert(!emitted);
    for(int i=0;i<20;++i)diag.Record(Diagnostics::Mode::Summary,1000,event,"before",huge,UINT64_MAX,UINT64_MAX,false,emit);
    assert(emitted==16);diag.Record(Diagnostics::Mode::Summary,2000,event,"before",s,0,0,false,emit);
    assert(emitted==18 && reasons[16]=="overflow;n=4");
    diag.Reset();diag.Record(Diagnostics::Mode::Trace,2001,event,"created",s,0,0,false,emit);assert(emitted==19);
    // Emit real production encoder records for a binary parser round trip.
    diag.Reset();event.featureGeneration=7;event.width=3840;event.height=2160;
    event.networkWidth=1920;event.networkHeight=1080;event.ratio=.5f;
    auto output=[](auto,const Diagnostics::Event& e){std::printf("MEM|%llu|%llu|%llu|%u|%u|%s\n",
        (unsigned long long)e.featureGeneration,(unsigned long long)e.queue,
        (unsigned long long)e.commandList,e.flags,e.result,e.reason);};
    diag.Record(Diagnostics::Mode::Summary,3000,event,"before",{10000,1000,0,true},500,700,true,output);
    diag.Record(Diagnostics::Mode::Summary,3001,event,"created",{10000,1400,0,true},0,0,false,output);
    diag.Record(Diagnostics::Mode::Summary,3002,event,"gpu_ready",{10000,1300,0,true},0,0,false,output);
    diag.Record(Diagnostics::Mode::Summary,3003,event,"scratch",{},100,200,false,output);
    std::puts("PASS cache identity/live budget/failure backoff/overflow; zero initialization requires completed submission; CPU only");
}
