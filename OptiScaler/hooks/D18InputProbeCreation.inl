// Included inside D18InputProbe. Caller holds guard while editing registries.
inline const GUID deviceTag={0xe1be4459,0x9751,0x4ccd,{0xb7,0x9c,0x78,0xd9,0x73,0xd0,0x49,0x51}};
inline unsigned long long deviceSerial=0;
struct CreationBudget {unsigned count=0;unsigned long long bytes=0;};
inline CreationBudget deviceBudgets[17]{};
inline unsigned stageCounts[6]{};
inline unsigned long long creationRejected=0,creationFailures=0;
inline std::filesystem::path ProbeRoot(){
    HMODULE m=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&ProbeRoot),&m);
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(m,path,MAX_PATH);return std::filesystem::path(path).parent_path();
}
inline bool EnsureOutput(){
    if(probeReady)return true;
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);const auto name=std::filesystem::path(exe).filename().wstring();
    if(_wcsicmp(name.c_str(),L"GRW.exe")&&_wcsicmp(name.c_str(),L"d18-input-probe-host.exe"))return false;
    const auto root=ProbeRoot();
    DlssNr::Dx11CommandListWrites::researchTrace.store(DlssNr::BuildProfile::ResearchCaptureRequested(root),std::memory_order_relaxed);
    Wildlands::Enable(root);if(!Wildlands::enabled&&(!DlssNr::BuildProfile::Diagnostic||GetFileAttributesW((root/L"D18InputProbe.enabled").c_str())==INVALID_FILE_ATTRIBUTES))return false;
    if(!Wildlands::enabled||Config::Instance()->DlssNrDiagnostics.value_or_default()!=0)output=_wfsopen((root/L"D18InputProbe.jsonl").c_str(),L"wb",_SH_DENYNO);if(!output&&!Wildlands::enabled)return false;probeReady=true;
    if(!Wildlands::enabled)shaderArchive=_wfsopen((root/L"D18InputProbe.shaders.bin").c_str(),L"wb",_SH_DENYNO);Numeric::Enable(root);if(Wildlands::enabled)Numeric::enabled=false;
    LogInput("{\"event\":\"input_creation_begin\",\"schema\":\"d18-input-observation-v2\",\"archive_limit\":67108864,\"max_devices\":16,\"max_implementations_per_stage\":4,\"archive_open\":%s}\n",shaderArchive?"true":"false");FlushInput();return true;
}
inline unsigned long long DeviceId(ID3D11Device* d){unsigned long long id=0;UINT n=sizeof(id);if(FAILED(d->GetPrivateData(deviceTag,&n,&id)))return 0;return id;}
inline void CreationIdentity(ID3D11Device* d,ID3D11DeviceChild* shader,const void* data,SIZE_T size,unsigned stage,const char* name){
    if(Wildlands::enabled){
        if((stage!=0&&!(stage==5&&DlssNr::WildlandsSr::postReplay))||(!DlssNr::WildlandsSr::postReplay && size!=4164)||size>256*1024)return;
        std::lock_guard lock(guard);
        unsigned long long h=14695981039346656037ULL;for(SIZE_T i=0;i<size;++i)h=(h^static_cast<const unsigned char*>(data)[i])*1099511628211ULL;
        if(stage==5){
         auto info=DlssNr::Dx11ComputeIdentity::Describe(data,size,h);shader->SetPrivateData(DlssNr::Dx11ComputeIdentity::tag,sizeof(info),&info);
         DlssNr::Dx11ComputeIdentity::Cache(shader,data,UINT(size));Wildlands::PostReplay::computeArchive.Open(ProbeRoot());return;
        }
        if(DlssNr::WildlandsSr::postReplay)Wildlands::PostReplay::Register(shader,h);
        Numeric::Register(shader,h);return;
    }

    ++created;std::lock_guard lock(guard);if(!output||!shader||!data)return;
    const auto deviceId=DeviceId(d);
    const char* rejection=nullptr;
    if(!deviceId||deviceId>16)rejection="device_not_registered";
    else if(size>256*1024||size==0)rejection="individual_size";
    else if(deviceBudgets[deviceId].count>=8192||deviceBudgets[deviceId].bytes+size>64ULL*1024*1024)rejection="device_budget";
    else if(stageCounts[stage]>=4096)rejection="stage_count";
    else if(hashedBytes.load()+size>96ULL*1024*1024)rejection="global_hash_budget";
    if(rejection){++creationRejected;if(creationRejected<=32)LogInput("{\"event\":\"input_creation_rejected\",\"device_id\":%llu,\"stage\":\"%s\",\"reason\":\"%s\",\"bytes\":%llu}\n",deviceId,name,rejection,static_cast<unsigned long long>(size));return;}
    ++deviceBudgets[deviceId].count;deviceBudgets[deviceId].bytes+=size;++stageCounts[stage];hashedBytes+=size;
    unsigned long long hash=14695981039346656037ULL;
    for(SIZE_T i=0;i<size;++i)hash=(hash^static_cast<const unsigned char*>(data)[i])*1099511628211ULL;
    const unsigned role=stage==0?(size==448&&hash==0x92bf737dc635002aULL?1:size==1748&&hash==0x6277c995b757aee6ULL?2:0):0;
    ShaderInfo info{hash,static_cast<unsigned>(size),role,deviceId};
    const HRESULT status=shader->SetPrivateData(tag,sizeof(info),&info);
    if(FAILED(status)){++creationFailures;if(creationFailures<=32)LogInput("{\"event\":\"input_creation_tag_failed\",\"hresult\":%ld}\n",status);return;}
    if(role)++matched;
    Numeric::Register(shader,hash);
    if(shaderArchive&&archiveBytes+size<=64ULL*1024*1024&&!archived.count(hash)){
        const auto offset=archiveBytes;const auto written=fwrite(data,1,size,shaderArchive);archiveBytes+=written;
        if(written==size)archived.insert(hash);
        LogInput("{\"event\":\"input_shader_blob\",\"hash\":\"%016llx\",\"offset\":%llu,\"bytes\":%llu,\"complete\":%s}\n",hash,offset,static_cast<unsigned long long>(written),written==size?"true":"false");
    }
    LogInput("{\"event\":\"input_shader_identity\",\"hash\":\"%016llx\",\"bytes\":%u,\"role\":%u,\"stage\":\"%s\",\"device_id\":%llu,\"shader_id\":\"%p\",\"archive_present\":%s}\n",hash,info.bytes,role,name,deviceId,static_cast<void*>(shader),archived.count(hash)?"true":"false");
}
template<class Shader,unsigned Stage> struct CreationHook {
    using Fn=HRESULT(WINAPI*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,Shader**);
    inline static Fn originals[4]{};
    inline static void* entries[4]{};
    inline static unsigned count=0;
    inline static constexpr const char* name=Stage==0?"PS":Stage==1?"VS":Stage==2?"GS":Stage==3?"HS":Stage==4?"DS":"CS";
    template<unsigned N> static HRESULT WINAPI OnCreate(ID3D11Device* d,const void* data,SIZE_T size,ID3D11ClassLinkage* linkage,Shader** shader){
        const HRESULT hr=originals[N](d,data,size,linkage,shader);
        if(!internalWork&&SUCCEEDED(hr)&&shader&&*shader){try{CreationIdentity(d,*shader,data,size,Stage,name);}catch(...){++creationExceptions;}}
        return hr;
    }
    inline static std::atomic<unsigned long long> creationExceptions{0};
    static Fn Callback(unsigned i){Fn callbacks[]={OnCreate<0>,OnCreate<1>,OnCreate<2>,OnCreate<3>};return callbacks[i];}
    static bool Attach(void* entry,const char* label){
        (void)label;for(unsigned i=0;i<count;++i)if(entries[i]==entry)return true;
        if(count==4){LogInput("{\"event\":\"input_creation_hook_limit\",\"stage\":\"%s\"}\n",name);return false;}
        const unsigned i=count;entries[i]=entry;originals[i]=reinterpret_cast<Fn>(entry);
        LONG status=DetourTransactionBegin();
        if(status==NO_ERROR){status=DetourUpdateThread(GetCurrentThread());if(status==NO_ERROR)status=DetourAttach(reinterpret_cast<PVOID*>(&originals[i]),Callback(i));if(status==NO_ERROR)status=DetourTransactionCommit();else DetourTransactionAbort();}
        LogInput("{\"event\":\"input_creation_hook\",\"stage\":\"%s\",\"entry\":\"%p\",\"hook_status\":%ld}\n",name,entry,status);
        if(status==NO_ERROR){++count;return true;}entries[i]=nullptr;originals[i]=nullptr;return false;
    }
    static LONG Detach(){for(unsigned i=0;i<count;++i){const auto status=DetourDetach(reinterpret_cast<PVOID*>(&originals[i]),Callback(i));if(status!=NO_ERROR)return status;}return NO_ERROR;}
    static void Reset(){count=0;for(unsigned i=0;i<4;++i){entries[i]=nullptr;originals[i]=nullptr;}}
};
using PixelCreation=CreationHook<ID3D11PixelShader,0>;
using VertexCreation=CreationHook<ID3D11VertexShader,1>;
using GeometryCreation=CreationHook<ID3D11GeometryShader,2>;
using HullCreation=CreationHook<ID3D11HullShader,3>;
using DomainCreation=CreationHook<ID3D11DomainShader,4>;
using ComputeCreation=CreationHook<ID3D11ComputeShader,5>;
inline void EarlyDevice(ID3D11Device* device){
    if(!device)return;
    try{std::lock_guard lock(guard);if(!EnsureOutput())return;
        auto id=DeviceId(device);if(!id){if(deviceSerial>=16)return;id=++deviceSerial;if(FAILED(device->SetPrivateData(deviceTag,sizeof(id),&id)))return;}
        auto vt=*reinterpret_cast<void***>(device);
        // Native rendering consumes PS identities and, for post replay, CS identities.
        // The separate diagnostic observer still covers all six stages.
        const bool allStages=!Wildlands::enabled;
        const bool ps=PixelCreation::Attach(vt[15],"PS");
        const bool vs=allStages&&VertexCreation::Attach(vt[12],"VS");
        const bool gs=allStages&&GeometryCreation::Attach(vt[13],"GS");
        const bool hs=allStages&&HullCreation::Attach(vt[16],"HS");
        const bool ds=allStages&&DomainCreation::Attach(vt[17],"DS");
        const bool needCs=allStages||DlssNr::WildlandsSr::postReplay;
        const bool cs=needCs&&ComputeCreation::Attach(vt[18],"CS");
        LogInput("{\"event\":\"input_creation_policy\",\"device_id\":%llu,\"required_stage_mask\":%u,\"attached_stage_mask\":%u}\n",id,allStages?63u:(needCs?33u:1u),unsigned(ps)|(unsigned(vs)<<1)|(unsigned(gs)<<2)|(unsigned(hs)<<3)|(unsigned(ds)<<4)|(unsigned(cs)<<5));
        // Log registrations once, including the actual feature level; selection occurs at swapchain.
        static unsigned records=0;if(records++<32)LogInput("{\"event\":\"input_creation_device\",\"device_id\":%llu,\"device\":\"%p\",\"feature_level\":%u,\"ps\":%s,\"vs\":%s,\"gs\":%s,\"hs\":%s,\"ds\":%s,\"cs\":%s}\n",id,static_cast<void*>(device),unsigned(device->GetFeatureLevel()),ps?"true":"false",vs?"true":"false",gs?"true":"false",hs?"true":"false",ds?"true":"false",cs?"true":"false");FlushInput();
    }catch(...){/* Diagnostic setup never changes device creation HRESULT. */}
}
inline LONG DetachCreation(){
    LONG status=PixelCreation::Detach();if(status==NO_ERROR)status=VertexCreation::Detach();if(status==NO_ERROR)status=GeometryCreation::Detach();if(status==NO_ERROR)status=HullCreation::Detach();if(status==NO_ERROR)status=DomainCreation::Detach();if(status==NO_ERROR)status=ComputeCreation::Detach();return status;
}
inline void ResetCreation(){PixelCreation::Reset();VertexCreation::Reset();GeometryCreation::Reset();HullCreation::Reset();DomainCreation::Reset();ComputeCreation::Reset();}
inline void CreationStats(){if(output)LogInput("{\"event\":\"input_creation_stats\",\"rejected\":%llu,\"tag_failures\":%llu,\"exceptions\":%llu,\"hashed_bytes\":%llu,\"archive_bytes\":%llu}\n",creationRejected,creationFailures,PixelCreation::creationExceptions.load()+VertexCreation::creationExceptions.load()+GeometryCreation::creationExceptions.load()+HullCreation::creationExceptions.load()+DomainCreation::creationExceptions.load()+ComputeCreation::creationExceptions.load(),hashedBytes.load(),archiveBytes);}
