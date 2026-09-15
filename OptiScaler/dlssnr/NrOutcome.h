#pragma once
#include "Diagnostics.h"
#include "NrContinuity.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace DlssNr {
struct NrOutcome {
    uint64_t attempt=0, source=0;
    uint32_t width=0, height=0, requested=1, ready=0, evaluated=0, composed=0, resetMask=0;
    bool shared=false, requestedShared=false;
    HistoryReset history=HistoryReset::None;
    char reason[29]="pre_model_return";
    void Reason(const char* value) { std::snprintf(reason,sizeof(reason),"%s",value); }
    bool Same(const NrOutcome& b) const {
        return source==b.source && width==b.width && height==b.height && requested==b.requested &&
            ready==b.ready && evaluated==b.evaluated && composed==b.composed && resetMask==b.resetMask &&
            shared==b.shared && requestedShared==b.requestedShared && history==b.history && std::strcmp(reason,b.reason)==0;
    }
};
// Summary: fixed 16 signatures per one-second window, including alternating success/fallback.
// No allocation, GPU wait, pixel capture or per-frame disk growth. Overflow is explicit.
// Trace emits individual outcomes instead. The old binary ring layout remains unchanged.
class NrOutcomeSummary {
    struct Bucket { NrOutcome value; uint32_t count=0; };
    std::array<Bucket,16> buckets_{};
    uint32_t overflow_=0;
    uint64_t start_=0;
    Diagnostics::Mode mode_=Diagnostics::Mode::Off;
    template<class Emit> void Write(const NrOutcome& o,uint32_t count,Emit emit) {
        Diagnostics::Event e{}; e.type="nr_outcome";e.frame=o.attempt;
        e.width=o.width;e.height=o.height;e.result=o.composed?1:0;
        e.flags=(o.requested<<12)|(o.ready<<16)|(o.evaluated<<20)|(o.composed<<24)|
            (o.resetMask<<4)|(o.shared?2u:0u)|(o.requestedShared?4u:0u)|(o.resetMask?1u:0u);
        char reason[96]{};
        std::snprintf(reason,sizeof(reason),"%s;n=%u;s=%llu;h=%u",o.reason,count,
            static_cast<unsigned long long>(o.source),static_cast<unsigned>(o.history));
        e.reason=reason;emit(mode_,e);
    }
public:
    template<class Emit> void Flush(Emit emit) {
        for(auto& b:buckets_) {if(b.count)Write(b.value,b.count,emit);b.count=0;}
        if(overflow_) {NrOutcome o{};o.Reason("summary_overflow");Write(o,overflow_,emit);overflow_=0;}
    }
    template<class Emit> void Mode(Diagnostics::Mode mode,uint64_t now,Emit emit) {
        if(mode!=mode_) {Flush(emit);mode_=mode;start_=now;}
    }
    template<class Emit> void Add(const NrOutcome& o,Diagnostics::Mode mode,uint64_t now,Emit emit) {
        Mode(mode,now,emit);
        if(mode==Diagnostics::Mode::Off)return;
        if(mode==Diagnostics::Mode::Trace){Write(o,1,emit);return;}
        if(now-start_>=1000){Flush(emit);start_=now;}
        for(auto& b:buckets_)if(b.count && b.value.Same(o)) {
            ++b.count;b.value.attempt=o.attempt;return;
        }
        for(auto& b:buckets_)if(!b.count){b.value=o;b.count=1;return;}
        ++overflow_;
    }
};
}
