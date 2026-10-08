// Native SL2 passthrough observers. No replacement handlers or additional API queries.
bool FgRetain(void* address);
using ChainValues=std::array<uint64_t,4>;
struct ChainOptions {
    std::array<ChainValues,6> groups{};
    bool valid=false;
    uint64_t fingerprint=0;
};
ChainOptions ChainReadOptions(const sl::DLSSGOptions& o) {
    ChainOptions s{};
    __try {
        if(o.structType!=sl::DLSSGOptions::s_structType || o.structVersion<1 || o.structVersion>5)return s;
        s.groups[0]={uint64_t(o.mode),o.numFramesToGenerate,uint32_t(o.flags),o.structVersion};
        s.groups[1]={o.dynamicResWidth,o.dynamicResHeight,o.numBackBuffers,o.mvecDepthWidth};
        s.groups[2]={o.mvecDepthHeight,o.colorWidth,o.colorHeight,o.colorBufferFormat};
        s.groups[3]={o.mvecBufferFormat,o.depthBufferFormat,o.hudLessBufferFormat,o.uiBufferFormat};
        s.groups[4]={uintptr_t(o.onErrorCallback),uintptr_t(o.next),
            o.structVersion>=3?uint32_t(o.queueParallelismMode):UINT32_MAX,
            o.structVersion>=4?uint32_t(o.enableUserInterfaceRecomposition):UINT32_MAX};
        uint32_t target=UINT32_MAX;if(o.structVersion>=5)memcpy(&target,&o.dynamicTargetFrameRate,4);
        s.groups[5]={target,o.structVersion>=2?uint32_t(o.bReserved15):UINT32_MAX,0,0};
        s.fingerprint=1469598103934665603ull;
        for(const auto& g:s.groups)for(auto v:g){s.fingerprint^=v;s.fingerprint*=1099511628211ull;}
        s.valid=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return s;
}
void ChainOptionsEmit(Fgp::Mode mode,const ChainOptions& s,bool submitted,uint32_t result) {
    static const char* req[]={"options.req.mode","options.req.dims0","options.req.dims1","options.req.formats","options.req.callbacks","options.req.extra"};
    static const char* sub[]={"options.sub.mode","options.sub.dims0","options.sub.dims1","options.sub.formats","options.sub.callbacks","options.sub.extra"};
    if(!s.valid){Fgp::ChainSample(mode,submitted?"options.sub.unreadable":"options.req.unreadable",{},result);return;}
    for(size_t i=0;i<6;++i)Fgp::ChainSample(mode,submitted?sub[i]:req[i],s.groups[i],result,result!=0);
}
void ChainState(const sl::ViewportHandle& viewport,const sl::DLSSGState& s,sl::Result result) {
    const auto mode=Fgp::ChainMode();if(mode==Fgp::Mode::Off)return;
    __try {
        if(result!=sl::Result::eOk || s.structVersion<1 || s.structVersion>4 || s.structType!=sl::DLSSGState::s_structType) {
            Fgp::ChainSample(mode,"state.unreadable",{uint32_t(viewport),0,0,0},uint32_t(result),true);return;
        }
        Fgp::ChainSample(mode,"state",{uint32_t(s.status),uint32_t(viewport),s.numFramesActuallyPresented,s.structVersion},
            uint32_t(result),false,s.numFramesActuallyPresented,true,uint32_t(s.status));
        Fgp::ChainSample(mode,"state.capacity",{s.estimatedVRAMUsageInBytes,uint32_t(viewport),s.minWidthOrHeight,
            s.structVersion>=2?s.numFramesToGenerateMax:UINT32_MAX});
        if(s.structVersion>=2)Fgp::ChainSample(mode,"state.fence",{uintptr_t(s.inputsProcessingCompletionFence),s.lastPresentInputsProcessingCompletionFenceValue,0,0});
    } __except(EXCEPTION_EXECUTE_HANDLER){Fgp::ChainSample(mode,"state.unreadable",{},uint32_t(result),true);}
}
decltype(&slSetTag) chainTag=nullptr;
decltype(&slSetTagForFrame) chainTagFrame=nullptr;
decltype(&slSetConstants) chainConstants=nullptr;
decltype(&slGetNewFrameToken) chainToken=nullptr;
void ChainTags(const char* route,const sl::ViewportHandle& viewport,const sl::ResourceTag* tags,uint32_t count,
               uint32_t frame,sl::Result result) {
    const auto mode=Fgp::ChainMode();if(mode==Fgp::Mode::Off)return;
    Fgp::ChainSample(mode,route,{uint32_t(viewport),frame,count,tags?1u:0u},uint32_t(result),result!=sl::Result::eOk);
    if(count>64){Fgp::ChainSample(mode,"tags.truncated",{count,64,0,0});count=64;}
    __try {
        if(!tags)return;
        for(uint32_t i=0;i<count;++i){const auto& t=tags[i];const char* name=nullptr;const char* extent=nullptr;
            switch(t.type){
            case sl::kBufferTypeDepth:name="tag.depth";extent="extent.depth";break;
            case sl::kBufferTypeMotionVectors:name="tag.mv";extent="extent.mv";break;
            case sl::kBufferTypeHUDLessColor:name="tag.hudless";extent="extent.hudless";break;
            case sl::kBufferTypeUIColorAndAlpha:name="tag.ui";extent="extent.ui";break;
            case sl::kBufferTypeUIAlpha:name="tag.ui_alpha";extent="extent.ui_alpha";break;
            case sl::kBufferTypeBackbuffer:name="tag.backbuffer";extent="extent.backbuffer";break;
            default:continue;}
            const auto native=t.resource?uintptr_t(t.resource->native):0;
            Fgp::ChainSample(mode,name,{native?1u:0u,uint32_t(viewport),uint32_t(t.lifecycle),t.resource?t.resource->state:UINT32_MAX},
                uint32_t(result),result!=sl::Result::eOk,0,true,(uint64_t(uint32_t(viewport))<<32)|(native?1u:0u));
            Fgp::ChainSample(mode,extent,{t.extent.width,t.extent.height,t.extent.left,t.extent.top},uint32_t(result),result!=sl::Result::eOk);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){Fgp::ChainSample(mode,"tags.unreadable",{},uint32_t(result),true);}
}
sl::Result ChainTag(const sl::ViewportHandle& v,const sl::ResourceTag* tags,uint32_t count,sl::CommandBuffer* cmd) {
    const auto r=chainTag(v,tags,count,cmd);ChainTags("slSetTag",v,tags,count,UINT32_MAX,r);return r;
}
sl::Result ChainTagFrame(const sl::FrameToken& f,const sl::ViewportHandle& v,const sl::ResourceTag* tags,uint32_t count,sl::CommandBuffer* cmd) {
    const auto r=chainTagFrame(f,v,tags,count,cmd);ChainTags("slSetTagForFrame",v,tags,count,uint32_t(f),r);return r;
}
bool ChainFinite(const float* data,size_t count){for(size_t i=0;i<count;++i)if(!std::isfinite(data[i]) || data[i]==sl::INVALID_FLOAT)return false;return true;}
sl::Result ChainConstants(const sl::Constants& c,const sl::FrameToken& f,const sl::ViewportHandle& v) {
    const auto r=chainConstants(c,f,v);const auto mode=Fgp::ChainMode();if(mode==Fgp::Mode::Off)return r;
    __try {
        if(c.structVersion<1 || c.structVersion>2 || c.structType!=sl::Constants::s_structType){Fgp::ChainSample(mode,"constants.unreadable",{},uint32_t(r),true);return r;}
        uint32_t valid=0;
        valid|=ChainFinite(&c.cameraViewToClip.row[0].x,16)?1:0;
        valid|=ChainFinite(&c.clipToCameraView.row[0].x,16)?2:0;
        valid|=ChainFinite(&c.clipToPrevClip.row[0].x,16)?4:0;
        valid|=ChainFinite(&c.prevClipToClip.row[0].x,16)?8:0;
        valid|=ChainFinite(&c.cameraPos.x,12)?16:0;
        valid|=ChainFinite(&c.cameraNear,4) && c.cameraNear>0 && c.cameraFar>c.cameraNear && c.cameraFOV>0 && c.cameraAspectRatio>0?32:0;
        Fgp::ChainSample(mode,"constants",{uint32_t(f),uint32_t(v),uint32_t(c.reset),valid},uint32_t(r),r!=sl::Result::eOk,0,true,(uint64_t(uint32_t(c.reset))<<32)|valid);
        uint32_t mvx=0,mvy=0;memcpy(&mvx,&c.mvecScale.x,4);memcpy(&mvy,&c.mvecScale.y,4);
        Fgp::ChainSample(mode,"constants.mv",{mvx,mvy,uint32_t(c.cameraMotionIncluded),uint32_t(c.depthInverted)},uint32_t(r),r!=sl::Result::eOk);
        uint64_t cameraHash=1469598103934665603ull;
        const auto hashFloats=[&](const float* p,size_t count){for(size_t i=0;i<count;++i){uint32_t b=0;memcpy(&b,p+i,4);cameraHash^=b;cameraHash*=1099511628211ull;}};
        hashFloats(&c.cameraViewToClip.row[0].x,16);hashFloats(&c.clipToCameraView.row[0].x,16);
        hashFloats(&c.clipToPrevClip.row[0].x,16);hashFloats(&c.prevClipToClip.row[0].x,16);hashFloats(&c.cameraPos.x,12);
        uint32_t nearBits=0,farBits=0,fovBits=0,aspectBits=0;
        memcpy(&nearBits,&c.cameraNear,4);memcpy(&farBits,&c.cameraFar,4);memcpy(&fovBits,&c.cameraFOV,4);memcpy(&aspectBits,&c.cameraAspectRatio,4);
        Fgp::ChainSample(mode,"constants.camera",{cameraHash,uint32_t(v),nearBits,farBits},uint32_t(r),r!=sl::Result::eOk);
        Fgp::ChainSample(mode,"constants.camera_scalars",{fovBits,aspectBits,uint32_t(c.orthographicProjection),c.structVersion},uint32_t(r),r!=sl::Result::eOk);
        Fgp::ChainSample(mode,"constants.flags",{uint32_t(c.motionVectors3D),uint32_t(c.motionVectorsDilated),uint32_t(c.motionVectorsJittered),uint32_t(c.reset)},uint32_t(r),r!=sl::Result::eOk);
    } __except(EXCEPTION_EXECUTE_HANDLER){Fgp::ChainSample(mode,"constants.unreadable",{},uint32_t(r),true);}
    return r;
}
sl::Result ChainToken(sl::FrameToken*& token,const uint32_t* index) {
    const auto r=chainToken(token,index);const auto mode=Fgp::ChainMode();if(mode==Fgp::Mode::Off)return r;
    __try {Fgp::ChainSample(mode,"slGetNewFrameToken",{index?*index:UINT32_MAX,r==sl::Result::eOk && token?uint32_t(*token):UINT32_MAX,token?1u:0u,index?1u:0u},uint32_t(r),r!=sl::Result::eOk);}
    __except(EXCEPTION_EXECUTE_HANDLER){Fgp::ChainSample(mode,"token.unreadable",{},uint32_t(r),true);}return r;
}
struct ChainCallbacks {
    std::atomic<PFun_slReflexSetOptions*> reflex{nullptr};
    std::atomic<PFun_slPCLSetOptions*> pcl{nullptr};
    std::atomic<PFun_slPCLSetMarker*> marker{nullptr};
    std::atomic<PFun_slReflexSleep*> sleep{nullptr};
};
std::array<ChainCallbacks,4> chainCallbacks;
template<size_t I> sl::Result ChainReflex(const sl::ReflexOptions& o) {
    const auto r=chainCallbacks[I].reflex.load()(o);const auto mode=Fgp::ChainMode();
    if(mode!=Fgp::Mode::Off){__try {Fgp::ChainSample(mode,"slReflexSetOptions",{uint32_t(o.mode),o.frameLimitUs,o.useMarkersToOptimize?1u:0u,o.structVersion},uint32_t(r),r!=sl::Result::eOk,0,true,(uint64_t(o.frameLimitUs)<<32)|uint32_t(o.mode));}
        __except(EXCEPTION_EXECUTE_HANDLER){Fgp::ChainSample(mode,"reflex.unreadable",{},uint32_t(r),true);}}return r;
}
template<size_t I> sl::Result ChainPcl(const sl::PCLOptions& o) {
    const auto r=chainCallbacks[I].pcl.load()(o);const auto mode=Fgp::ChainMode();
    if(mode!=Fgp::Mode::Off){__try {Fgp::ChainSample(mode,"slPCLSetOptions",{uint16_t(o.virtualKey),o.idThread,o.structVersion,0},uint32_t(r),r!=sl::Result::eOk);}
        __except(EXCEPTION_EXECUTE_HANDLER){Fgp::ChainSample(mode,"pcl.unreadable",{},uint32_t(r),true);}}return r;
}
template<size_t I> sl::Result ChainMarker(sl::PCLMarker marker,const sl::FrameToken& frame) {
    const auto r=chainCallbacks[I].marker.load()(marker,frame);const auto mode=Fgp::ChainMode();
    if(mode!=Fgp::Mode::Off){char route[80]{};snprintf(route,sizeof(route),"slPCL.marker.%u",uint32_t(marker));
        Fgp::ChainSample(mode,route,{uint32_t(frame),uint32_t(marker),0,0},uint32_t(r),r!=sl::Result::eOk);}return r;
}
template<size_t I> sl::Result ChainSleep(const sl::FrameToken& f) {
    const auto r=chainCallbacks[I].sleep.load()(f);Fgp::ChainSample(Fgp::ChainMode(),"slReflexSleep",{uint32_t(f),0,0,0},uint32_t(r),r!=sl::Result::eOk);return r;
}
// Fixed immutable callback slots, with the same module-lease/fail-open rules as P0.
template<class T> bool ChainBindOne(std::atomic<T>& slot,T thunk,void*& function) {
    auto original=slot.load();if(function==reinterpret_cast<void*>(thunk))return true;
    if(original && function==reinterpret_cast<void*>(original)){function=reinterpret_cast<void*>(thunk);return true;}
    if(original)return false;
    if(!FgRetain(function))return false;
    slot.store(reinterpret_cast<T>(function));function=reinterpret_cast<void*>(thunk);return true;
}
void ChainBind(const char* name,void*& function) {
    // SL may cache callback pointers before the D3D12 device selects the API.
    // Arm from startup diagnostics configuration; runtime samples still require native DX12.
    const auto& state=State::Instance();
    if(Config::Instance()->DlssNrDiagnostics.value_or_default()==0 ||
        state.activeFgInput!=FGInput::NoFG || state.activeFgOutput!=FGOutput::NoFG ||
        state.api==API::DX11 || state.api==API::Vulkan || !name || !function)return;
    const bool reflex=strcmp(name,"slReflexSetOptions")==0,pcl=strcmp(name,"slPCLSetOptions")==0,
        marker=strcmp(name,"slPCLSetMarker")==0,sleep=strcmp(name,"slReflexSleep")==0;
    if(!reflex && !pcl && !marker && !sleep)return;
    static std::mutex mutex;std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock()){Fgp::FgCoverage(Fgp::ChainMode(),"chain/callback_busy_original_preserved");return;}
    // Search every existing target before consuming an empty slot.
    for(const auto& slot:chainCallbacks) {
        const void* target=reflex?reinterpret_cast<void*>(slot.reflex.load()):pcl?reinterpret_cast<void*>(slot.pcl.load()):
            marker?reinterpret_cast<void*>(slot.marker.load()):reinterpret_cast<void*>(slot.sleep.load());
        if(target==function) {
            const size_t i=&slot-chainCallbacks.data();
            static PFun_slReflexSetOptions* a[]={ChainReflex<0>,ChainReflex<1>,ChainReflex<2>,ChainReflex<3>};
            static PFun_slPCLSetOptions* b[]={ChainPcl<0>,ChainPcl<1>,ChainPcl<2>,ChainPcl<3>};
            static PFun_slPCLSetMarker* c[]={ChainMarker<0>,ChainMarker<1>,ChainMarker<2>,ChainMarker<3>};
            static PFun_slReflexSleep* d[]={ChainSleep<0>,ChainSleep<1>,ChainSleep<2>,ChainSleep<3>};
            function=reflex?reinterpret_cast<void*>(a[i]):pcl?reinterpret_cast<void*>(b[i]):marker?reinterpret_cast<void*>(c[i]):reinterpret_cast<void*>(d[i]);return;
        }
    }
#define CHAIN_BIND(I) if((reflex && ChainBindOne(chainCallbacks[I].reflex,ChainReflex<I>,function)) || (pcl && ChainBindOne(chainCallbacks[I].pcl,ChainPcl<I>,function)) || (marker && ChainBindOne(chainCallbacks[I].marker,ChainMarker<I>,function)) || (sleep && ChainBindOne(chainCallbacks[I].sleep,ChainSleep<I>,function))){Fgp::FgCoverage(Fgp::ChainMode(),name);return;}
    CHAIN_BIND(0) CHAIN_BIND(1) CHAIN_BIND(2) CHAIN_BIND(3)
#undef CHAIN_BIND
    Fgp::FgCoverage(Fgp::ChainMode(),"chain/callback_capacity_or_lease_original_preserved");
}
void ChainInstall(HMODULE module) {
    const auto coverageMode=static_cast<Fgp::Mode>(std::min(Config::Instance()->DlssNrDiagnostics.value_or_default(),2u));
    if(Config::Instance()->DlssNrDiagnostics.value_or_default()==0)return;
    chainTag=reinterpret_cast<decltype(chainTag)>(KernelBaseProxy::GetProcAddress_()(module,"slSetTag"));
    chainTagFrame=reinterpret_cast<decltype(chainTagFrame)>(KernelBaseProxy::GetProcAddress_()(module,"slSetTagForFrame"));
    chainConstants=reinterpret_cast<decltype(chainConstants)>(KernelBaseProxy::GetProcAddress_()(module,"slSetConstants"));
    chainToken=reinterpret_cast<decltype(chainToken)>(KernelBaseProxy::GetProcAddress_()(module,"slGetNewFrameToken"));
    // Each independent optional hook is its own transaction. Failure leaves other coverage intact.
#define CHAIN_ATTACH(P,F,N) if(P){const auto outcome=HookLifecycle::Transact([]{return DetourTransactionBegin();},[]{return DetourUpdateThread(GetCurrentThread());},[]{return DetourAttach(&(PVOID&)P,F);},[]{return DetourTransactionCommit();},[]{return DetourTransactionAbort();});char coverage[96]{};snprintf(coverage,sizeof(coverage),"%s/%s",N,outcome?"attached":outcome.stage);Fgp::FgCoverage(coverageMode,coverage,uint32_t(outcome.code));if(!outcome)P=nullptr;}else Fgp::FgCoverage(coverageMode,N "/export_missing");
    CHAIN_ATTACH(chainTag,ChainTag,"chain/slSetTag")
    CHAIN_ATTACH(chainTagFrame,ChainTagFrame,"chain/slSetTagForFrame")
    CHAIN_ATTACH(chainConstants,ChainConstants,"chain/slSetConstants")
    CHAIN_ATTACH(chainToken,ChainToken,"chain/slGetNewFrameToken")
#undef CHAIN_ATTACH
}
