// Under g_nrMutex. Pin the canonical device identity to prevent address reuse.
Microsoft::WRL::ComPtr<IUnknown> g_memoryDevice;
DlssNr::MemoryIdentity g_memoryIdentity;
DlssNr::MemoryAdapterCache<Microsoft::WRL::ComPtr<IDXGIAdapter3>> g_memoryAdapter;
DlssNr::MemoryDiagnostics g_memoryDiagnostics;
void ResetNrMemory() {
    g_memoryAdapter.Reset();g_memoryDevice.Reset();g_memoryIdentity={};g_memoryDiagnostics.Reset();
}
DlssNr::MemorySample ReadNrMemory(ID3D12Device* device) {
    Microsoft::WRL::ComPtr<IUnknown> identity;
    if(!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&identity))))return {};
    const auto luid=device->GetAdapterLuid();
    DlssNr::MemoryIdentity key{reinterpret_cast<uintptr_t>(identity.Get()),
        (uint64_t(uint32_t(luid.HighPart))<<32)|luid.LowPart};
    if(key!=g_memoryIdentity){g_memoryAdapter.Reset();g_memoryDevice=identity;g_memoryIdentity=key;}
    return g_memoryAdapter.Read(key,GetTickCount64(),[&]{
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
        auto* swapchain=State::Instance().currentRealSwapchain;
        if(!swapchain)swapchain=State::Instance().currentSwapchain;
        if(swapchain)swapchain->GetParent(IID_PPV_ARGS(&factory));
        if(factory)factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter));
        // Bridges need not have a DXGI swapchain. Keep their previous fallback,
        // but only resolve it on a cache miss, never for each valid budget read.
        if(!adapter){
            factory.Reset();
            if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
                factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter));
        }
        return adapter;
    },[](const auto& adapter){
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        const HRESULT hr=adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info);
        return DlssNr::MemorySample{info.Budget,info.CurrentUsage,uint32_t(hr),SUCCEEDED(hr)};
    });
}
bool MemoryDiagnosticsEnabled(const Config& cfg){return cfg.DlssNrDiagnostics.value_or_default()!=0;}
void RecordNrMemory(const Config& cfg,const char* phase,DlssNr::MemorySample sample,
                    unsigned pass,uint64_t generation,unsigned width,unsigned height,float ratio,
                    uint64_t proposed=0,uint64_t reserve=0,bool admitted=false) {
    const auto mode=static_cast<DlssNr::Diagnostics::Mode>(std::min(cfg.DlssNrDiagnostics.value_or_default(),2u));
    DlssNr::Diagnostics::Event e{};
    e.frame=g_frames;e.featureGeneration=generation;e.flags=pass<<8;
    // nr_memory-specific payload: these are device identity/LUID, not a queue/list.
    e.queue=g_memoryIdentity.luid;e.commandList=g_memoryIdentity.device;
    e.width=width;e.height=height;e.ratio=ratio;
    e.networkWidth=DlssNr::Multipass::Network(width,ratio,16);
    e.networkHeight=DlssNr::Multipass::Network(height,ratio,8);
    g_memoryDiagnostics.Record(mode,GetTickCount64(),e,phase,sample,proposed,reserve,admitted,
        [](auto m,const auto& event){DlssNr::Diagnostics::Record(m,event);});
}
