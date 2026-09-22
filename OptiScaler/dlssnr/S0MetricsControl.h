#pragma once
#include <array>
#include <cstdint>

// CPU-only bounded policy. The owner serializes this object with g_vkMutex.
namespace S0MetricsPolicy {
struct Arm {
    unsigned id=0,warmup=0,attempts=0,saved=0,pending=0;
    uint64_t begin=0,nextSample=0;
    bool active=false,closing=false,terminal=false;
    const char* terminalPhase="cancelled";
    const char* reason="none";
};
struct Control {
    using Observer=void(*)(const Arm&,const char*,const char*);
    std::array<Arm,4> arms{};
    unsigned count=0,totalSaved=0,totalPending=0;
    bool lastMarker=false;
    Observer observer=nullptr;
    void Emit(Arm& a,const char* phase,const char* reason){if(observer)observer(a,phase,reason);}
    Arm* Current(){return count?&arms[count-1]:nullptr;}
    void FinishIfDrained(Arm& a){if(a.closing&&!a.pending&&!a.terminal){a.terminal=true;Emit(a,a.terminalPhase,a.reason);}}
    void Stop(Arm& a,const char* phase,const char* reason){
        if(!a.active||a.closing)return;
        a.active=false;a.closing=true;a.terminalPhase=phase;a.reason=reason;
        if(a.pending)Emit(a,"draining",reason);FinishIfDrained(a);
    }
    void Gate(uint64_t now,bool diagnostics,bool marker,bool identity){
        const bool rising=marker&&!lastMarker;lastMarker=marker;
        if(auto* a=Current();a&&a->active){
            if(!diagnostics||!marker||!identity)Stop(*a,"cancelled",!diagnostics?"diagnostics_off":!identity?"identity_unavailable":"marker_removed");
            else if(now-a->begin>=30000)Stop(*a,"timed_out","deadline_30s");
        }
        if(rising&&diagnostics&&identity&&count<arms.size()&&totalSaved<128){
            auto& a=arms[count];a=Arm{};a.id=++count;a.begin=now;a.active=true;
            Emit(a,"warming","marker_rising_edge");
        }
    }
    unsigned Reserve(uint64_t now,bool qualified){
        auto* a=Current();if(!a||!a->active||!qualified)return 0;
        if(now-a->begin>=30000){Stop(*a,"timed_out","deadline_30s");return 0;}
        if(a->warmup<30){if(++a->warmup==30)Emit(*a,"recording","warmup_30_qualified_frames");return 0;}
        if(a->attempts>=256){Stop(*a,"failed","attempt_budget_256");return 0;}
        if(totalPending>=4||totalSaved+totalPending>=128||a->saved+a->pending>=32||now<a->nextSample)return 0;
        ++a->attempts;++a->pending;++totalPending;a->nextSample=now+100;return a->id;
    }
    void Result(unsigned arm,bool complete){
        if(!arm||arm>count)return;auto& a=arms[arm-1];
        if(!a.pending||!totalPending)return;--a.pending;--totalPending;
        if(complete){++a.saved;++totalSaved;Emit(a,a.closing?"draining":"recording","sample_complete");}
        if(a.active&&a.saved==32)Stop(a,"complete","target_32_completed");
        if(a.active&&a.attempts>=256&&!a.pending)Stop(a,"failed","attempt_budget_256");
        FinishIfDrained(a);
    }
};
}
