#pragma once
#include <cstdint>
namespace DlssNr {
struct SrResolutionContract {
    unsigned inputWidth=0,inputHeight=0,outputWidth=0,outputHeight=0;
    bool Valid() const {
        const auto valid=[](unsigned w,unsigned h){return w>=64 && h>=64 && w<=8192 && h<=8192 && uint64_t(w)*h<=16777216;};
        if(!valid(inputWidth,inputHeight)||!valid(outputWidth,outputHeight)||outputWidth<inputWidth||outputHeight<inputHeight)return false;
        const uint64_t a=uint64_t(inputWidth)*outputHeight,b=uint64_t(inputHeight)*outputWidth;
        // Allow one-pixel integer rounding, not arbitrary cropping/stretching.
        return (a>b?a-b:b-a)<=uint64_t(outputWidth)+outputHeight;
    }
    bool Upscaling() const {return outputWidth!=inputWidth || outputHeight!=inputHeight;}
};
}
