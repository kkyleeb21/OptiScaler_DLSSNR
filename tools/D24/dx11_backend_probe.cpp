// Isolated, fixed-build research host. NR modes adapt process memory only; never write runtime/game files.
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cstdarg>
#include <cmath>
#include "dx11_probe_parameters.h"
#pragma comment(lib,"d3d11.lib")
static void event(const char* name, long long value) { printf("{\"event\":\"%s\",\"value\":%lld}\n",name,value); fflush(stdout); }
static unsigned char* imageBase=nullptr;
static void diagnosticLog(const char*,int line,const char* function,const char* format,...) {
    fprintf(stderr,"[%s:%d] ",function?function:"?",line);
    va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);fputc('\n',stderr);fflush(stderr);
}
static bool redirectLog(unsigned char* entry) {
    unsigned char jump[12]={0x48,0xb8};void* destination=reinterpret_cast<void*>(&diagnosticLog);
    memcpy(jump+2,&destination,8);jump[10]=0xff;jump[11]=0xe0;
    DWORD old=0;if(!VirtualProtect(entry,12,PAGE_EXECUTE_READWRITE,&old))return false;
    memcpy(entry,jump,12);DWORD ignored=0;VirtualProtect(entry,12,old,&ignored);FlushInstructionCache(GetCurrentProcess(),entry,12);return true;
}
static void* nativeBackend=nullptr;
static void** nativeVtable=nullptr;
static std::vector<void*> vaResources;
static std::vector<void*> residentResources;
static void track(void* resource){if(resource&&std::find(residentResources.begin(),residentResources.end(),resource)==residentResources.end())residentResources.push_back(resource);}
static int trackedBuffer(void* backend,const unsigned* desc,void** out,const char* name,int flags){
    int status=reinterpret_cast<int(*)(void*,const unsigned*,void**,const char*,int)>(nativeVtable[0x70/8])(backend,desc,out,name,flags);
    if(!status&&desc[6]==1)track(*out);return status;
}
static int trackedTexture(void* backend,const unsigned* desc,void** out,const char* name,int flags){
    int status=reinterpret_cast<int(*)(void*,const unsigned*,void**,const char*,int)>(nativeVtable[0x78/8])(backend,desc,out,name,flags);
    if(!status)track(*out);return status;
}
static int trackedRelease(void* backend,void* resource){
    int status=reinterpret_cast<int(*)(void*,void*)>(nativeVtable[0xa0/8])(backend,resource);
    if(!status){residentResources.erase(std::remove(residentResources.begin(),residentResources.end(),resource),residentResources.end());vaResources.erase(std::remove(vaResources.begin(),vaResources.end(),resource),vaResources.end());}
    return status;
}
static unsigned registeredDispatches=0;
alignas(16) static unsigned char pendingKernel[0x140]{};
static int appendHandle(unsigned char* state,void* handle,bool write) {
    auto count=reinterpret_cast<unsigned*>(state+(write?0x34:0x30));
    auto total=*reinterpret_cast<unsigned*>(state+0x30)+*reinterpret_cast<unsigned*>(state+0x34);
    auto list=reinterpret_cast<void**>(state+(write?0xc0:0x40));
    for(unsigned i=0;i<*count;++i)if(list[i]==handle)return 0;
    if(*count>=(write?16u:16u)||total>=24)return -1;
    list[(*count)++]=handle;return 0;
}
static int pureBufferVA(void* backend,void* resource,uint64_t* output) {
    void* device=*reinterpret_cast<void**>(static_cast<unsigned char*>(backend)+0x460);
    void* handle=nullptr;
    int status=reinterpret_cast<int(*)(void*,void*,void**)>(imageBase+0x1ad0)(device,resource,&handle);
    if(!status)status=reinterpret_cast<int(*)(void*,void*,uint64_t*)>(imageBase+0x38a0)(device,handle,output);
    if(!status&&resource&&std::find(vaResources.begin(),vaResources.end(),resource)==vaResources.end())vaResources.push_back(resource);
    event("adapter_buffer_va",status);return status;
}
static int registeredBegin(void* backend,void* kernel) {
    int status=reinterpret_cast<int(*)(void*,void*)>(nativeVtable[0xd8/8])(backend,kernel);
    if(status)return status;
    for(void* resource:residentResources) {
        status=reinterpret_cast<int(*)(void*,void*)>(nativeVtable[0xc8/8])(backend,resource);if(status)return status;
        status=reinterpret_cast<int(*)(void*,void*)>(nativeVtable[0xd0/8])(backend,resource);if(status)return status;
    }
    auto state=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(backend)+0x140);
    for(unsigned write=0;write<2;++write){auto count=*reinterpret_cast<unsigned*>(pendingKernel+(write?0x34:0x30));auto list=reinterpret_cast<void**>(pendingKernel+(write?0xc0:0x40));
        for(unsigned i=0;i<count;++i)if(appendHandle(state,list[i],write!=0))return -1;}
    ++registeredDispatches;
    return 0;
}
static int clearBuffer(void*,ID3D11DeviceContext* context,ID3D11Resource* resource,unsigned value) {
    ID3D11Buffer* buffer=nullptr;HRESULT hr=resource->QueryInterface(__uuidof(ID3D11Buffer),reinterpret_cast<void**>(&buffer));
    if(FAILED(hr))return -5;
    D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
    if(!(desc.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)||desc.ByteWidth%4){buffer->Release();return -5;}
    ID3D11Device* device=nullptr;context->GetDevice(&device);
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32_TYPELESS;ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements=desc.ByteWidth/4;ud.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
    ID3D11UnorderedAccessView* view=nullptr;hr=device->CreateUnorderedAccessView(buffer,&ud,&view);
    if(SUCCEEDED(hr)){unsigned values[4]={value,value,value,value};context->ClearUnorderedAccessViewUint(view,values);view->Release();}
    device->Release();buffer->Release();event("adapter_clear_buffer",hr);return SUCCEEDED(hr)?0:-1;
}
static int exception(EXCEPTION_POINTERS* p) {event("exception_rva",reinterpret_cast<unsigned char*>(p->ExceptionRecord->ExceptionAddress)-imageBase);event("access_address",p->ExceptionRecord->ExceptionInformation[1]);return EXCEPTION_EXECUTE_HANDLER;}
struct Arguments {
    virtual void prepare() {}
    virtual unsigned id() {return 0xD24;}
    virtual void* data() {return &input;}
    virtual unsigned size() {return 24;}
    uint64_t input=0,output=0;
    unsigned count=256,salt=0;
};
static bool dryDispatch=false;
static unsigned dispatchBudget=0;
static unsigned dispatchCount=0;
static int traceDispatch(void* backend,Arguments* args,void* context,unsigned x,unsigned y,unsigned z) {
    auto state=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(backend)+0x140);
    auto readCount=*reinterpret_cast<unsigned*>(state+0x30),writeCount=*reinterpret_cast<unsigned*>(state+0x34);
    const char* name=reinterpret_cast<const char*>(state+0x10);
    if(*reinterpret_cast<size_t*>(state+0x28)>=16)name=*reinterpret_cast<const char**>(state+0x10);
    fprintf(stderr,"D24_DISPATCH %u %s grid=%u,%u,%u read=%u write=%u bytes=%u dry=%d\n",dispatchCount,name,x,y,z,readCount,writeCount,args->size(),dryDispatch);
    if(readCount>16||writeCount>16||readCount+writeCount>24||state==pendingKernel)return -1;
    ++dispatchCount;
    if(dryDispatch||(dispatchBudget&&dispatchCount>dispatchBudget))return 0;
    return reinterpret_cast<int(*)(void*,Arguments*,void*,unsigned,unsigned,unsigned)>(nativeVtable[0x140/8])(backend,args,context,x,y,z);
}
static int kernelProbe(unsigned char* base,void* backend,void** vt,ID3D11Device* device,ID3D11DeviceContext* context,const wchar_t* cubinPath) {
    std::ifstream file(cubinPath,std::ios::binary);
    std::vector<char> cubin((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    if(cubin.empty())return 10;
    void* feature=nullptr;
    auto createFeature=reinterpret_cast<int(*)(void*,void**)>(vt[0x18/8]);
    int featureStatus=createFeature(backend,&feature);event("feature_create",featureStatus);if(featureStatus||!feature)return 25;
    auto beginEval=reinterpret_cast<int(*)(void*,void*,void*,void*)>(vt[0xe0/8]);
    featureStatus=beginEval(backend,context,nullptr,feature);event("begin_evaluate",featureStatus);if(featureStatus)return 26;
    void* kernel=nullptr;
    auto create=reinterpret_cast<int(*)(void*,void*,unsigned,const char*,unsigned,void**,unsigned,unsigned,unsigned,unsigned)>(vt[0x60/8]);
    int status=create(backend,cubin.data(),static_cast<unsigned>(cubin.size()),"d24_affine",0xD24,&kernel,64,1,1,0);
    event("kernel_create",status);if(status||!kernel)return 11;
    auto destroy=reinterpret_cast<int(*)(void*,void*)>(vt[0x98/8]);
    auto begin=reinterpret_cast<int(*)(void*,void*)>(vt[0xd8/8]);
    auto alloc=reinterpret_cast<int(*)(void*,const unsigned*,void**,const char*,int)>(vt[0x70/8]);
    auto release=reinterpret_cast<int(*)(void*,void*)>(vt[0xa0/8]);
    unsigned desc[9]={1024,1,0xffffffffu,0,1,2,1,0,0};
    void* input=nullptr;void* output=nullptr;
    status=alloc(backend,desc,&input,"D24Input",-1);event("input_alloc",status);
    if(status){destroy(backend,kernel);return 12;}
    status=alloc(backend,desc,&output,"D24Output",-1);event("output_alloc",status);
    if(status){release(backend,input);destroy(backend,kernel);return 13;}
    ID3D11Buffer* staging=nullptr;D3D11_BUFFER_DESC stagingDesc{};
    stagingDesc.ByteWidth=1024;stagingDesc.Usage=D3D11_USAGE_STAGING;stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    HRESULT hr=device->CreateBuffer(&stagingDesc,nullptr,&staging);event("staging_create",hr);
    if(FAILED(hr))return 14;
    ID3D11Query* query=nullptr;D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT,0};
    hr=device->CreateQuery(&qd,&query);event("query_create",hr);if(FAILED(hr))return 15;
    auto getva=reinterpret_cast<int(*)(void*,void*,uint64_t*)>(vt[0xb8/8]);
    auto markwrite=reinterpret_cast<int(*)(void*,void*)>(vt[0xd0/8]);
    auto dispatch=reinterpret_cast<int(*)(void*,Arguments*,void*,unsigned,unsigned,unsigned)>(vt[0x140/8]);
    Arguments args;unsigned src[256],sentinel[256];int result=0;
    for(unsigned frame=0;frame<60;++frame) {
        if(frame==0)event("update_begin",1);
        for(unsigned i=0;i<256;++i){src[i]=i+frame*17;sentinel[i]=0xdeadbeef;}
        context->UpdateSubresource(static_cast<ID3D11Resource*>(input),0,nullptr,src,0,0);
        context->UpdateSubresource(static_cast<ID3D11Resource*>(output),0,nullptr,sentinel,0,0);
        if(frame==0)event("begin_begin",1);
        status=begin(backend,kernel); if(status){event("begin_fail",status);result=16;break;}
        if(frame==0)event("input_va_begin",1);
        status=getva(backend,input,&args.input);if(status){event("input_va_fail",status);result=17;break;}
        status=getva(backend,output,&args.output);if(status){event("output_va_fail",status);result=18;break;}
        status=markwrite(backend,output);if(status){event("mark_write_fail",status);result=19;break;}
        if(!args.input||!args.output){event("zero_va",1);result=20;break;}
        args.salt=frame+7;
        status=dispatch(backend,&args,context,4,1,1);if(status){event("dispatch_fail",status);result=21;break;}
        context->CopyResource(staging,static_cast<ID3D11Resource*>(output));
        context->End(query);context->Flush();ULONGLONG deadline=GetTickCount64()+5000;
        BOOL done=FALSE;do {hr=context->GetData(query,&done,sizeof(done),0);if(hr!=S_FALSE)break;Sleep(1);}while(GetTickCount64()<deadline);
        if(hr!=S_OK||!done){event("gpu_completion_fail",hr);return 22;} // Process owns resources on uncertain completion.
        D3D11_MAPPED_SUBRESOURCE mapped{};hr=context->Map(staging,0,D3D11_MAP_READ,0,&mapped);
        if(FAILED(hr)){event("map_fail",hr);result=23;break;}
        unsigned mismatches=0;auto values=static_cast<unsigned*>(mapped.pData);
        for(unsigned i=0;i<256;++i)if(values[i]!=src[i]*3+args.salt)++mismatches;
        context->Unmap(staging,0);event("frame_mismatches",mismatches);
        if(mismatches){result=24;break;}
    }
    query->Release();staging->Release();
    event("output_release",release(backend,output));event("input_release",release(backend,input));
    event("kernel_destroy",destroy(backend,kernel));
    event("feature_destroy",reinterpret_cast<int(*)(void*,void*,bool)>(vt[0x20/8])(backend,feature,true));
    (void)base;return result;
}
static int nrEvaluate(unsigned char* base,void* common,void* handle,ProbeParameters& params,ID3D11Device* device,ID3D11DeviceContext* context) {
    constexpr unsigned w=256,h=256;
    ID3D11Texture2D* textures[4]{};
    DXGI_FORMAT formats[4]={DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32G32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT};
    for(unsigned i=0;i<4;++i){D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=formats[i];td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        HRESULT hr=device->CreateTexture2D(&td,nullptr,&textures[i]);event("nr_texture_create",hr);if(FAILED(hr))return 40;}
    const char* names[4]={"DLSSNR.Color","DLSSNR.Depth","DLSSNR.MVec","DLSSNR.Output"};
    for(unsigned i=0;i<4;++i){params.Set(names[i],static_cast<ID3D11Resource*>(textures[i]));track(textures[i]);}
    for(const char* role:{"Color","Depth","MVec","Output"}) {
        std::string prefix="DLSSNR.";prefix+=role;
        params.Set((prefix+"SubrectBaseX").c_str(),0u);params.Set((prefix+"SubrectBaseY").c_str(),0u);
        params.Set((prefix+"SubrectWidth").c_str(),w);params.Set((prefix+"SubrectHeight").c_str(),h);
    }
    params.Set("DLSSNR.MVecScaleX",1.0f);params.Set("DLSSNR.MVecScaleY",1.0f);
    params.Set("DLSSNR.DepthInverted",0u);params.Set("DLSSNR.UseAutoMask",0u);
    params.Set("DLSSNR.LocalStructureStrength",0.5f);params.Set("DLSSNR.LocalToneStrength",0.5f);params.Set("DLSSNR.SkinStructureStrength",0.5f);
    std::vector<float> color(w*h*4),depth(w*h,0.5f),motion(w*h*2,0),sentinel(w*h*4,-1000);
    ID3D11Texture2D* staging=nullptr;D3D11_TEXTURE2D_DESC sd{};textures[3]->GetDesc(&sd);sd.BindFlags=0;sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    HRESULT hr=device->CreateTexture2D(&sd,nullptr,&staging);if(FAILED(hr))return 41;
    ID3D11Query* query=nullptr;D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT,0};hr=device->CreateQuery(&qd,&query);if(FAILED(hr))return 42;
    for(unsigned frame=0;frame<60;++frame) {
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){unsigned i=(y*w+x)*4;float a=(frame/10)%2?0.15f:0.0f;color[i]=0.1f+0.6f*x/w+a;color[i+1]=0.1f+0.6f*y/h;color[i+2]=((x/8+y/8)%2)?0.6f:0.2f;color[i+3]=1;}
        context->UpdateSubresource(textures[0],0,nullptr,color.data(),w*16,0);
        context->UpdateSubresource(textures[1],0,nullptr,depth.data(),w*4,0);
        context->UpdateSubresource(textures[2],0,nullptr,motion.data(),w*8,0);
        context->UpdateSubresource(textures[3],0,nullptr,sentinel.data(),w*16,0);
        params.Set("DLSSNR.Reset",frame%10==0?1u:0u);
        memset(pendingKernel,0,sizeof(pendingKernel));
        auto feature=*reinterpret_cast<void**>(static_cast<unsigned char*>(common)+0x10);
        auto beginEval=reinterpret_cast<int(*)(void*,void*,void*,void*)>(nativeVtable[0xe0/8]);
        int prepared=beginEval(nativeBackend,context,nullptr,feature);if(prepared)return 47;
        *reinterpret_cast<void**>(static_cast<unsigned char*>(nativeBackend)+0x140)=pendingKernel;
        event("nr_evaluate_begin",frame);
        int status=reinterpret_cast<int(*)(void*,void*,void*,void*,void*)>(base+0x18620)(common,context,handle,&params,nullptr);
        event("nr_evaluate",static_cast<unsigned>(status));if(status!=1)return 43;
        if(dryDispatch){event("dry_dispatch_count",dispatchCount);event("nr_output_not_tested",1);return 48;}
        context->CopyResource(staging,textures[3]);context->End(query);context->Flush();
        ULONGLONG deadline=GetTickCount64()+10000;BOOL done=FALSE;
        do{hr=context->GetData(query,&done,sizeof(done),0);if(hr!=S_FALSE)break;Sleep(1);}while(GetTickCount64()<deadline);
        event("nr_gpu_completion",hr);if(hr!=S_OK||!done)return 44;
        if(dispatchBudget){event("prefix_completed",dispatchBudget);event("nr_output_not_tested",1);return 49;}
        D3D11_MAPPED_SUBRESOURCE mapped{};hr=context->Map(staging,0,D3D11_MAP_READ,0,&mapped);if(FAILED(hr))return 45;
        unsigned nonfinite=0,unchanged=0;double sum=0,difference=0;
        std::ofstream raw("nr-frame-"+std::to_string(frame)+".f32",std::ios::binary);
        for(unsigned y=0;y<h;++y){auto row=reinterpret_cast<float*>(static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch);raw.write(reinterpret_cast<const char*>(row),w*16);
            for(unsigned x=0;x<w;++x)for(unsigned ch=0;ch<3;++ch){float v=row[x*4+ch];if(!std::isfinite(v))++nonfinite;if(v==-1000)++unchanged;sum+=v;difference+=std::abs(v-color[(y*w+x)*4+ch]);}}
        context->Unmap(staging,0);
        printf("{\"event\":\"nr_frame\",\"frame\":%u,\"nonfinite\":%u,\"sentinel\":%u,\"mean\":%.9g,\"mean_abs_input_difference\":%.9g,\"registered_dispatches\":%u}\n",frame,nonfinite,unchanged,sum/(w*h*3),difference/(w*h*3),registeredDispatches);fflush(stdout);
        if(nonfinite||unchanged)return 46;
    }
    query->Release();staging->Release();for(auto* t:textures)t->Release();return 0;
}
static int nrCreateProbe(unsigned char* base,void* backend,void** vt,ID3D11Device* device,ID3D11DeviceContext* context,const wchar_t* mode) {
    if(!redirectLog(base+0xe660)||!redirectLog(base+0xe890))return 34;
    event("process_local_log_redirect",1);
    bool experiment=wcscmp(mode,L"--nr-create")!=0;
    dryDispatch=wcscmp(mode,L"--nr-create-dry")==0;
    if(wcsncmp(mode,L"--nr-create-prefix",18)==0)dispatchBudget=static_cast<unsigned>(_wtoi(mode+18));
    bool evaluate=wcscmp(mode,L"--nr-create-evaluate")==0||dryDispatch||dispatchBudget;
    bool adapter=wcscmp(mode,L"--nr-create-adapter")==0||evaluate;
    void* adaptedVtable[0x300/8];
    if(adapter){
        memcpy(adaptedVtable,vt,sizeof(adaptedVtable));nativeBackend=backend;nativeVtable=vt;
        adaptedVtable[0x70/8]=reinterpret_cast<void*>(&trackedBuffer);
        adaptedVtable[0x78/8]=reinterpret_cast<void*>(&trackedTexture);
        adaptedVtable[0xa0/8]=reinterpret_cast<void*>(&trackedRelease);
        adaptedVtable[0xb8/8]=reinterpret_cast<void*>(&pureBufferVA);
        adaptedVtable[0xd8/8]=reinterpret_cast<void*>(&registeredBegin);
        adaptedVtable[0x1b0/8]=reinterpret_cast<void*>(&clearBuffer);
        adaptedVtable[0x140/8]=reinterpret_cast<void*>(&traceDispatch);
        *reinterpret_cast<void***>(backend)=adaptedVtable;
        event("process_local_resource_adapter",1);
    }
    alignas(16) unsigned char common[0xa8]{};
    reinterpret_cast<void*(*)(void*)>(base+0x16860)(common);
    int init=reinterpret_cast<int(*)(void*,void*,const wchar_t*,void*,int)>(base+0x19e70)(common,device,L".",backend,0);
    event("nr_common_init",init);if(init!=1)return 30;
    ProbeParameters params;
    params.Set("DLSSNR.Width",256u);params.Set("DLSSNR.Height",256u);
    params.Set("Width",256u);params.Set("Height",256u);
    params.Set("DLSSNR.Enabled",1u);params.Set("DLSSNR.Hint.Render.Preset",0u);
    params.Set("DLSSNR.Intensity",1.0f);params.Set("DLSSNR.Style",0u);
    params.Set("DLSSNR.ScalingRatio",1.0f);
    // A process-local, byte-guarded experiment. No image is written to disk.
    if(experiment) {
        const unsigned char expected[]={0x4c,0x8d,0x0d,0x8d,0xf2,0x08,0x00};
        if(memcmp(base+0x20f2c,expected,sizeof(expected))){event("gate_bytes_mismatch",1);return 31;}
        DWORD old=0; if(!VirtualProtect(base+0x20f2c,7,PAGE_EXECUTE_READWRITE,&old))return 32;
        const unsigned char jump[]={0xe9,0x24,0,0,0,0x90,0x90};memcpy(base+0x20f2c,jump,7);
        DWORD ignored=0;VirtualProtect(base+0x20f2c,7,old,&ignored);FlushInstructionCache(GetCurrentProcess(),base+0x20f2c,7);
        event("process_local_nr_gate_experiment",1);
    }
    void* handle=nullptr;event("nr_create_begin",1);
    int result=reinterpret_cast<int(*)(void*,void*,void*,void**)>(base+0x17e20)(common,context,&params,&handle);
    event("nr_create",static_cast<unsigned>(result));event("nr_handle_nonnull",handle!=nullptr);
    if(evaluate&&result==1){int tested=nrEvaluate(base,common,handle,params,device,context);event("nr_evaluate_probe_result",tested);result=tested==0?1:0;}
    event("nr_device_removed",device->GetDeviceRemovedReason());
    if(evaluate&&result==1) {
        int released=reinterpret_cast<int(*)(void*,void*)>(base+0x1aeb0)(common,handle);event("nr_release",static_cast<unsigned>(released));
        int stopped=reinterpret_cast<int(*)(void*)>(vt[2])(backend);event("nr_backend_shutdown",stopped);
        *reinterpret_cast<void***>(backend)=vt;
        context->ClearState();context->Flush();context->Release();device->Release();
        event("nr_dll_unload",FreeLibrary(reinterpret_cast<HMODULE>(base)));
        event("nr_lifecycle_complete",released==1&&stopped==0);
        fflush(stdout);ExitProcess(released==1&&stopped==0?0:35);
    }
    (void)vt;
    // Creation-only research child: exit immediately so failed/unknown ownership is not reused.
    fflush(stdout);ExitProcess(result==1?0:33);
}
static int run(const wchar_t* path,const wchar_t* cubin,const wchar_t* mode) {
    auto module=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module){event("load_error",GetLastError());return 2;}
    auto base=reinterpret_cast<unsigned char*>(module);
    imageBase=base;
    // Constructor-installed vtable and API-kind body pin the internal ABI as well as the runner's SHA guard.
    void* backend=base+0x1153550;
    auto vt=*reinterpret_cast<void***>(backend);
    if(vt!=reinterpret_cast<void**>(base+0xb55f8) || vt[1]!=base+0x59b00 || vt[10]!=base+0x59910){event("abi_mismatch",1);return 3;}
    event("backend_vtable_verified",1);
    ID3D11Device* device=nullptr; ID3D11DeviceContext* context=nullptr;
    D3D_FEATURE_LEVEL fl{};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&fl,&context);
    event("d3d11_create",hr); if(FAILED(hr))return 4;
    event("feature_level",fl);
    auto init=reinterpret_cast<int(*)(void*,void*)>(vt[1]);
    event("backend_init_begin",1); int result=init(backend,device); event("backend_init",result);
    int kind=-1; auto getkind=reinterpret_cast<int(*)(void*,int*)>(vt[10]);
    event("api_kind_result",getkind(backend,&kind));event("api_kind",kind);
    if(mode&&result==0)return nrCreateProbe(base,backend,vt,device,context,mode);
    if(result==0&&cubin)result=kernelProbe(base,backend,vt,device,context,cubin);
    event("device_removed",device->GetDeviceRemovedReason());
    event("backend_shutdown_begin",1);
    auto shutdown=reinterpret_cast<int(*)(void*)>(vt[2]); event("backend_shutdown",shutdown(backend));
    context->Release();device->Release();
    // Static backend destructor is owned by DLL unload, after Shutdown.
    FreeLibrary(module); event("complete",1);return result==0?0:5;
}
int wmain(int argc,wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc!=2&&argc!=3)return 1;
    bool nr=argc==3&&wcsncmp(argv[2],L"--nr-create",11)==0;
    __try { return run(argv[1],argc==3&&!nr?argv[2]:nullptr,nr?argv[2]:nullptr); }
    __except(exception(GetExceptionInformation())) { event("seh_exception",GetExceptionCode()); return 90; }
}
