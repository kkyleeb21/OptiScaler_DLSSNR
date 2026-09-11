#include "dlssnr/AllocationBatch.h"
#include <cassert>
#include <cstdio>

struct Resource {
    static inline int live=0;
    Resource(){++live;}
    void Release(){--live;delete this;}
};

// Fail each position, including after partial success; a live resource from an earlier generation
// is deliberately present and must not be released by rollback. Retry succeeds on the same slots.
int main() {
    unsigned cases=0;
    for (int count : {7,32}) for(int fail=0;fail<count;++fail) for(bool hot : {false,true}) {
        Resource* slots[32]{};
        Resource* inFlight = hot ? new Resource : nullptr;
        const int baseline=Resource::live;
        {
            DlssNr::AllocationBatch<Resource> batch;
            if(hot) assert(batch.Ensure(inFlight, []()->Resource*{assert(false);return nullptr;}));
            bool ready=true;
            for(int i=0;i<count && ready;++i)
                ready=batch.Ensure(slots[i],[&]()->Resource*{return i==fail?nullptr:new Resource;});
            assert(!ready);
            // No feature creation / dispatch is permitted before this commit point.
            if(ready) batch.Commit();
        }
        assert(Resource::live==baseline);
        for(auto slot:slots) assert(slot==nullptr);
        if(hot) assert(inFlight!=nullptr);
        {
            DlssNr::AllocationBatch<Resource> retry;
            for(int i=0;i<count;++i) assert(retry.Ensure(slots[i],[]{return new Resource;}));
            retry.Commit();
        }
        assert(Resource::live==baseline+count);
        for(auto slot:slots) if(slot) slot->Release();
        if(inFlight) inFlight->Release();
        assert(Resource::live==0);++cases;
    }
    std::printf("PASS %u injected allocation failures: rollback, existing ownership, retry; no leaked resources\n",cases);
}
