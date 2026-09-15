#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace DlssNr::Multipass {
constexpr unsigned MaxPasses=4;
// At most 12 shader dispatches per four-pass shared frame. Keep five complete
// recordings without CPU waits; every slot still requires its actual fence.
constexpr unsigned DescriptorSlots=64;
struct Tuning {
    bool scaling=true,autoMask=true;
    float ratio=.5f,intensity=1,structure=1,tone=1,skin=-1;
    uint32_t preset=0,style=0;
    bool operator==(const Tuning&)const=default;
};
inline float Bounded(float value,float fallback,float low,float high) {
    return std::isfinite(value)?std::clamp(value,low,high):fallback;
}
inline Tuning Sanitize(Tuning t) {
    t.ratio=Bounded(t.ratio,.5f,.5f,1);t.intensity=Bounded(t.intensity,1,0,2);
    t.structure=Bounded(t.structure,1,0,2);t.tone=Bounded(t.tone,1,0,2);t.skin=Bounded(t.skin,-1,-1,2);
    t.preset=(std::min)(t.preset,3u);t.style=(std::min)(t.style,2u);return t;
}
inline unsigned Count(unsigned requested,bool high,bool supported=true){return high||!supported?1u:std::clamp(requested,1u,MaxPasses);}
inline uint32_t Network(uint32_t size,float ratio,uint32_t alignment) {
    return (std::max)(alignment,uint32_t(size*ratio+.5f)&~(alignment-1));
}
inline unsigned Slots(unsigned count){return count>1?3+2*count:4;}
// Conservative admission headroom, not a bound on an opaque model's allocations.
inline uint64_t Reserve(uint32_t w,uint32_t h,const Tuning& t){return uint64_t(Network(w,t.scaling?t.ratio:1,16))*Network(h,t.scaling?t.ratio:1,8)*384+256ull*1024*1024;}
// Existing ring schema: only mp_* events interpret these high bits.
inline uint32_t Flags(unsigned pass,unsigned requested,unsigned ready,unsigned recorded,bool reset=false){return (pass<<8)|(requested<<12)|(ready<<16)|(recorded<<20)|(reset?1:0);}
}
