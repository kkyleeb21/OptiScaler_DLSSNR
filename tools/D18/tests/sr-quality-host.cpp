#include "dlssnr/SrQualityMode.h"
#include <cassert>
#include <cstdio>
int main(){
    using namespace DlssNr;
    struct Case{unsigned w,h;SrQualityMode mode;bool standard;};
    const Case cases[]={
        {3840,2160,SrQualityMode::DLAA,true},
        {2880,1620,SrQualityMode::Quality,false},
        {2560,1440,SrQualityMode::Quality,true},
        {2227,1253,SrQualityMode::Balanced,true},
        {1920,1080,SrQualityMode::Performance,true},
        {1280,720,SrQualityMode::UltraPerformance,true},
        {3072,1728,SrQualityMode::Quality,false},
        {2112,1188,SrQualityMode::Balanced,false},
        {1728,972,SrQualityMode::Performance,false}};
    for(auto c:cases){auto q=SelectSrQuality({c.w,c.h,3840,2160});assert(q.mode==c.mode&&q.standard==c.standard);}
    assert(SelectSrQuality({0,0,3840,2160}).mode==SrQualityMode::Invalid);
    assert(SelectSrQuality({3840,2160,1920,1080}).mode==SrQualityMode::Invalid);
    assert(SelectSrQuality({2880,1080,3840,2160}).mode==SrQualityMode::Invalid);
    assert(SelectSrQuality({8192,8192,8192,8192}).mode==SrQualityMode::Invalid);
    assert(SelectSrQuality({1707,960,2560,1440}).standard);
    assert(!SelectSrQuality({2550,1434,3840,2160}).standard);
    puts("PASS: 15 quality policy cases; dimensions and labels remain distinct");
}
