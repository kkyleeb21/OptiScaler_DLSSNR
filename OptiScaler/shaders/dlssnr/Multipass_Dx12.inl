// Included inside the existing DX12 renderer's anonymous namespace. All access
// is under g_nrMutex; creation/evaluation use the same observed queue ticket.
struct NrAdditionalPass {
    void* feature=nullptr;
    NVSDK_NGX_Parameter* params=nullptr;
    DlssNr::Multipass::Tuning built{};
    DlssNr::Submission::Token created,lastUse;
    uint32_t width=0,height=0;
    bool linearResolve=false,linearColor=false,reset=true,blocked=false,announced=false;
    uint64_t generation=0;
};
struct NrMultipassState {
    std::array<NrAdditionalPass,3> passes;
    ID3D12Resource* input=nullptr;
    ID3D12Resource* answer=nullptr;
    ID3D12Resource* delta[2]{};
    ID3D12Resource* zeroMotion=nullptr;
    DlssNr::Submission::Token scratchUse;
    DlssNr::CompletedInitialization<DlssNr::Submission::Token> zeroInitialized;
    uint32_t width=0,height=0,requested=1,ready=1,recorded=0;
    uint64_t generation=0,baseGeneration=0;
    const char* reason="";
    bool shared=false,customInput=false,catmullInput=false;
} g_multi;

