#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <sstream>
#include <cmath>
#include <iomanip>
#include <limits>

namespace capture {
struct GuideRegion { uint32_t x=0,y=0,width=0,height=0; };
// Map a central, bounded output ROI to each guide's actual valid rectangle.
inline GuideRegion GuideCrop(GuideRegion valid,uint32_t outputWidth,uint32_t outputHeight) {
    if(!valid.width||!valid.height||!outputWidth||!outputHeight)return {};
    const uint32_t rw=(std::min)(outputWidth,512u),rh=(std::min)(outputHeight,512u);
    const uint32_t ox=(outputWidth-rw)/2,oy=(outputHeight-rh)/2;
    const uint32_t x=uint32_t(uint64_t(ox)*valid.width/outputWidth),y=uint32_t(uint64_t(oy)*valid.height/outputHeight);
    const uint32_t endX=uint32_t((uint64_t(ox+rw)*valid.width+outputWidth-1)/outputWidth);
    const uint32_t endY=uint32_t((uint64_t(oy+rh)*valid.height+outputHeight-1)/outputHeight);
    const auto w=(std::min)(endX-x,512u),h=(std::min)(endY-y,512u);
    return {valid.x+x+(endX-x-w)/2,valid.y+y+(endY-y-h)/2,w,h};
}
struct GuideEvidence {
    std::string role,coverage="not_observed",reason="not_requested",file,aliasOf;
    const char* api="unknown";const char* stage="before_model";const char* units="source_texel_values";
    uint32_t sourceFormat=0,storageFormat=0,sourceWidth=0,sourceHeight=0,texelBytes=0,channels=0,aspect=0,mip=0,layer=0;
    GuideRegion valid{},roi{};float scaleX=1,scaleY=1;
    uint64_t resourceGeneration=0,viewGeneration=0;
    bool recorded=false;
    std::string Json(bool complete) const {
        auto quote=[](const std::string& value){std::string s="\"";for(char c:value){if(c=='\\'||c=='\"')s+='\\';s+=c;}return s+'\"';};
        auto rect=[](GuideRegion r){return "["+std::to_string(r.x)+","+std::to_string(r.y)+","+std::to_string(r.width)+","+std::to_string(r.height)+"]";};
        std::ostringstream s;
        auto number=[](float v){std::ostringstream n;n<<std::setprecision(std::numeric_limits<float>::max_digits10)<<v;return std::isfinite(v)?n.str():std::string("null");};
        s<<"{\"coverage\":"<<quote(recorded?(complete?"captured":"recorded_pending"):coverage)<<",\"reason\":"<<quote(reason)
         <<",\"file\":"<<quote(file)<<",\"alias_of\":"<<quote(aliasOf)<<",\"format_api\":"<<quote(api)<<",\"source_format\":"<<sourceFormat<<",\"storage_format\":"<<storageFormat
         <<",\"source_width\":"<<sourceWidth<<",\"source_height\":"<<sourceHeight<<",\"valid_rect\":"<<rect(valid)<<",\"roi\":"<<rect(roi)
         <<",\"texel_bytes\":"<<texelBytes<<",\"channels\":"<<channels<<",\"row_pitch\":"<<roi.width*texelBytes<<",\"aspect\":"<<aspect<<",\"mip\":"<<mip<<",\"layer\":"<<layer
         <<",\"stage\":"<<quote(stage)<<",\"units\":"<<quote(units)<<",\"scale\":["<<number(scaleX)<<","<<number(scaleY)
         <<"],\"resource_generation\":"<<resourceGeneration<<",\"view_generation\":"<<viewGeneration<<",\"scope\":\"bounded_roi_no_halo\"}";
        return s.str();
    }
};
}
