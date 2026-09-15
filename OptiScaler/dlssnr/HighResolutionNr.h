#pragma once
#include <cmath>
#include <cstdint>
#include <iterator>
#include "DlssNrAbi.h"
namespace DlssNr::HighResolution {
struct Size { uint32_t width=0,height=0; };
template<class Slots> bool SlotsReady(const Slots& slots,uint32_t first,uint32_t count) {
    const auto size=std::size(slots);
    if(first>=size || count>size)return false;
    for(uint32_t i=0;i<count;++i){const auto& token=slots[(first+i)%size];if(token && !token->Complete())return false;}
    return true;
}
inline bool Plan(const DlssNrAbi::Rect& base,float scale,Size& work) {
    if (!base.width || !base.height || !std::isfinite(scale) || (scale!=1.25f && scale!=1.5f)) return false;
    const uint64_t w=(uint64_t(std::ceil(double(base.width)*scale))+15)&~uint64_t(15);
    const uint64_t h=(uint64_t(std::ceil(double(base.height)*scale))+7)&~uint64_t(7);
    if(w>16384 || h>16384 || w>uint64_t(base.width)*2 || h>uint64_t(base.height)*2) return false;
    work={uint32_t(w),uint32_t(h)};return true;
}
inline uint64_t ColorBytes(uint32_t aw,uint32_t ah,Size work,uint32_t pixelBytes) {
    return (uint64_t(aw)*ah*3+uint64_t(work.width)*work.height*2)*pixelBytes;
}
// A conservative admission check, not a prediction of the opaque runtime workspace.
inline bool Budget(uint64_t liveAndRetired,uint64_t proposed,uint64_t available,Size work) {
    constexpr uint64_t limit=2ull*1024*1024*1024;
    if(!work.width || !work.height || work.width>16384 || work.height>16384 ||
       liveAndRetired>limit || proposed>limit-liveAndRetired) return false;
    const uint64_t reserve=uint64_t(work.width)*work.height*64+256ull*1024*1024;
    return available>=proposed+reserve;
}
}