void ResetAdditional(unsigned from=0) {for(unsigned i=from;i<3;++i)g_multi.passes[i].reset=true;}
bool AdditionalIdle(const NrAdditionalPass& pass){return !pass.lastUse || pass.lastUse->Complete();}
bool ReleaseAdditional(NrAdditionalPass& pass) {
    if(!AdditionalIdle(pass))return false;
    if(pass.feature)g_nr.release(pass.feature);
    if(pass.params && NVNGXProxy::D3D12_DestroyParameters())NVNGXProxy::D3D12_DestroyParameters()(pass.params);
    pass=NrAdditionalPass{};return true;
}
bool ReleaseMultipassScratch() {
    if(g_multi.scratchUse&&!g_multi.scratchUse->Complete())return false;
    for(auto** p:{&g_multi.input,&g_multi.answer,&g_multi.delta[0],&g_multi.delta[1],&g_multi.zeroMotion})if(*p){(*p)->Release();*p=nullptr;}
    g_multi.width=g_multi.height=0;g_multi.scratchUse.reset();g_multi.zeroInitialized.Reset();return true;
}
void MultipassEvent(const Config& cfg,const char* type,unsigned index,uint32_t result,
                   const char* reason="",bool trace=false,bool reset=false) {
    const auto mode=static_cast<DlssNr::Diagnostics::Mode>(std::min(cfg.DlssNrDiagnostics.value_or_default(),2u));
    if(mode==DlssNr::Diagnostics::Mode::Off)return;
    DlssNr::Diagnostics::Event e{};e.type=type;e.reason=reason;e.frame=g_frames;e.result=result;
    e.flags=DlssNr::Multipass::Flags(index+1,g_multi.requested,g_multi.ready,g_multi.recorded,reset);
    if(index){const auto& p=g_multi.passes[index-1];e.featureGeneration=p.generation;e.ratio=p.built.scaling?p.built.ratio:1;
        e.width=p.width;e.height=p.height;e.networkWidth=DlssNr::Multipass::Network(p.width,e.ratio,16);e.networkHeight=DlssNr::Multipass::Network(p.height,e.ratio,8);
        const auto ticket=g_multi.shared?g_submission:p.lastUse;
        if(ticket){e.queue=reinterpret_cast<uint64_t>(DlssNr::Submission::owner);e.fenceTarget=1;e.fenceCompleted=ticket->fence->GetCompletedValue();if(ticket->submitted.load())e.flags|=16;if(ticket->Complete())e.flags|=32;}
        if(g_multi.shared){const auto t=DlssNr::Multipass::Read(cfg,0);e.width=g_nr.width;e.height=g_nr.height;e.ratio=t.scaling?t.ratio:1;e.networkWidth=g_nr.networkWidth;e.networkHeight=g_nr.networkHeight;e.featureGeneration=g_multi.baseGeneration;}}
    DlssNr::Diagnostics::Record(mode,e,trace);
}
// Fixed 32-event process budget, emitted only when the contract changes and diagnostics is enabled.
// Keep the actual DX12 backend separate from the generic upscaler API selector.
void RecordMultipassContract(const Config& cfg,DXGI_FORMAT format,unsigned effective,unsigned width,unsigned height) {
    const auto mode=static_cast<DlssNr::Diagnostics::Mode>(std::min<unsigned>(cfg.DlssNrDiagnostics.value_or_default(),2));
    if(mode==DlssNr::Diagnostics::Mode::Off)return;
    const unsigned requested=DlssNr::Multipass::Count(cfg.DlssNrPassCount.value_or_default(),false);
    const unsigned api=unsigned(State::Instance().api);
    const uint64_t signature=uint64_t(format)|(uint64_t(requested)<<32)|(uint64_t(effective)<<36)|(uint64_t(api)<<40);
    static uint64_t previous=UINT64_MAX;static unsigned emitted=0;
    if(previous==signature || emitted>=32)return;
    previous=signature;++emitted;
    char reason[96]{};
    snprintf(reason,sizeof(reason),"backend=dx12;format=%u;global_api=%u;requested=%u;effective=%u",unsigned(format),api,requested,effective);
    DlssNr::Diagnostics::Event e{};e.type="mp_contract";e.reason=reason;e.frame=g_frames;
    e.width=width;e.height=height;e.result=effective;e.flags=unsigned(format);
    DlssNr::Diagnostics::Record(mode,e);
}
void RecordLatchedNrFailure(const Config& cfg,const D3D12_RESOURCE_DESC& desc) {
    const auto mode=static_cast<DlssNr::Diagnostics::Mode>(std::min<unsigned>(cfg.DlssNrDiagnostics.value_or_default(),2));
    if(mode==DlssNr::Diagnostics::Mode::Off)return;
    static std::array<char,256> previous{};static unsigned emitted=0;
    if(emitted>=32 || std::strcmp(previous.data(),g_nr.reason)==0)return;
    std::snprintf(previous.data(),previous.size(),"%s",g_nr.reason);++emitted;
    DlssNr::Diagnostics::Event e{};e.type="nr_failure_state";e.reason=g_nr.reason;
    e.frame=g_frames;e.width=unsigned(desc.Width);e.height=desc.Height;e.flags=unsigned(desc.Format);
    e.result=g_deviceLost?1:0;
    DlssNr::Diagnostics::Record(mode,e);
}
// Returns false only after recording creation work: no evaluate in that batch.
// Pending edits stop at a contiguous ready prefix. Never free an in-flight object.
bool PrepareAdditional(const Config& cfg,ID3D12Device* device,ID3D12GraphicsCommandList* list,
                       const DlssNr::Submission::Token& ticket,unsigned count,unsigned width,unsigned height,
                       uint64_t baseGeneration,ID3D12Resource* motion) {
    const bool customInput=cfg.DlssNrCustomColorFilter.value_or_default();
    const bool catmullInput=cfg.DlssNrCatmullRomInput.value_or_default();
    if(g_multi.customInput!=customInput || g_multi.catmullInput!=catmullInput){ResetAdditional();g_multi.customInput=customInput;g_multi.catmullInput=catmullInput;}
    const bool countChanged=g_multi.requested!=count;
    g_multi.requested=count;g_multi.ready=1;g_multi.recorded=0;g_multi.reason="";
    bool compatible=true;
    for(unsigned i=1;i<count;++i)if(DlssNr::Multipass::Read(cfg,i)!=DlssNr::Multipass::Read(cfg,0))compatible=false;
    const bool requestedShared=DlssNr::BuildProfile::SharedHistoryResearch && cfg.DlssNrSharedHistory.value_or_default();
    const bool shared=count>1 && requestedShared && compatible;
    if(g_multi.shared!=shared){g_nr.reset=true;ResetAdditional();g_multi.shared=shared;}
    if(g_multi.baseGeneration!=baseGeneration){ResetAdditional();g_multi.baseGeneration=baseGeneration;}
    if(countChanged){if(shared)g_nr.reset=true;for(auto& p:g_multi.passes)p.blocked=false;}
    for(unsigned i=shared?0:count-1;i<3;++i)if(ReleaseAdditional(g_multi.passes[i])){}else g_multi.passes[i].reset=true;
    if(count==1){ReleaseMultipassScratch();return true;}
    if(requestedShared&&!compatible){g_multi.reason="Shared history requires identical parameters and ratios for all active passes";return true;}
    if((g_multi.width!=width || g_multi.height!=height) && !ReleaseMultipassScratch()) {
        g_multi.reason="Multipass scratch awaits GPU completion";return true;
    }
    const auto motionDesc=motion->GetDesc();
    if(g_multi.zeroMotion && (g_multi.zeroMotion->GetDesc().Width!=motionDesc.Width ||
       g_multi.zeroMotion->GetDesc().Height!=motionDesc.Height || g_multi.zeroMotion->GetDesc().Format!=TypedGuideFormat(motionDesc.Format))) {
        if(!ReleaseMultipassScratch()){g_multi.reason="Shared motion resource awaits GPU completion";return true;}
    }
    if(shared)g_multi.ready=count;
    for(unsigned i=0;!shared && i<count-1;++i) {
        auto& pass=g_multi.passes[i];const auto tuning=DlssNr::Multipass::Read(cfg,i+1);
        const bool changed=pass.width!=width || pass.height!=height || pass.built!=tuning ||
            pass.linearResolve!=g_nr.builtLinearResolve || pass.linearColor!=g_nr.builtLinearColorInput;
        if(changed) {
            ResetAdditional(i);
            if(!ReleaseAdditional(pass)){g_multi.reason="Pass edit awaits GPU completion";break;}
            pass.built=tuning;pass.width=width;pass.height=height;
            pass.linearResolve=g_nr.builtLinearResolve;pass.linearColor=g_nr.builtLinearColorInput;
        }
        if(pass.blocked){g_multi.reason="Additional pass unavailable; change settings or reselect pass count to retry";break;}
        if(!pass.feature) {
            // A failed create can have recorded GPU work referencing its parameters.
            if(!AdditionalIdle(pass)){g_multi.reason="Pass creation awaits GPU completion";break;}
            if(!g_nrRetired.empty()){g_multi.reason="Previous NR generation awaits GPU completion";break;}
            const auto memory=ReadNrMemory(device);
            const auto proposed=g_multi.input?0:uint64_t(width)*height*8*4;
            const auto reserve=DlssNr::Multipass::Reserve(width,height,tuning);
            if(!memory.Allows(proposed,reserve)) {
                RecordNrMemory(cfg,"admission",memory,i+2,0,width,height,tuning.scaling?tuning.ratio:1,proposed,reserve,false);
                g_multi.reason="Insufficient observed video-memory headroom for another NR pass";
                pass.blocked=true;MultipassEvent(cfg,"mp_admission",i+1,0,"video_memory_headroom");break;
            }
            auto snippet=Util::FindFilePath(g_dllDir,"nvngx_dlssnr.dll");
            if(!snippet)snippet=Util::FindFilePath(Util::ExePath().remove_filename(),"nvngx_dlssnr.dll");
            const auto allocate=NVNGXProxy::D3D12_GetCapabilityParameters();
            if(!snippet || !allocate || !NVNGXProxy::D3D12_DestroyParameters() ||
               allocate(&pass.params)!=NVSDK_NGX_Result_Success || !pass.params){pass.blocked=true;g_multi.reason="Additional NR parameter allocation failed";break;}
            // Driver-owned capability blocks carry callbacks missing from plain AllocateParameters.
            // A backend returning a singleton cannot provide independent writable parameter state.
            bool alias=pass.params==g_nr.capabilityParams;
            for(const auto& other:g_multi.passes)if(&other!=&pass && other.params==pass.params)alias=true;
            if(alias){pass.params=nullptr;pass.blocked=true;g_multi.reason="Additional NR parameter allocation failed";break;}
            if(g_nr.setExtras)g_nr.setExtras(pass.params,1,nullptr,nullptr,nullptr,0,0,0,0);
            pass.lastUse=pass.created=ticket;pass.generation=++g_multi.generation;
            RecordNrMemory(cfg,"before",memory,i+2,pass.generation,width,height,tuning.scaling?tuning.ratio:1,proposed,reserve,true);
            RecordNrCommandListUse(list);
            pass.feature=g_nr.create(snippet->wstring().c_str(),State::Instance().NVNGX_ApplicationDataPath.c_str(),
                device,list,pass.params,width,height,int(tuning.preset),tuning.intensity,int(tuning.style),tuning.structure,
                tuning.tone,tuning.skin,tuning.autoMask?1:0,1,tuning.scaling?tuning.ratio:1);
            if(MemoryDiagnosticsEnabled(cfg))RecordNrMemory(cfg,"created",ReadNrMemory(device),i+2,pass.generation,
                width,height,tuning.scaling?tuning.ratio:1);
            pass.blocked=!pass.feature;pass.reset=true;
            MultipassEvent(cfg,"mp_create",i+1,uint32_t(g_nr.lastCreate?*g_nr.lastCreate:0));
            if(FAILED(device->GetDeviceRemovedReason())){g_deviceLost=true;g_nr.failed=true;g_nr.reason="Device lost during additional NR creation";}
            return false;
        }
        if(!pass.created || !pass.created->Complete()){g_multi.reason="Additional NR creation awaits GPU completion";break;}
        ++g_multi.ready;
        if(!pass.announced){
            if(MemoryDiagnosticsEnabled(cfg))RecordNrMemory(cfg,"gpu_ready",ReadNrMemory(device),i+2,pass.generation,
                width,height,tuning.scaling?tuning.ratio:1);
            MultipassEvent(cfg,"mp_ready",i+1,1,"creation_gpu_complete");pass.announced=true;
        }
    }
    if(g_multi.ready>1 && (!g_multi.input || (shared&&!g_multi.zeroMotion))) {
        const uint64_t bytes=(g_multi.input?0:uint64_t(width)*height*8*4)+(shared&&!g_multi.zeroMotion?uint64_t(motionDesc.Width)*motionDesc.Height*16:0);
        const auto memory=ReadNrMemory(device);
        RecordNrMemory(cfg,"scratch",memory,0,g_multi.generation,width,height,1,bytes,256ull*1024*1024,
            memory.Allows(bytes,256ull*1024*1024));
        if(!memory.Allows(bytes,256ull*1024*1024)){g_multi.ready=1;ResetAdditional();g_multi.reason="Insufficient scratch headroom; single pass retained";return true;}
        DlssNr::AllocationBatch<ID3D12Resource> batch;
        bool ok=true;
        for(auto** p:{&g_multi.input,&g_multi.answer,&g_multi.delta[0],&g_multi.delta[1]})
            if(!batch.Ensure(*p,[&]{return CreateScratch(device,DXGI_FORMAT_R16G16B16A16_FLOAT,width,height,"multipass scratch",false);})) {ok=false;break;}
        if(ok&&shared)ok=batch.Ensure(g_multi.zeroMotion,[&]{return CreateScratch(device,TypedGuideFormat(motionDesc.Format),unsigned(motionDesc.Width),motionDesc.Height,"shared-history zero motion",false);});
        if(!ok){g_multi.ready=1;g_multi.reason="Multipass scratch allocation failed; single pass retained";}
        else {batch.Commit();g_multi.width=width;g_multi.height=height;}
    }
    if(g_multi.ready<count)ResetAdditional(g_multi.ready-1);
    return true;
}
