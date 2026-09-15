#pragma once
#include <cstdint>

namespace DlssNr {
// Positive DXGI statuses and DO_NOT_WAIT backpressure are not device failures.
// Runtime queries may recover during settings changes; persistently invalid ones
// receive a bounded retry window. No resource may be reused based on this policy alone.
struct NativeFgPresentPolicy {
    enum class Decision { Ready, PresentRetry, RuntimeRetry, PresentFailed, RuntimeExpired };
    bool waiting=false, runtimeWaiting=false;
    uint64_t runtimeSince=0;
    Decision Observe(uint64_t now, uint32_t hr, int query, uint32_t status) {
        if((hr & 0x80000000u) && hr!=0x887A000Au)return Decision::PresentFailed;
        if(hr!=0){waiting=true;runtimeWaiting=false;return Decision::PresentRetry;}
        if(query!=0 || status!=0){
            waiting=true;
            if(!runtimeWaiting){runtimeSince=now;runtimeWaiting=true;}
            return now-runtimeSince>=2000?Decision::RuntimeExpired:Decision::RuntimeRetry;
        }
        waiting=false;runtimeWaiting=false;return Decision::Ready;
    }
};
}
