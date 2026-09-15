#pragma once
#include "SrResolutionContract.h"
#include <cmath>

namespace DlssNr {
// Policy for adapters that observe dimensions but receive no engine quality enum.
// Does not resize inputs, infer dynamic-resolution bounds, or select an NGX model.
enum class SrQualityMode { Invalid, DLAA, Quality, Balanced, Performance, UltraPerformance };
struct SrQualitySelection {
    SrQualityMode mode=SrQualityMode::Invalid;
    double ratio=0;
    bool standard=false;
};
inline const char* SrQualityName(SrQualityMode mode) {
    switch(mode) {
    case SrQualityMode::DLAA:return "DLAA";
    case SrQualityMode::Quality:return "Quality";
    case SrQualityMode::Balanced:return "Balanced";
    case SrQualityMode::Performance:return "Performance";
    case SrQualityMode::UltraPerformance:return "Ultra Performance";
    default:return "Waiting for input dimensions";
    }
}
inline SrQualitySelection SelectSrQuality(const SrResolutionContract& r) {
    if(!r.Valid())return {};
    if(!r.Upscaling())return {SrQualityMode::DLAA,1.0,true};
    const double x=double(r.inputWidth)/r.outputWidth,y=double(r.inputHeight)/r.outputHeight;
    const double ratio=(x+y)*0.5;
    constexpr double ratios[]={2.0/3.0,0.58,0.5,1.0/3.0};
    constexpr SrQualityMode modes[]={SrQualityMode::Quality,SrQualityMode::Balanced,SrQualityMode::Performance,SrQualityMode::UltraPerformance};
    unsigned best=0;
    for(unsigned i=1;i<4;++i)if(std::abs(ratio-ratios[i])<std::abs(ratio-ratios[best]))best=i;
    // Standard labels allow only integer dimension rounding (one input pixel).
    const bool standard=std::abs(x-ratios[best])*r.outputWidth<=1.01 && std::abs(y-ratios[best])*r.outputHeight<=1.01;
    return {modes[best],ratio,standard};
}
}
