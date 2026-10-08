#pragma once
#include <array>
#include <cstdint>
#include <cstring>

// Metadata only; portable so the synthetic test runs this exact accumulator.
namespace DlssNr::Diagnostics {
struct ChainWindow {
    char route[80]{};
    std::array<uint64_t, 4> first{}, last{};
    uint64_t count=0, failures=0, sum=0, critical=0;
    uint32_t result=0;
    bool seen=false, changed=false, active=false;
};
struct ChainAccumulator {
    std::array<ChainWindow,192> slots{};
    uint64_t begin=0, second=0, overflow=0, transitionsSuppressed=0;
    uint32_t requested=15, submitted=15, transitions=0;
    bool started=false;
    template<class Emit> void Flush(uint64_t now, Emit emit) {
        if (!started) return;
        for(auto& s:slots) if(s.seen) {
            if(s.active && !s.count) {
                if(transitions++<16)emit(s,now,now,requested,submitted,true);else ++transitionsSuppressed;
            }
            s.active=s.count!=0;
            emit(s,begin,now,requested,submitted,false);
            s.count=s.failures=s.sum=0;s.changed=false;s.first=s.last;
        }
        ChainWindow limits{};std::strcpy(limits.route,"collector.limits");limits.seen=true;limits.count=1;
        limits.first=limits.last={overflow,transitionsSuppressed,0,0};
        emit(limits,begin,now,requested,submitted,false);
        begin=now;
    }
    template<class Emit> void Tick(uint64_t now, Emit emit) {
        if(!started){started=true;begin=now;second=now/1000;}
        if(now/1000!=second){Flush(now,emit);second=now/1000;transitions=0;}
    }
    template<class Emit> void Pair(uint64_t now,uint32_t req,uint32_t sub,Emit emit) {
        Tick(now,emit);
        if(req!=requested || sub!=submitted){Flush(now,emit);requested=req;submitted=sub;}
    }
    template<class Emit> void Add(uint64_t now,const char* route,std::array<uint64_t,4> value,
                                 uint32_t result,bool failed,uint64_t sum,
                                 bool immediate,uint64_t critical,Emit emit) {
        Tick(now,emit);
        ChainWindow* slot=nullptr;
        for(auto& s:slots) if(s.seen && std::strcmp(s.route,route)==0){slot=&s;break;}
        if(!slot) for(auto& s:slots) if(!s.seen){slot=&s;break;}
        if(!slot){++overflow;return;}
        auto& s=*slot;
        const bool change=!s.seen || s.last!=value || s.result!=result;
        const bool urgent=immediate && (!s.seen || s.critical!=critical || s.result!=result);
        if(!s.seen){std::strncpy(s.route,route,sizeof(s.route)-1);s.seen=true;}
        if(!s.count)s.first=value;
        ++s.count;s.failures+=failed;s.sum+=sum;s.changed|=change;
        s.last=value;s.result=result;s.critical=critical;
        if(urgent){if(transitions++<16)emit(s,now,now,requested,submitted,true);else ++transitionsSuppressed;}
    }
};
}
