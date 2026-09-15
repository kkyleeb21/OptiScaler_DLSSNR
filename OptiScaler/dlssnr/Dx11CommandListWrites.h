#pragma once
#include <d3d11_4.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <new>
#include <vector>
#include "Dx11WriterJournal.h"
#include "BuildProfile.h"
// Bounded, resource-owning metadata. No deferred-context Get* binding queries.
namespace DlssNr::Dx11CommandListWrites {
using Microsoft::WRL::ComPtr;
inline const GUID contextTag={0x62ec2921,0x39d1,0x40c2,{0xa6,0xb6,0x8a,0xf9,0xcd,0x23,0x10,0x01}};
inline const GUID listTag={0x62ec2921,0x39d1,0x40c2,{0xa6,0xb6,0x8a,0xf9,0xcd,0x23,0x10,0x02}};
enum Reason:UINT {PrefixMissing=1,BindingsUnknown=2,CoverageMissing=4,Overflow=8,Unsupported=16};
// Detailed operation records are research-only. Resource write tracking remains active.
inline std::atomic<bool> researchTrace{false};
inline bool TraceEnabled(){
 if constexpr(!BuildProfile::Diagnostic)return false;
 return researchTrace.load(std::memory_order_relaxed);
}
inline std::atomic<bool> extendedTrace{false};
inline std::atomic<ULONGLONG> conflictTraceUntil{0};
inline bool ExtendedTrace(){auto until=conflictTraceUntil.load(std::memory_order_relaxed);return extendedTrace.load(std::memory_order_relaxed)||(until&&GetTickCount64()<until);}
inline std::mutex creationMutex;
inline std::atomic<UINT> contexts{0},lists{0};inline std::atomic<UINT64> serial{0};
inline bool PinMetadataModule(){static const bool pinned=[](){HMODULE module=nullptr;return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&PinMetadataModule),&module)!=FALSE;}();return pinned;}
struct Ref: IUnknown {
 std::atomic<ULONG> refs{1};
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(riid!=__uuidof(IUnknown))return E_NOINTERFACE;*p=static_cast<IUnknown*>(this);AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
 ULONG STDMETHODCALLTYPE Release()override{ULONG n=--refs;if(!n)delete this;return n;}
 virtual ~Ref()=default;
};
struct ResourceInfo {
 UINT64 id=0;UINT dimension=0,width=0,height=0,depth=0,format=0,mips=0,array=0,samples=0,bind=0,bytes=0,stride=0;
 UINT viewFormat=0,viewDimension=0,first=0,count=0;
};
inline ResourceInfo Describe(ID3D11Resource* r){ResourceInfo o;if(!r)return o;o.id=Dx11WriterJournal::Id(r);D3D11_RESOURCE_DIMENSION dim{};r->GetType(&dim);o.dimension=dim;
 ComPtr<ID3D11Buffer> b;ComPtr<ID3D11Texture2D> t;ComPtr<ID3D11Texture3D> t3;ComPtr<ID3D11Texture1D> t1;
 if(SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&b)))){D3D11_BUFFER_DESC d{};b->GetDesc(&d);o.bytes=d.ByteWidth;o.stride=d.StructureByteStride;o.bind=d.BindFlags;}
 else if(SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&t)))){D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);o.width=d.Width;o.height=d.Height;o.mips=d.MipLevels;o.array=d.ArraySize;o.samples=d.SampleDesc.Count;o.format=d.Format;o.bind=d.BindFlags;}
 else if(SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&t3)))){D3D11_TEXTURE3D_DESC d{};t3->GetDesc(&d);o.width=d.Width;o.height=d.Height;o.depth=d.Depth;o.mips=d.MipLevels;o.format=d.Format;o.bind=d.BindFlags;}
 else if(SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&t1)))){D3D11_TEXTURE1D_DESC d{};t1->GetDesc(&d);o.width=d.Width;o.mips=d.MipLevels;o.array=d.ArraySize;o.format=d.Format;o.bind=d.BindFlags;}return o;
}
inline ResourceInfo Describe(ID3D11View* v){if(!v)return {};ComPtr<ID3D11Resource> r;v->GetResource(&r);auto o=Describe(r.Get());
 ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11UnorderedAccessView> uav;ComPtr<ID3D11RenderTargetView> rt;ComPtr<ID3D11DepthStencilView> ds;
 if(SUCCEEDED(v->QueryInterface(IID_PPV_ARGS(&srv)))){D3D11_SHADER_RESOURCE_VIEW_DESC d{};srv->GetDesc(&d);o.viewFormat=d.Format;o.viewDimension=d.ViewDimension;if(d.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D){o.first=d.Texture2D.MostDetailedMip;o.count=d.Texture2D.MipLevels;}}
 else if(SUCCEEDED(v->QueryInterface(IID_PPV_ARGS(&uav)))){D3D11_UNORDERED_ACCESS_VIEW_DESC d{};uav->GetDesc(&d);o.viewFormat=d.Format;o.viewDimension=d.ViewDimension;if(d.ViewDimension==D3D11_UAV_DIMENSION_TEXTURE2D){o.first=d.Texture2D.MipSlice;o.count=1;}}
 else if(SUCCEEDED(v->QueryInterface(IID_PPV_ARGS(&rt)))){D3D11_RENDER_TARGET_VIEW_DESC d{};rt->GetDesc(&d);o.viewFormat=d.Format;o.viewDimension=d.ViewDimension;if(d.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D){o.first=d.Texture2D.MipSlice;o.count=1;}}
 else if(SUCCEEDED(v->QueryInterface(IID_PPV_ARGS(&ds)))){D3D11_DEPTH_STENCIL_VIEW_DESC d{};ds->GetDesc(&d);o.viewFormat=d.Format;o.viewDimension=d.ViewDimension;if(d.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D){o.first=d.Texture2D.MipSlice;o.count=1;}}return o;
}
struct Operation {UINT method=0,binding=0,ordinal=0,thread=0;UINT64 tick=0,shader=0;ResourceInfo destination,source;UINT args[12]{};};
struct Writes {
 std::vector<Operation> trace;UINT traceSize=0,traceDropped=0;
 Operation* Trace(UINT method,UINT binding=0){if(!TraceEnabled())return nullptr;if(traceSize>=(ExtendedTrace()?4096u:512u)){if(traceDropped<UINT(-1))++traceDropped;return nullptr;}try{trace.emplace_back();}catch(const std::bad_alloc&){if(traceDropped<UINT(-1))++traceDropped;return nullptr;}auto& op=trace[traceSize++];op.method=method;op.binding=binding;op.ordinal=operations;op.thread=GetCurrentThreadId();op.tick=GetTickCount64();return &op;}
 ComPtr<ID3D11Resource> resources[64];UINT size=0,reasons=PrefixMissing,operations=0,draws=0,dispatches=0;
 void Add(ID3D11Resource* r){if(!r)return;for(UINT i=0;i<size;++i)if(resources[i].Get()==r)return;if(size==64){reasons|=Overflow;return;}resources[size++]=r;}
 void Add(ID3D11View* v){if(v){ComPtr<ID3D11Resource> r;v->GetResource(&r);Add(r.Get());}}
 void CountOperation(){if(operations<4097)++operations;if(operations>4096)reasons|=Overflow;}
};
struct List final:Ref {Writes writes;UINT64 id=++serial;~List(){--lists;}};
struct Recording final:Ref {
 std::mutex mutex;Writes writes;bool bindingsKnown=false;UINT uavSlots=8;
 ComPtr<ID3D11RenderTargetView> rt[8];ComPtr<ID3D11DepthStencilView> ds;
 ComPtr<ID3D11UnorderedAccessView> om[64],cs[64];ComPtr<ID3D11Buffer> so[4];
 void ClearBindings(){for(auto& p:rt)p.Reset();ds.Reset();for(auto& p:om)p.Reset();for(auto& p:cs)p.Reset();for(auto& p:so)p.Reset();bindingsKnown=true;}
 void Draw(){if(writes.draws<4097)++writes.draws;if(!bindingsKnown)writes.reasons|=BindingsUnknown;for(auto& p:rt)writes.Add(p.Get());writes.Add(ds.Get());for(auto& p:om)writes.Add(p.Get());for(auto& p:so)writes.Add(p.Get());}
 void Dispatch(){if(writes.dispatches<4097)++writes.dispatches;if(!bindingsKnown)writes.reasons|=BindingsUnknown;for(auto& p:cs)writes.Add(p.Get());}
 ~Recording(){--contexts;}
};
template<class T,class Child> inline ComPtr<T> Read(Child* child,const GUID& tag){ComPtr<T> result;if(!child)return result;IUnknown* raw=nullptr;UINT size=sizeof(raw);if(SUCCEEDED(child->GetPrivateData(tag,&size,&raw))&&raw)result.Attach(static_cast<T*>(raw));return result;}
inline ComPtr<Recording> Get(ID3D11DeviceContext* c){
 auto value=Read<Recording>(c,contextTag);if(value)return value;
 std::lock_guard creationLock(creationMutex);value=Read<Recording>(c,contextTag);if(value)return value;
 if(!PinMetadataModule())return {};
 if(contexts.fetch_add(1)>=64){--contexts;return {};}
 auto p=new(std::nothrow) Recording;if(!p){--contexts;return {};}
 value.Attach(p);ComPtr<ID3D11Device> device;c->GetDevice(&device);p->uavSlots=device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?64:8;if(FAILED(c->SetPrivateDataInterface(contextTag,p)))return {};return value;
}
inline void Finish(ID3D11DeviceContext* c,ID3D11CommandList* list,BOOL restore,bool covered){
 auto recording=Get(c);if(!recording||!list)return;std::lock_guard lock(recording->mutex);
 if(lists.fetch_add(1)<256){ComPtr<List> frozen;auto p=new(std::nothrow) List;if(p){frozen.Attach(p);try{p->writes=recording->writes;}catch(const std::bad_alloc&){p->writes.reasons|=Overflow;}if(!covered)p->writes.reasons|=CoverageMissing;list->SetPrivateDataInterface(listTag,p);}else --lists;}else --lists;
 recording->writes=Writes{};recording->writes.reasons=covered?0u:UINT(CoverageMissing);
 if(!restore)recording->ClearBindings();
}
}
