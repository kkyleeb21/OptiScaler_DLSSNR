#include "../../OptiScaler/dlssnr/RetirementPolicy.h"
#include <cassert>
#include <vector>
#include <cstdio>
struct Batch { int id; std::vector<uint64_t> completed; uint64_t target; bool fencePresent=true; };
int main() {
    using namespace DlssNr::Retirement;
    assert(FenceReached(4,4) && FenceReached(5,4) && !FenceReached(3,4));
    assert(!FenceReached(UINT64_MAX,4));
    std::vector<Batch> pending{{1,{3},4},{2,{4,4},4},{3,{4,2},4},{4,{UINT64_MAX},4},{5,{4},4,false}};
    std::vector<int> freed;
    auto ready=[](const Batch& b){if(!b.fencePresent)return false;for(auto v:b.completed)if(!FenceReached(v,b.target))return false;return true;};
    auto release=[&](Batch& b){freed.push_back(b.id);};
    assert(CollectReady(pending,ready,release)==1 && freed==std::vector<int>{2});
    assert(pending.size()==4 && !CanRetire(pending.size()));
    pending[0].completed[0]=4;
    // Same collector continues to work when NR is disabled or replacement creation failed.
    assert(CollectReady(pending,ready,release)==1 && CanRetire(pending.size()));
    assert(CollectReady(pending,ready,release)==0);
    pending[0].completed[1]=4;
    assert(CollectReady(pending,ready,release)==1);
    assert(pending.size()==2 && pending[0].id==4 && pending[1].id==5);
    assert(freed==std::vector<int>({2,1,3}));
    for(int i=0;i<100;++i){
        std::vector<Batch> delayed;
        while(CanRetire(delayed.size()))delayed.push_back({i,{0},1});
        assert(delayed.size()==4 && CollectReady(delayed,ready,release)==0);
        for(auto& b:delayed)b.completed[0]=1;
        assert(CollectReady(delayed,ready,release)==4 && delayed.empty());
    }
    puts("PASS: pending/completed/multiple/missing/removed fences; out-of-order collection; bounded rebuild and recovery; no double release");
}
