// Render-owner state uses g_nrMutex. The observer touches only SubmissionBatch/pending.
namespace auto_wp {
using Microsoft::WRL::ComPtr;
using Controller=DlssNr::AutoWhitePoint::Controller;
std::atomic<bool> pendingCopy=false;
bool everGameExposure=false; // session latch; intentionally survives ordinary NR shutdown/rebuild
enum Source : unsigned { Fixed, Game, Estimate, Hold, Waiting };
struct State {
    capture::SubmissionBatch submission;
    ComPtr<ID3D12Device> device, contextDevice;
    ComPtr<ID3D12Resource> grid, readback, source;
    ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    Controller control;
    bool active=false, allowed=false, failed=false, latest=false;
    uint64_t epoch=0, sampleEpoch=0, contextSource=0, sampleFrame=0, frame=0;
    uint64_t lastSampleMs=0, latestFrame=0;
    unsigned sampleFlags=0,latestFlags=0;
    unsigned x=0,y=0,w=0,h=0,format=0,latestCount=0,sourceKind=Fixed;
    float samplePre=1, sampleKey=.05f, currentPre=1, raw=0, white=1;
    double sampleTime=0;
    const char* unavailable="Estimate unavailable: waiting for a DX12 HDR frame.";
    const char* status="waiting_first_measurement";
};
// In-flight/discarded lists keep every GPU object pinned, including across shutdown.
State& Get(){static auto* state=new State;return *state;}
bool Active(){return Get().active;}
void Disable(){auto& s=Get();if(s.active){++s.epoch;s.control={};s.latest=false;}s.active=false;s.allowed=false;
    s.unavailable="Estimate unavailable: waiting for a DX12 HDR frame.";}
bool Init(State& s,ID3D12Device* d) {
    if(s.device.Get()!=d) {
        s.device=d;s.grid.Reset();s.readback.Reset();s.heap.Reset();s.root.Reset();s.pso.Reset();s.failed=false;
    }
    if(s.failed)return false;
    if(s.pso)return true;
    s.failed=true; // bounded retry: a different device may try again
    D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},
                                    {D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1}};
    D3D12_ROOT_PARAMETER p[2]{};
    p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[0].Constants={0,0,4};
    p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[1].DescriptorTable={2,ranges};
    D3D12_ROOT_SIGNATURE_DESC rd{2,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob,err,code;
    if(FAILED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&err)) ||
       FAILED(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&s.root))) ||
       FAILED(D3DCompile(DlssNr::AutoWhitePoint::ShaderSource,sizeof(DlssNr::AutoWhitePoint::ShaderSource)-1,
           "D18_AutoWhitePoint",nullptr,nullptr,"Measure","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&err)))return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=s.root.Get();pd.CS={code->GetBufferPointer(),code->GetBufferSize()};
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    const auto def=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), rb=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    auto gd=CD3DX12_RESOURCE_DESC::Buffer(DlssNr::AutoWhitePoint::Cells*sizeof(float),D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto bd=CD3DX12_RESOURCE_DESC::Buffer(DlssNr::AutoWhitePoint::Cells*sizeof(float));
    if(FAILED(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&s.heap))) ||
       FAILED(d->CreateCommittedResource(&def,D3D12_HEAP_FLAG_NONE,&gd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&s.grid))) ||
       FAILED(d->CreateCommittedResource(&rb,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&s.readback))) ||
       FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&s.pso)))){s.pso.Reset();return false;}
    s.failed=false;return true;
}
void Poll(State& s,double now) {
    if(!pendingCopy.load() || !s.submission.complete())return;
    // Old context/reset/disabled readbacks are retired only after actual completion, never consumed.
    if(s.active && s.sampleEpoch==s.epoch) {
        void* data=nullptr;D3D12_RANGE range{0,DlssNr::AutoWhitePoint::Cells*sizeof(float)};
        if(SUCCEEDED(s.readback->Map(0,&range,&data)) && data) {
            const auto m=DlssNr::AutoWhitePoint::Measure(static_cast<const float*>(data),DlssNr::AutoWhitePoint::Cells);
            s.latest=m.count>0;s.raw=m.raw;s.latestCount=unsigned(m.count);
            s.latestFrame=s.sampleFrame;s.latestFlags=s.sampleFlags;
            s.status=s.control.Accept(m,s.samplePre,s.sampleKey,s.sampleTime,now)?"ready":
                m.valid?"stale_sample":"black_or_nonfinite_sample";
            D3D12_RANGE empty{};s.readback->Unmap(0,&empty);
        } else s.status="map_failed";
    }
    s.submission.clearCompleted();s.source.Reset();pendingCopy.store(false);
}
void Sample(State& s,ID3D12GraphicsCommandList* list,ID3D12Resource* target,
            ID3D12Device* d,bool rr,const Config& cfg,uint64_t nowMs) {
    if(nowMs-s.lastSampleMs<100)return;
    s.lastSampleMs=nowMs;
    if(pendingCopy.load()){s.status="awaiting_submission_or_gpu";return;}
    // Match the NR host-state envelope and its restoration gate before changing any bindings.
    if((cfg.RestoreComputeSignature.value_or_default() || cfg.RestoreGraphicSignature.value_or_default()) &&
       !D3D12Hooks::CanRestoreNrState(list)){s.status="host_state_unavailable";return;}
    if(!DlssNr::EnsureNativeSubmissionObserver(d)){s.status="observer_unavailable";return;}
    if(!Init(s,d)){s.status="allocation_or_pipeline_failed";return;}
    if(!s.submission.arm(d,list)){s.status="fence_unavailable";return;}
    auto handle=s.heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=target->GetDesc().Format;
    srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    d->CreateShaderResourceView(target,&srv,handle);
    handle.ptr+=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements=unsigned(DlssNr::AutoWhitePoint::Cells);uav.Buffer.StructureByteStride=sizeof(float);
    d->CreateUnorderedAccessView(s.grid.Get(),nullptr,&uav,handle);
    s.source=target;s.sampleEpoch=s.epoch;s.samplePre=s.currentPre;
    const int sampleDriver=DlssNr::ExposureObservation::Read(unsigned(s.contextSource)).driver;
    s.sampleFlags=(cfg.DlssNrEnabled.value_or_default()?128u:0u)|(sampleDriver>=0?256u:0u)|(sampleDriver==1?512u:0u);
    s.sampleKey=cfg.DlssNrWhitePointAutoKey.value_or_default();s.sampleTime=nowMs/1000.0;s.sampleFrame=s.frame;
    const auto arrival=rr?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
        cfg.OutputResourceBarrier.has_value()?D3D12_RESOURCE_STATES(cfg.OutputResourceBarrier.value()):D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ScopedNrStateEnvelope envelope(list);
    Barrier(list,target,arrival,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap* heaps[]={s.heap.Get()};list->SetDescriptorHeaps(1,heaps);
    list->SetComputeRootSignature(s.root.Get());list->SetPipelineState(s.pso.Get());
    const unsigned rect[]={s.x,s.y,s.w,s.h};list->SetComputeRoot32BitConstants(0,4,rect,0);
    list->SetComputeRootDescriptorTable(1,s.heap->GetGPUDescriptorHandleForHeapStart());
    list->Dispatch(8,8,1);
    Barrier(list,s.grid.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(s.readback.Get(),0,s.grid.Get(),0,DlssNr::AutoWhitePoint::Cells*sizeof(float));
    Barrier(list,s.grid.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Barrier(list,target,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,arrival);
    pendingCopy.store(true);s.status="sample_recorded";
}
void Begin(ID3D12GraphicsCommandList* list,NVSDK_NGX_Parameter* params,unsigned ow,unsigned oh,
           bool rr,uint64_t source,bool hdr,ID3D12Resource* target) {
    auto& s=Get();const auto& cfg=*Config::Instance();const auto nowMs=GetTickCount64();const double now=nowMs/1000.0;
    ++s.frame;
    s.currentPre=DlssNr::AutoWhitePoint::Pre(g_inputExposure.rawPreExposure);
    s.allowed=list && params && target && hdr && !everGameExposure &&
        g_inputExposure.scaleObserved && g_inputExposure.exposureScale==1.0f;
    s.unavailable=!list || !params || !target?"Estimate unavailable: waiting for a DX12 HDR frame.":
        !hdr?"Estimate unavailable: input is not linear HDR.":
        everGameExposure?"Estimate unavailable: game exposure has been read this session.":
        !g_inputExposure.scaleObserved || g_inputExposure.exposureScale!=1.0f?
        "Estimate unavailable: Exposure_Scale is unobserved or is not 1.":"";
    const bool active=s.allowed && cfg.WhitePointAutoEnabled();
    if(active!=s.active){++s.epoch;s.control={};s.latest=false;s.active=active;}
    ComPtr<ID3D12Device> device;
    unsigned x=0,y=0,w=0,h=0;
    if(active) {
        const auto d=target->GetDesc();w=unsigned(d.Width);h=d.Height;unsigned rw=0,rh=0;
        params->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X,&x);params->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y,&y);
        params->Get(NVSDK_NGX_Parameter_OutWidth,&w);params->Get(NVSDK_NGX_Parameter_OutHeight,&h);
        params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,&rw);params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,&rh);
        const auto rect=DlssNr::ResolveSrOutputRect(_wcsicmp(Util::ExePath().filename().c_str(),L"yysls.exe")==0,
            rr,{x,y,w,h},rw,rh,d.Width,d.Height,ow,oh);w=rect.width;h=rect.height;
        target->GetDevice(IID_PPV_ARGS(&device));
        if(device.Get()!=s.contextDevice.Get() || source!=s.contextSource) {
            ++s.epoch;s.control={};s.latest=false;s.contextDevice=device;s.contextSource=source;
        }
        if(x!=s.x || y!=s.y || w!=s.w || h!=s.h || unsigned(d.Format)!=s.format) {
            ++s.epoch;s.latest=false;s.x=x;s.y=y;s.w=w;s.h=h;s.format=d.Format;
        }
        int reset=0;params->Get(NVSDK_NGX_Parameter_Reset,&reset);
        if(reset){++s.epoch;s.control.ResetSample();s.latest=false;}
    }
    Poll(s,now); // also drain already recorded work after disabling, without writing diagnostics
    if(active) {
        s.control.Frame(now);
        // NR off and diagnostics off: record nothing on the game's list; the last estimate is held.
        if(!cfg.DlssNrEnabled.value_or_default() && cfg.DlssNrDiagnostics.value_or_default()==0) s.status="nr_disabled";
        else if(device && w && h && uint64_t(x)+w<=target->GetDesc().Width && uint64_t(y)+h<=target->GetDesc().Height &&
           target->GetDesc().Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && target->GetDesc().SampleDesc.Count==1 &&
           target->GetDesc().DepthOrArraySize==1 && exposure_probe::Supported(target->GetDesc().Format) && !(target->GetDesc().Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
            Sample(s,list,target,device.Get(),rr,cfg,nowMs);
        else s.status="invalid_output_contract";
    }
    s.sourceKind=active?(s.control.ready?(s.control.Holding(now)?Hold:Estimate):Waiting):
        hdr && cfg.DlssNrWhitePointFromExposure.value_or_default() && g_nr.gameExposure>1e-6f?Game:Fixed;
    s.white=active?s.control.White(s.currentPre,cfg.DlssNrWhitePointAutoTrim.value_or_default(),cfg.DlssNrWhitePointScale.value_or_default()):
        ResolveWhitePoint(cfg,hdr);
}
float Resolve(const Config& cfg,bool hdr) {
    auto& s=Get();
    // ConsumeMeterReadback may have supplied the first usable game exposure after Begin.
    if(everGameExposure && s.active) { Disable();
        s.unavailable="Estimate unavailable: game exposure has been read this session."; }
    if(!hdr || !s.active){s.sourceKind=hdr && cfg.DlssNrWhitePointFromExposure.value_or_default() && g_nr.gameExposure>1e-6f?Game:Fixed;
        return s.white=ResolveWhitePoint(cfg,hdr);}
    return s.white=s.control.White(s.currentPre,cfg.DlssNrWhitePointAutoTrim.value_or_default(),cfg.DlssNrWhitePointScale.value_or_default());
}
const char* Status(){return Get().status;}
void Diagnostic(DlssNr::Diagnostics::Event& e) {
    const auto& s=Get();
    e.whitePoint=s.white;e.flags&=~(1024u|131072u|262144u|524288u|1048576u);
    if(s.sourceKind==Game)e.flags|=1024;
    if(s.active) {
        e.flags|=131072;
        if(s.sourceKind==Hold)e.flags|=262144;
        if(s.sourceKind==Waiting)e.flags|=524288;
        if(s.control.ready){const float b=float(s.control.b);std::memcpy(&e.result,&b,sizeof(b));e.flags|=1048576;}
        if(s.latest){e.flags|=4096;e.mvScaleY=s.raw;e.queue=s.latestFrame;
            e.guideWidth=s.latestCount;e.guideHeight=s.latestFlags;}
        else e.flags&=~4096u;
    }
}
}
