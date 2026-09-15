#pragma once
#include "Diagnostics.h"
#include <cstdint>
#include <cstdio>
#include <utility>

namespace DlssNr {
struct MemoryIdentity {
    uintptr_t device=0;
    uint64_t luid=0;
    bool operator==(const MemoryIdentity&) const = default;
};
struct MemorySample {
    uint64_t budget=0,usage=0;
    uint32_t status=0x80004005u;
    bool valid=false;
    bool Allows(uint64_t proposed,uint64_t reserve) const {
        return valid && budget>usage && proposed<=budget-usage && reserve<=budget-usage-proposed;
    }
};
// Identity is pinned by the API adapter. Budget values are never cached.
template<class Adapter> class MemoryAdapterCache {
    Adapter adapter{};
    MemoryIdentity identity{};
    uint64_t retryAt=0;
public:
    void Reset(){adapter=Adapter{};identity={};retryAt=0;}
    template<class Resolve,class Query>
    MemorySample Read(MemoryIdentity key,uint64_t now,Resolve resolve,Query query) {
        if(identity!=key){Reset();identity=key;}
        MemorySample sample;
        if(!adapter){
            if(now<retryAt)return sample;
            adapter=resolve();
            if(!adapter){retryAt=now+1000;return sample;}
        }
        sample=query(adapter);
        if(!sample.valid){adapter=Adapter{};retryAt=now+1000;}
        return sample;
    }
};
// A recorded clear is not initialized data until its actual submission completes.
template<class Token> struct CompletedInitialization {
    Token ticket{};
    bool Ready() const {return ticket && ticket->Complete();}
    void Recorded(Token value){ticket=std::move(value);}
    void Reset(){ticket={};}
};
// Shared fixed-rate diagnostic writer. Lifecycle only, at most 16 samples/second
// plus one overflow count at the next window; no new schema or per-frame file IO.
class MemoryDiagnostics {
    uint64_t window=0,suppressed=0;
    unsigned count=0;
public:
    void Reset(){window=suppressed=0;count=0;}
    template<class Emit>
    void Record(Diagnostics::Mode mode,uint64_t now,Diagnostics::Event event,
                const char* phase,MemorySample sample,uint64_t proposed,uint64_t reserve,
                bool admitted,Emit emit) {
        if(mode==Diagnostics::Mode::Off)return;
        if(now-window>=1000){
            if(suppressed){
                char reason[96]{};
                std::snprintf(reason,sizeof(reason),"overflow;n=%llu",(unsigned long long)suppressed);
                Diagnostics::Event overflow{};overflow.type="nr_memory";overflow.reason=reason;
                emit(mode,overflow);
            }
            window=now;count=0;suppressed=0;
        }
        if(count>=16){++suppressed;return;}
        ++count;
        char reason[96]{};
        std::snprintf(reason,sizeof(reason),"%s;b=%llx;u=%llx;p=%llx;r=%llx",phase,
            (unsigned long long)sample.budget,(unsigned long long)sample.usage,
            (unsigned long long)proposed,(unsigned long long)reserve);
        event.type="nr_memory";event.reason=reason;event.result=sample.status;
        event.flags|=(sample.valid?1u:0u)|(admitted?2u:0u);
        emit(mode,event);
    }
};
}
