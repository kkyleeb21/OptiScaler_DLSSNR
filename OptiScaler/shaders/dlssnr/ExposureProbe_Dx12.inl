// Included in the NR translation unit. All render-owner fields use g_nrMutex;
// only SubmissionBatch is shared with the existing post-Execute observer.
namespace exposure_probe {
using Microsoft::WRL::ComPtr;
std::atomic<bool> pendingCopy=false;
constexpr unsigned Grid=8, Samples=Grid*Grid, Stride=512, Bytes=Samples*Stride;
enum : unsigned { Pre=1, Scale=2, TextureKnown=4, Texture=8, CreationKnown=16,
    Auto=32, Hdr=64, Nr=128, DriverKnown=256, Rr=512, GameWhite=1024,
    ExposureRead=2048, Luma=4096, AppliedWhite=8192, EffectiveHdr=16384, UseExposure=32768,
    EffectiveHdrKnown=65536 };
struct State {
    capture::SubmissionBatch submission;
    ComPtr<ID3D12Resource> readback, source;
    ComPtr<ID3D12Device> device;
    std::atomic<bool> pending=false;
    bool allocationFailed=false, logged=false, latestLuma=false;
    uint64_t lastSampleMs=0, lastLogMs=0, sampleFrame=0, latestFrame=0;
    unsigned sampleFlags=0, latestFlags=0, latestCount=0, lastFlags=0;
    unsigned lastWidth=0,lastHeight=0,lastFormat=0;
    uint64_t lastSource=0;
    float latestLog=0;
    DlssNr::Diagnostics::Event event{};
    const char* status="unobserved";
};
// Pending/discarded GPU recordings must keep pins until process teardown;
// never retire by elapsed time or release them in the ordinary NR shutdown.
State& Get(){static auto* state=new State;return *state;}
float UnsignedFloat(unsigned bits,unsigned mantissa) {
    const unsigned m=bits&((1u<<mantissa)-1),e=bits>>mantissa;
    if(e==31)return m?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity();
    return e?std::ldexp(1.0f+float(m)/float(1u<<mantissa),int(e)-15):
             std::ldexp(float(m),-14-int(mantissa));
}
float Half(uint16_t v) { return (v&0x8000?-1.0f:1.0f)*UnsignedFloat(v&0x7fff,10); }
bool Supported(DXGI_FORMAT f) {
    return f==DXGI_FORMAT_R16G16B16A16_FLOAT || f==DXGI_FORMAT_R32G32B32A32_FLOAT ||
           f==DXGI_FORMAT_R11G11B10_FLOAT;
}
void Poll(State& s) {
    if(!s.pending.load() || !s.submission.complete())return;
    void* mapped=nullptr;D3D12_RANGE range{0,Bytes};
    s.latestLuma=false;
    if(SUCCEEDED(s.readback->Map(0,&range,&mapped)) && mapped) {
        double sum=0;unsigned count=0;
        const auto format=s.source->GetDesc().Format;
        for(unsigned i=0;i<Samples;++i) {
            const auto* p=static_cast<const unsigned char*>(mapped)+i*Stride;
            float rgb[3]{};
            if(format==DXGI_FORMAT_R16G16B16A16_FLOAT) {
                uint16_t v[4];std::memcpy(v,p,sizeof(v));
                for(unsigned c=0;c<3;++c)rgb[c]=Half(v[c]);
            } else if(format==DXGI_FORMAT_R32G32B32A32_FLOAT)std::memcpy(rgb,p,sizeof(rgb));
            else {uint32_t v;std::memcpy(&v,p,sizeof(v));
                rgb[0]=UnsignedFloat(v&2047,6);rgb[1]=UnsignedFloat((v>>11)&2047,6);
                rgb[2]=UnsignedFloat(v>>22,5);
            }
            if(!std::isfinite(rgb[0]) || !std::isfinite(rgb[1]) || !std::isfinite(rgb[2]))continue;
            const float y=0.2126f*rgb[0]+0.7152f*rgb[1]+0.0722f*rgb[2];
            sum+=std::log2(std::max(y,1e-6f));++count;
        }
        D3D12_RANGE written{0,0};s.readback->Unmap(0,&written);
        s.latestLuma=count>0;s.latestLog=count?float(sum/count):0;
        s.latestCount=count;s.latestFrame=s.sampleFrame;s.latestFlags=s.sampleFlags;
        s.status=count?"ready":"nonfinite_samples";
    } else s.status="map_failed";
    s.submission.clearCompleted();s.source.Reset();s.pending.store(false);pendingCopy.store(false);
}
void Sample(State& s,ID3D12GraphicsCommandList* list,ID3D12Resource* target,
            NVSDK_NGX_Parameter* params,ID3D12CommandQueue* timingQueue,
            unsigned outputWidth,unsigned outputHeight,bool rr,const Config& cfg) {
    const auto now=GetTickCount64();
    if(now-s.lastSampleMs<1000)return;
    s.lastSampleMs=now;
    if(s.pending.load()){s.status="awaiting_submission_or_gpu";return;}
    if(!target){s.status="output_unobserved";return;}
    const auto d=target->GetDesc();
    if(!Supported(d.Format) || d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
       d.SampleDesc.Count!=1 || d.DepthOrArraySize!=1){s.status="unsupported_output";return;}
    unsigned x=0,y=0,w=unsigned(d.Width),h=d.Height,rw=0,rh=0;
    params->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X,&x);
    params->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y,&y);
    params->Get(NVSDK_NGX_Parameter_OutWidth,&w);params->Get(NVSDK_NGX_Parameter_OutHeight,&h);
    params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,&rw);
    params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,&rh);
    const bool yysls=_wcsicmp(Util::ExePath().filename().c_str(),L"yysls.exe")==0;
    const auto resolution=DlssNr::ResolveSrOutputRect(yysls,rr,{x,y,w,h},rw,rh,d.Width,d.Height,outputWidth,outputHeight);
    w=resolution.width;h=resolution.height;
    if(!w || !h || uint64_t(x)+w>d.Width || uint64_t(y)+h>d.Height){s.status="invalid_output_rect";return;}
    ComPtr<ID3D12Device> device;
    if(FAILED(target->GetDevice(IID_PPV_ARGS(&device))) ||
       !DlssNr::EnsureNativeSubmissionObserver(device.Get())){s.status="observer_unavailable";return;}
    if(s.device.Get()!=device.Get()) {s.readback.Reset();s.device=device;s.allocationFailed=false;}
    if(s.allocationFailed){s.status="allocation_failed";return;}
    if(!s.readback) {
        const auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        const auto buffer=CD3DX12_RESOURCE_DESC::Buffer(Bytes);
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&s.readback)))) {
            s.allocationFailed=true;s.status="allocation_failed";return;
        }
    }
    if(!s.submission.arm(device.Get(),list)){s.status="fence_unavailable";return;}
    s.source=target;s.sampleFrame=s.event.frame;s.sampleFlags=s.event.flags;
    // Same documented arrival contract as DispatchLocked; this copy runs before all NR writes.
    const auto arrival=rr?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
        cfg.OutputResourceBarrier.has_value()?D3D12_RESOURCE_STATES(cfg.OutputResourceBarrier.value()):
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Barrier(list,target,arrival,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=target;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=s.readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint={d.Format,1,1,1,256};
    for(unsigned row=0;row<Grid;++row)for(unsigned col=0;col<Grid;++col) {
        const unsigned px=x+unsigned((uint64_t(2*col+1)*w)/(2*Grid));
        const unsigned py=y+unsigned((uint64_t(2*row+1)*h)/(2*Grid));
        D3D12_BOX box{px,py,0,px+1,py+1,1};
        dst.PlacedFootprint.Offset=(row*Grid+col)*Stride;
        list->CopyTextureRegion(&dst,0,0,0,&src,&box);
    }
    Barrier(list,target,D3D12_RESOURCE_STATE_COPY_SOURCE,arrival);
    s.pending.store(true);pendingCopy.store(true);s.status="sample_recorded";
}
void Begin(ID3D12GraphicsCommandList* list,NVSDK_NGX_Parameter* params,
           ID3D12CommandQueue* timingQueue,unsigned ow,unsigned oh,bool rrArrival,int rr,uint64_t source) {
    if(!params){
        g_inputExposure={};
        auto_wp::Begin(list,nullptr,ow,oh,rrArrival,source,false,nullptr);
        if(Config::Instance()->DlssNrDiagnostics.value_or_default()!=0)Get().event={};
        return;
    }
    float pre=0,scale=0;
    g_inputExposure.preObserved=params->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,&pre)==NVSDK_NGX_Result_Success;
    g_inputExposure.scaleObserved=params->Get(NVSDK_NGX_Parameter_DLSS_Exposure_Scale,&scale)==NVSDK_NGX_Result_Success;
    g_inputExposure.rawPreExposure=pre;g_inputExposure.exposureScale=scale;
    const auto creation=DlssNr::ExposureObservation::Read(unsigned(source));
    g_inputExposure.flagsObserved=creation.known;
    g_inputExposure.autoExposure=(creation.flags&NVSDK_NGX_DLSS_Feature_Flags_AutoExposure)!=0;
    const auto& cfg=*Config::Instance();
    auto* autoTarget=list?GetResource(params,NVSDK_NGX_Parameter_Output,"DLSSD.Output"):nullptr;
    unsigned autoFlags=0;params->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,&autoFlags);
    const bool autoHdr=autoTarget && (autoFlags&NVSDK_NGX_DLSS_Feature_Flags_IsHDR) && FormatCanHoldLinearHdr(autoTarget->GetDesc().Format);
    auto_wp::Begin(list,params,ow,oh,rrArrival,source,autoHdr,autoTarget);
    if(cfg.DlssNrDiagnostics.value_or_default()==0){Poll(Get());return;}
    auto& s=Get();Poll(s);s.event={};auto& e=s.event;
    e.type="exposure_probe";e.frame=++g_exposureProbeFrame;e.featureGeneration=source;
    if(g_inputExposure.preObserved)e.flags|=Pre;
    if(g_inputExposure.scaleObserved)e.flags|=Scale;
    e.ratio=pre;e.exposure=scale;
    void* texture=nullptr;
    if(params->Get(NVSDK_NGX_Parameter_ExposureTexture,&texture)==NVSDK_NGX_Result_Success)e.flags|=TextureKnown;
    if(!texture) {ID3D12Resource* typed=nullptr;
        if(params->Get(NVSDK_NGX_Parameter_ExposureTexture,&typed)==NVSDK_NGX_Result_Success){e.flags|=TextureKnown;texture=typed;}}
    if(texture)e.flags|=Texture;
    unsigned flags=0;
    const bool haveFlags=params->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,&flags)==NVSDK_NGX_Result_Success;
    e.networkHeight=creation.flags;
    if(creation.known)e.flags|=CreationKnown;
    if(g_inputExposure.autoExposure)e.flags|=Auto;
    if(creation.flags&NVSDK_NGX_DLSS_Feature_Flags_IsHDR)e.flags|=Hdr;
    const int driver=creation.driver>=0?creation.driver:rr;
    if(driver>=0)e.flags|=DriverKnown;
    if(driver==1)e.flags|=Rr;
    if(cfg.DlssNrEnabled.value_or_default())e.flags|=Nr;
    if(cfg.DlssNrWhitePointFromExposure.value_or_default())e.flags|=UseExposure;
    auto* target=list?GetResource(params,NVSDK_NGX_Parameter_Output,"DLSSD.Output"):nullptr;
    bool hdr=false;
    if(target){auto d=target->GetDesc();e.width=unsigned(d.Width);e.height=d.Height;e.networkWidth=d.Format;
        if(haveFlags)e.flags|=EffectiveHdrKnown;
        hdr=(flags&NVSDK_NGX_DLSS_Feature_Flags_IsHDR) && FormatCanHoldLinearHdr(d.Format);}
    if(hdr)e.flags|=EffectiveHdr;
    // Preview while NR is off/gated; DispatchLocked replaces this with the actual resolved value.
    const float p=g_inputExposure.preObserved && pre>1e-6f?pre:1.0f;
    const bool game=hdr && (e.flags&UseExposure) && g_nr.gameExposure>1e-6f;
    if(game)e.flags|=GameWhite;
    e.whitePoint=game?std::clamp(p/g_nr.gameExposure*cfg.DlssNrWhitePointScale.value_or_default(),0.01f,4096.0f):
                         cfg.DlssNrWhitePointScale.value_or_default();
    if(list && !auto_wp::Active())Sample(s,list,target,params,timingQueue,ow,oh,rrArrival,cfg);
    auto_wp::Diagnostic(e);
}
void Finish() {
    const auto& cfg=*Config::Instance();
    if(cfg.DlssNrDiagnostics.value_or_default()==0)return;
    auto& s=Get();auto& e=s.event;if(!e.frame)return;
    if(g_nr.exposureReadObserved){e.flags|=ExposureRead;e.mvScaleX=g_nr.exposureReadRaw;e.commandList=g_nr.exposureReadFrame;}
    if(s.latestLuma){e.flags|=Luma;e.mvScaleY=s.latestLog;e.queue=s.latestFrame;
        e.guideWidth=s.latestCount;e.guideHeight=s.latestFlags;}
    auto_wp::Diagnostic(e);
    const unsigned discrete=e.flags&~(ExposureRead|Luma|AppliedWhite);
    const auto now=GetTickCount64();
    const bool changed=!s.logged || discrete!=s.lastFlags || e.featureGeneration!=s.lastSource ||
        e.width!=s.lastWidth || e.height!=s.lastHeight || e.networkWidth!=s.lastFormat;
    if(!changed && now-s.lastLogMs<1000)return;
    s.logged=true;s.lastLogMs=now;s.lastFlags=discrete;s.lastSource=e.featureGeneration;
    s.lastWidth=e.width;s.lastHeight=e.height;s.lastFormat=e.networkWidth;
    e.reason=auto_wp::Active()?auto_wp::Status():s.status;
    DlssNr::Diagnostics::Record(static_cast<DlssNr::Diagnostics::Mode>(std::min(cfg.DlssNrDiagnostics.value_or_default(),2u)),e);
}
struct FinishOnExit {~FinishOnExit(){Finish();}};
void White(float value,bool hdr) {
    if(Config::Instance()->DlssNrDiagnostics.value_or_default()==0)return;
    auto& e=Get().event;e.whitePoint=value;e.flags|=AppliedWhite;
    e.flags&=~GameWhite;
    if(hdr && Config::Instance()->DlssNrWhitePointFromExposure.value_or_default() && g_nr.gameExposure>1e-6f)e.flags|=GameWhite;
}
} // namespace exposure_probe
