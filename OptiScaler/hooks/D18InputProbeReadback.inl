// Included inside D18InputProbe. All GPU work uses the selected immediate context.
namespace Numeric {
using Microsoft::WRL::ComPtr;
#include "D18ProbeGatherBytecode.h"
inline constexpr UINT gridBytes=64*36*3*16;
inline constexpr unsigned long long fileLimit=32ULL*1024*1024;
inline std::atomic<bool> enabled{false};
inline std::atomic<ID3D11DeviceChild*> targets[8]{};
inline FILE* file=nullptr;
inline ComPtr<ID3D11ComputeShader> gather;
inline unsigned long long fileBytes=0,issued=0,completed=0,busyDrops=0,invalidDrops=0,failures=0,lastFrame=~0ULL;
inline unsigned activeSession=0,steps[3]{};
inline bool stopped=false;
struct Internal {bool prior;Internal():prior(internalWork){internalWork=true;}~Internal(){internalWork=prior;}};
struct Job {
    ComPtr<ID3D11Buffer> gpu,read[4];ComPtr<ID3D11UnorderedAccessView> uav;ComPtr<ID3D11Query> query;
    UINT sizes[4]{gridBytes,0,0,0},first[3]{},counts[3]{};
    unsigned long long cbIds[3]{},resourceIds[3]{},frame=0,id=0,tick=0;
    unsigned session=0,group=0,step=0;UINT width=0,height=0;
    bool pending=false,timeoutReported=false;
};
inline Job jobs[3];
inline void Register(ID3D11DeviceChild* shader,unsigned long long hash){
    if((!enabled.load()&&!DlssNr::WildlandsSr::available.load())||hash!=0x34a304d07b55d993ULL)return;
    for(auto& t:targets)if(t.load()==shader)return;
    for(auto& t:targets)if(!t.load()){shader->AddRef();t.store(shader);return;}
}
inline bool IsTarget(ID3D11DeviceChild* shader){if(!shader)return false;for(auto& t:targets)if(t.load()==shader)return true;return false;}
inline void Enable(const std::filesystem::path& root){enabled.store(DlssNr::BuildProfile::Diagnostic && GetFileAttributesW((root/L"D18InputProbe.numeric.enabled").c_str())!=INVALID_FILE_ATTRIBUTES);}
inline void Initialize(ID3D11Device* device,const std::filesystem::path& root){
    if(!enabled.load()||file||stopped)return;
    Internal scope;
    file=_wfsopen((root/L"D18InputProbe.values.bin").c_str(),L"wb",_SH_DENYNO);
    const HRESULT hr=file?device->CreateComputeShader(numericBytecode,sizeof(numericBytecode),nullptr,&gather):E_FAIL;
    if(FAILED(hr)){stopped=true;++failures;}
    LogInput("{\"event\":\"numeric_begin\",\"hresult\":%ld,\"grid\":[64,36],\"record_float4s\":3,\"max_pending\":3,\"max_per_session\":24,\"file_limit\":%llu,\"target_hash\":\"34a304d07b55d993\"}\n",hr,fileLimit);FlushInput();
}
inline HRESULT EnsureBuffer(ID3D11Device* d,ComPtr<ID3D11Buffer>& b,UINT bytes){
    if(b){D3D11_BUFFER_DESC old{};b->GetDesc(&old);if(old.ByteWidth==bytes)return S_OK;b.Reset();}
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;return d->CreateBuffer(&desc,nullptr,&b);
}
// Preserve every modified CS binding, including 11.1 constant-buffer ranges and predication.
struct SavedCS {
    ID3D11DeviceContext* c;ComPtr<ID3D11DeviceContext1> c1;
    ID3D11ComputeShader* shader=nullptr;ID3D11ClassInstance* classes[256]{};UINT classCount=256;
    ID3D11ShaderResourceView* srvs[3]{};ID3D11Buffer* cbs[2]{};UINT first[2]{},counts[2]{};
    ID3D11SamplerState* sampler=nullptr;ID3D11UnorderedAccessView* uav=nullptr;
    ID3D11Predicate* predicate=nullptr;BOOL predicateValue=FALSE;
    SavedCS(ID3D11DeviceContext* ctx):c(ctx){
        ctx->QueryInterface(IID_PPV_ARGS(&c1));c->CSGetShader(&shader,classes,&classCount);c->CSGetShaderResources(0,3,srvs);
        if(c1)c1->CSGetConstantBuffers1(1,2,cbs,first,counts);else c->CSGetConstantBuffers(1,2,cbs);
        c->CSGetSamplers(0,1,&sampler);c->CSGetUnorderedAccessViews(0,1,&uav);c->GetPredication(&predicate,&predicateValue);
    }
    ~SavedCS(){
        ID3D11UnorderedAccessView* none=nullptr;c->CSSetUnorderedAccessViews(0,1,&none,nullptr);
        c->CSSetShaderResources(0,3,srvs);
        if(c1)c1->CSSetConstantBuffers1(1,2,cbs,first,counts);else c->CSSetConstantBuffers(1,2,cbs);
        c->CSSetSamplers(0,1,&sampler);UINT preserve=UINT(-1);c->CSSetUnorderedAccessViews(0,1,&uav,&preserve);
        c->CSSetShader(shader,classes,classCount);c->SetPredication(predicate,predicateValue);
        if(shader)shader->Release();for(UINT i=0;i<classCount&&i<256;++i)if(classes[i])classes[i]->Release();
        for(auto p:srvs)if(p)p->Release();for(auto p:cbs)if(p)p->Release();if(sampler)sampler->Release();if(uav)uav->Release();if(predicate)predicate->Release();
    }
};
inline void OnDraw(ID3D11DeviceContext* ctx){
    if(internalWork||!enabled.load()||!frameCounting.load()||boundContext!=ctx||!IsTarget(boundShader))return;
    std::unique_lock lock(guard,std::try_to_lock);if(!lock.owns_lock())return;
    if(stopped||!file||!gather||!armedUntil||GetTickCount64()>=armedUntil||lastFrame==frameNumber||ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
    ComPtr<ID3D11Device> device;ctx->GetDevice(&device);if(device.Get()!=observedDevice)return;
    ComPtr<ID3D11PixelShader> ps;ctx->PSGetShader(&ps,nullptr,nullptr);if(!IsTarget(ps.Get()))return;
    if(activeSession!=sessions){activeSession=sessions;steps[0]=steps[1]=steps[2]=0;}
    const auto now=GetTickCount64();const auto group=static_cast<unsigned>((now-(armedUntil-15000))/5000);if(group>=3||steps[group]>=8)return;
    Job* target=nullptr;for(auto& j:jobs)if(!j.pending){target=&j;break;}if(!target){++busyDrops;return;}
    Internal scope;Job& j=*target;
    ComPtr<ID3D11ShaderResourceView> views[3];ComPtr<ID3D11Resource> resources[3];const UINT slots[]={0,2,5};
    for(unsigned i=0;i<3;++i){
        ctx->PSGetShaderResources(slots[i],1,&views[i]);if(!views[i]){++invalidDrops;return;}
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};views[i]->GetDesc(&vd);views[i]->GetResource(&resources[i]);ComPtr<ID3D11Texture2D> t;
        if(vd.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||FAILED(resources[i].As(&t))){++invalidDrops;return;}
        D3D11_TEXTURE2D_DESC td{};t->GetDesc(&td);
        const bool format=i==0?vd.Format==DXGI_FORMAT_R10G10B10A2_UNORM:i==1?(vd.Format==DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS||vd.Format==DXGI_FORMAT_R32_FLOAT):vd.Format==DXGI_FORMAT_R16G16_FLOAT;
        if(!format||td.SampleDesc.Count!=1||td.MipLevels!=1||td.ArraySize!=1||vd.Texture2D.MostDetailedMip!=0||td.Width==0||td.Height==0){++invalidDrops;return;}
        if(i==0){j.width=td.Width;j.height=td.Height;}else if(j.width!=td.Width||j.height!=td.Height){++invalidDrops;return;}
        j.resourceIds[i]=ResourceId(resources[i].Get());
    }
    ComPtr<ID3D11Buffer> inputs[3];ComPtr<ID3D11DeviceContext1> c1;ctx->QueryInterface(IID_PPV_ARGS(&c1));const UINT cbSlots[]={1,2,5};const UINT required[]={161,43,19};
    for(unsigned i=0;i<3;++i){
        if(c1)c1->PSGetConstantBuffers1(cbSlots[i],1,&inputs[i],&j.first[i],&j.counts[i]);else {ctx->PSGetConstantBuffers(cbSlots[i],1,&inputs[i]);j.first[i]=0;j.counts[i]=4096;}
        if(!inputs[i]){++invalidDrops;return;}D3D11_BUFFER_DESC bd{};inputs[i]->GetDesc(&bd);
        if(bd.ByteWidth>65536||bd.ByteWidth<(j.first[i]+required[i])*16ULL||j.counts[i]<required[i]){++invalidDrops;return;}
        j.sizes[i+1]=bd.ByteWidth;j.cbIds[i]=reinterpret_cast<unsigned long long>(inputs[i].Get());
    }
    ComPtr<ID3D11SamplerState> sampler;ctx->PSGetSamplers(10,1,&sampler);if(!sampler){++invalidDrops;return;}
    HRESULT hr=S_OK;
    if(!j.gpu){D3D11_BUFFER_DESC bd{};bd.ByteWidth=gridBytes;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=16;
        hr=device->CreateBuffer(&bd,nullptr,&j.gpu);if(SUCCEEDED(hr)){D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};uv.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=gridBytes/16;hr=device->CreateUnorderedAccessView(j.gpu.Get(),&uv,&j.uav);}}
    if(SUCCEEDED(hr)&&!j.query){D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};hr=device->CreateQuery(&q,&j.query);}
    for(unsigned i=0;i<4&&SUCCEEDED(hr);++i)hr=EnsureBuffer(device.Get(),j.read[i],j.sizes[i]);
    if(FAILED(hr)||!j.uav){++failures;stopped=true;LogInput("{\"event\":\"numeric_failure\",\"where\":\"allocation\",\"hresult\":%ld}\n",hr);return;}
    unsigned long long needed=0;for(auto size:j.sizes)needed+=size;if(fileBytes+needed>fileLimit){stopped=true;return;}
    {
        SavedCS saved(ctx);ctx->SetPredication(nullptr,FALSE);
        ID3D11ShaderResourceView* rawViews[]={views[0].Get(),views[1].Get(),views[2].Get()};ctx->CSSetShaderResources(0,3,rawViews);
        ID3D11Buffer* rawCBs[]={inputs[0].Get(),inputs[2].Get()};UINT first[]={j.first[0],j.first[2]},counts[]={j.counts[0],j.counts[2]};
        if(c1)c1->CSSetConstantBuffers1(1,2,rawCBs,first,counts);else ctx->CSSetConstantBuffers(1,2,rawCBs);
        ID3D11SamplerState* rawSampler=sampler.Get();ctx->CSSetSamplers(0,1,&rawSampler);
        ID3D11UnorderedAccessView* rawUav=j.uav.Get();ctx->CSSetUnorderedAccessViews(0,1,&rawUav,nullptr);ctx->CSSetShader(gather.Get(),nullptr,0);ctx->Dispatch(8,9,1);
        ID3D11UnorderedAccessView* none=nullptr;ctx->CSSetUnorderedAccessViews(0,1,&none,nullptr);
        ctx->CopyResource(j.read[0].Get(),j.gpu.Get());for(unsigned i=0;i<3;++i)ctx->CopyResource(j.read[i+1].Get(),inputs[i].Get());ctx->End(j.query.Get());
    }
    j.pending=true;j.timeoutReported=false;j.frame=frameNumber;j.tick=now;j.id=++issued;j.session=sessions;j.group=group;j.step=steps[group]++;lastFrame=frameNumber;
    LogInput("{\"event\":\"numeric_issued\",\"id\":%llu,\"session\":%u,\"group\":%u,\"step\":%u,\"frame\":%llu}\n",j.id,j.session,j.group,j.step,j.frame);
}
inline void Poll(ID3D11DeviceContext* ctx){
    if(!enabled.load()||!file)return;Internal scope;
    for(auto& j:jobs){if(!j.pending)continue;
        BOOL ready=FALSE;const HRESULT hr=ctx->GetData(j.query.Get(),&ready,sizeof(ready),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(hr==S_FALSE||(!ready&&SUCCEEDED(hr))){if(!j.timeoutReported&&GetTickCount64()-j.tick>5000){j.timeoutReported=true;stopped=true;LogInput("{\"event\":\"numeric_pending_timeout\",\"id\":%llu}\n",j.id);}continue;}
        if(FAILED(hr)){++failures;stopped=true;j.pending=false;LogInput("{\"event\":\"numeric_failure\",\"where\":\"query\",\"hresult\":%ld,\"id\":%llu}\n",hr,j.id);continue;}
        D3D11_MAPPED_SUBRESOURCE maps[4]{};unsigned mapped=0;HRESULT mapStatus=S_OK;
        for(;mapped<4;++mapped){mapStatus=ctx->Map(j.read[mapped].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&maps[mapped]);if(FAILED(mapStatus))break;}
        if(mapped!=4){for(unsigned i=0;i<mapped;++i)ctx->Unmap(j.read[i].Get(),0);if(mapStatus!=DXGI_ERROR_WAS_STILL_DRAWING){++failures;stopped=true;j.pending=false;}continue;}
        const auto offset=fileBytes;bool complete=true;unsigned long long need=0;for(auto size:j.sizes)need+=size;
        if(fileBytes+need>fileLimit){complete=false;stopped=true;}else for(unsigned i=0;i<4;++i){const auto n=fwrite(maps[i].pData,1,j.sizes[i],file);fileBytes+=n;if(n!=j.sizes[i]){complete=false;stopped=true;break;}}
        for(unsigned i=0;i<4;++i)ctx->Unmap(j.read[i].Get(),0);
        LogInput("{\"event\":\"numeric_snapshot\",\"id\":%llu,\"session\":%u,\"group\":%u,\"step\":%u,\"frame\":%llu,\"tick\":%llu,\"offset\":%llu,\"complete\":%s,\"width\":%u,\"height\":%u,\"resources\":[%llu,%llu,%llu],\"sizes\":[%u,%u,%u,%u],\"cb_first\":[%u,%u,%u],\"cb_counts\":[%u,%u,%u]}\n",j.id,j.session,j.group,j.step,j.frame,j.tick,offset,complete?"true":"false",j.width,j.height,j.resourceIds[0],j.resourceIds[1],j.resourceIds[2],j.sizes[0],j.sizes[1],j.sizes[2],j.sizes[3],j.first[0],j.first[1],j.first[2],j.counts[0],j.counts[1],j.counts[2]);
        if(complete)++completed;j.pending=false;
    }
    fflush(file);FlushInput();
}
inline void Stats(){if(enabled.load()&&output)LogInput("{\"event\":\"numeric_stats\",\"issued\":%llu,\"completed\":%llu,\"busy_drops\":%llu,\"invalid_drops\":%llu,\"failures\":%llu,\"stopped\":%s}\n",issued,completed,busyDrops,invalidDrops,failures,stopped?"true":"false");}
inline void Cleanup(){enabled.store(false);for(auto& t:targets){auto p=t.exchange(nullptr);if(p)p->Release();}for(auto& j:jobs)j=Job{};gather.Reset();if(file){fclose(file);file=nullptr;}}
}
