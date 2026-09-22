#pragma once
// Bounded observational metadata only. Never writes NGX parameters or shader constants.
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>

namespace capture::coordinates {
struct FloatValue { uint32_t result=0; float value=0; bool queried=false;
    bool valid() const {return queried && result==1 && std::isfinite(value);} };
struct UintValue { uint32_t result=0,value=0; bool queried=false;
    bool valid() const {return queried && result==1;} };
struct Params {
    FloatValue jitterX,jitterY,mvX,mvY,preExposure;
    UintValue flags,reset,renderWidth,renderHeight;
};
template<class P> Params Read(P* p) {
    Params s{};if(!p)return s;
    auto f=[&](const char* key,FloatValue& v){v.queried=true;v.result=uint32_t(p->Get(key,&v.value));};
    auto u=[&](const char* key,UintValue& v){v.queried=true;v.result=uint32_t(p->Get(key,&v.value));};
    f("Jitter.Offset.X",s.jitterX);f("Jitter.Offset.Y",s.jitterY);
    f("MV.Scale.X",s.mvX);f("MV.Scale.Y",s.mvY);f("DLSS.Pre.Exposure",s.preExposure);
    u("DLSS.Feature.Create.Flags",s.flags);u("Reset",s.reset);
    u("DLSS.Render.Subrect.Dimensions.Width",s.renderWidth);u("DLSS.Render.Subrect.Dimensions.Height",s.renderHeight);
    return s;
}
struct Camera {
    bool observed=false,accepted=false;
    uint32_t token=0,viewport=0,version=0;
    std::array<float,16> projection{},inverseProjection{},clipToPrev{},prevToClip{};
    std::array<bool,4> matricesValid{};
    FloatValue jitterX,jitterY,nearPlane,farPlane;
    int depthInverted=-1,mvJittered=-1,cameraMotionIncluded=-1,orthographic=-1;
};
// No latest-value fallback. Exact token+viewport only; evicted/missing keys remain unknown.
class CameraCache {
    std::mutex mutex_;std::array<Camera,32> slots_{};unsigned next_=0;
public:
    void clear(){std::lock_guard lock(mutex_);slots_={};next_=0;}
    void put(const Camera& c){std::lock_guard lock(mutex_);
        for(auto& s:slots_)if(s.observed&&s.token==c.token&&s.viewport==c.viewport){s=c;return;}
        slots_[next_++%slots_.size()]=c;
    }
    Camera get(uint32_t token,uint32_t viewport){std::lock_guard lock(mutex_);
        for(const auto& s:slots_)if(s.observed&&s.token==token&&s.viewport==viewport)return s;
        return {};
    }
};
inline CameraCache cameras;
struct SlBinding {const void* command=nullptr;Camera camera{};bool viewportKnown=false;uint32_t token=0,viewport=0;};
inline thread_local const SlBinding* currentSl=nullptr;
struct SlScope {
    SlBinding binding{};const SlBinding* previous=nullptr;
    SlScope(bool enabled,const void* command,uint32_t token,uint32_t viewport,bool known) {
        previous=currentSl;
        if(enabled){binding.command=command;binding.token=token;binding.viewport=viewport;binding.viewportKnown=known;
            if(known)binding.camera=cameras.get(token,viewport);currentSl=&binding;}
        else currentSl=nullptr;
    }
    ~SlScope(){currentSl=previous;}
    SlScope(const SlScope&)=delete;SlScope& operator=(const SlScope&)=delete;
};
struct NgxBinding {const void* parameters=nullptr;const void* command=nullptr;uint64_t source=0;Params before{};SlBinding sl{};bool slNested=false;};
inline thread_local const NgxBinding* currentNgx=nullptr;
struct NgxScope {
    NgxBinding binding{};const NgxBinding* previous=nullptr;
    template<class P> NgxScope(bool enabled,P* p,const void* command,uint64_t source){
        previous=currentNgx;
        if(enabled&&p){binding.parameters=p;binding.command=command;binding.source=source;binding.before=Read(p);
            if(currentSl&&currentSl->command==command){binding.sl=*currentSl;binding.slNested=true;}
            currentNgx=&binding;
        }else currentNgx=nullptr;
    }
    ~NgxScope(){currentNgx=previous;}
    NgxScope(const NgxScope&)=delete;NgxScope& operator=(const NgxScope&)=delete;
};
template<class V> void Scalar(std::ostream& o,const char* name,const V& v){
    o<<'"'<<name<<"\":{\"queried\":"<<(v.queried?"true":"false")<<",\"result\":"<<v.result
     <<",\"valid\":"<<(v.valid()?"true":"false")<<",\"value\":";
    if(v.valid())o<<v.value;else o<<"null";o<<'}';
}
inline void ParamsJson(std::ostream& o,const Params& s){
    o<<'{';Scalar(o,"jitter_x",s.jitterX);o<<',';Scalar(o,"jitter_y",s.jitterY);o<<',';
    Scalar(o,"mv_scale_x",s.mvX);o<<',';Scalar(o,"mv_scale_y",s.mvY);o<<',';
    Scalar(o,"create_flags",s.flags);o<<',';Scalar(o,"reset",s.reset);o<<',';
    Scalar(o,"render_width",s.renderWidth);o<<',';Scalar(o,"render_height",s.renderHeight);o<<',';
    Scalar(o,"pre_exposure",s.preExposure);o<<'}';
}
inline void Matrix(std::ostream& o,const char* name,const std::array<float,16>& v,bool valid){
    o<<'"'<<name<<"\":";if(!valid){o<<"null";return;}o<<'[';
    for(unsigned i=0;i<16;++i){if(i)o<<',';o<<v[i];}o<<']';
}
template<class P> std::string Snapshot(P* p,const void* command,uint64_t source){
    const auto* b=currentNgx;
    if(!b||b->parameters!=p||b->command!=command||b->source!=source)return "{\"schema\":\"d18-coordinate-v1\",\"ngx_scope_matched\":false,\"reason\":\"pre_sr_scope_not_observed\"}";
    std::ostringstream o;o<<std::setprecision(std::numeric_limits<float>::max_digits10);
    o<<"{\"schema\":\"d18-coordinate-v1\",\"ngx_scope_matched\":true,\"history_source\":"<<source
     <<",\"stage\":\"before_sr_and_after_sr_before_nr\",\"jitter_units\":\"NGX_render_pixels\",\"before_sr\":";
    ParamsJson(o,b->before);o<<",\"after_sr\":";ParamsJson(o,Read(p));
    const auto& sl=b->sl;const auto& c=sl.camera;
    const bool matched=b->slNested&&sl.viewportKnown&&c.observed&&c.accepted;
    o<<",\"streamline\":{\"nested_same_command_list\":"<<(b->slNested?"true":"false")
     <<",\"viewport_observed\":"<<(sl.viewportKnown?"true":"false")<<",\"constants_matched\":"<<(matched?"true":"false")
     <<",\"frame_token\":";if(b->slNested)o<<sl.token;else o<<"null";
    o<<",\"viewport\":";if(sl.viewportKnown)o<<sl.viewport;else o<<"null";
    o<<",\"reason\":\""<<(matched?"exact_token_viewport_nested_call":"missing_or_unmatched_no_latest_fallback")<<'"';
    if(matched){o<<",\"struct_version\":"<<c.version<<',';Scalar(o,"jitter_x",c.jitterX);o<<',';Scalar(o,"jitter_y",c.jitterY);o<<',';
        Scalar(o,"camera_near",c.nearPlane);o<<',';Scalar(o,"camera_far",c.farPlane);o<<',';
        Matrix(o,"camera_view_to_clip",c.projection,c.matricesValid[0]);o<<',';
        Matrix(o,"clip_to_camera_view",c.inverseProjection,c.matricesValid[1]);o<<',';
        Matrix(o,"clip_to_prev_clip",c.clipToPrev,c.matricesValid[2]);o<<',';
        Matrix(o,"prev_clip_to_clip",c.prevToClip,c.matricesValid[3]);
        o<<",\"depth_inverted\":"<<c.depthInverted<<",\"motion_vectors_jittered\":"<<c.mvJittered
         <<",\"camera_motion_included\":"<<c.cameraMotionIncluded<<",\"orthographic_projection\":"<<c.orthographic;
    }
    o<<"},\"physical_alignment_verified\":false}";return o.str();
}
}
