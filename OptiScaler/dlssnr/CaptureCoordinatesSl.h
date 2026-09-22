#pragma once
#include "CaptureCoordinates.h"
#include <sl_consts.h>
#include <sl.h>

namespace capture::coordinates {
inline FloatValue SlFloat(float v){
    FloatValue f{};f.queried=true;f.value=v;
    f.result=std::isfinite(v)&&v!=sl::INVALID_FLOAT?1:0;return f;
}
inline Camera FromSl(const sl::Constants& v,uint32_t token,uint32_t viewport,bool accepted){
    Camera c{};c.observed=true;c.accepted=accepted;c.token=token;c.viewport=viewport;c.version=v.structVersion;
    if(v.structVersion<1){c.accepted=false;return c;}
    c.jitterX=SlFloat(v.jitterOffset.x);c.jitterY=SlFloat(v.jitterOffset.y);
    c.nearPlane=SlFloat(v.cameraNear);c.farPlane=SlFloat(v.cameraFar);
    const sl::float4x4* matrices[]={&v.cameraViewToClip,&v.clipToCameraView,&v.clipToPrevClip,&v.prevClipToClip};
    std::array<float,16>* output[]={&c.projection,&c.inverseProjection,&c.clipToPrev,&c.prevToClip};
    for(unsigned k=0;k<4;++k){bool valid=true;for(unsigned row=0;row<4;++row){const auto& r=(*matrices[k])[row];
        const float a[]={r.x,r.y,r.z,r.w};for(unsigned col=0;col<4;++col){(*output[k])[row*4+col]=a[col];valid &= SlFloat(a[col]).valid();}}
        c.matricesValid[k]=valid;
    }
    auto boolean=[](sl::Boolean b){return b==sl::Boolean::eTrue?1:(b==sl::Boolean::eFalse?0:-1);};
    c.depthInverted=boolean(v.depthInverted);c.mvJittered=boolean(v.motionVectorsJittered);
    c.cameraMotionIncluded=boolean(v.cameraMotionIncluded);c.orthographic=boolean(v.orthographicProjection);
    return c;
}
inline bool Viewport(const sl::BaseStructure** inputs,uint32_t count,uint32_t& viewport){
    bool found=false;if(!inputs||count>64)return false;
    for(uint32_t i=0;i<count;++i){if(!inputs[i])continue;
        if(inputs[i]->structType==sl::ViewportHandle::s_structType){
            const auto value=uint32_t(*static_cast<const sl::ViewportHandle*>(inputs[i]));
            if(found&&value!=viewport)return false;found=true;viewport=value;
        }}return found;
}
}
