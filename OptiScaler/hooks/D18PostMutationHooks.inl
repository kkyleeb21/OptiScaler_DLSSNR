// Diagnostic-only base-context mutation hooks. Shared observer preserves all original calls.
namespace PostMutationHooks {
using CopyResourceFn=void(WINAPI*)(ID3D11DeviceContext*,ID3D11Resource* d,ID3D11Resource* s);
inline CopyResourceFn CopyResource=nullptr;
inline void WINAPI OnCopyResource(ID3D11DeviceContext* c,ID3D11Resource* d,ID3D11Resource* s){CommandLists::Record<47>(c,d,s);Wildlands::PostReplay::TraceWrite(c,"copy_resource",d,s,nullptr,nullptr,_ReturnAddress());Wildlands::PostReplay::Mutation(c,d,"copy_resource");CopyResource(c,d,s);}
using CopySubresourceFn=void(WINAPI*)(ID3D11DeviceContext*,ID3D11Resource* d,UINT ds,UINT x,UINT y,UINT z,ID3D11Resource* s,UINT ss,const D3D11_BOX* b);
inline CopySubresourceFn CopySubresource=nullptr;
inline void WINAPI OnCopySubresource(ID3D11DeviceContext* c,ID3D11Resource* d,UINT ds,UINT x,UINT y,UINT z,ID3D11Resource* s,UINT ss,const D3D11_BOX* b){CommandLists::Record<46>(c,d,ds,x,y,z,s,ss,b);const UINT range[]={ds,ss,x,y,z,b?1u:0u,b?b->left:0,b?b->top:0,b?b->front:0,b?b->right:0,b?b->bottom:0,b?b->back:0};Wildlands::PostReplay::TraceWrite(c,"copy_subresource",d,s,nullptr,nullptr,_ReturnAddress(),range);Wildlands::PostReplay::Mutation(c,d,"copy_subresource");CopySubresource(c,d,ds,x,y,z,s,ss,b);}
using ClearTargetFn=void(WINAPI*)(ID3D11DeviceContext*,ID3D11RenderTargetView* v,const FLOAT colour[4]);
inline ClearTargetFn ClearTarget=nullptr;
inline void WINAPI OnClearTarget(ID3D11DeviceContext* c,ID3D11RenderTargetView* v,const FLOAT colour[4]){CommandLists::Record<50>(c,v,colour);UINT bits[4]{};memcpy(bits,colour,sizeof(bits));Microsoft::WRL::ComPtr<ID3D11Resource> resource;v->GetResource(&resource);Wildlands::PostReplay::TraceWrite(c,"clear_target_float",resource.Get(),nullptr,v,bits,_ReturnAddress());if(!Wildlands::PostReplay::ClearPrivate(c,v,bits,false,true))Wildlands::PostReplay::MutationView(c,v,"clear_target");ClearTarget(c,v,colour);}
using ClearUintFn=void(WINAPI*)(ID3D11DeviceContext*,ID3D11UnorderedAccessView* v,const UINT values[4]);
inline ClearUintFn ClearUint=nullptr;
inline void WINAPI OnClearUint(ID3D11DeviceContext* c,ID3D11UnorderedAccessView* v,const UINT values[4]){CommandLists::Record<51>(c,v,values);UINT bits[4]{};memcpy(bits,values,sizeof(bits));Microsoft::WRL::ComPtr<ID3D11Resource> resource;v->GetResource(&resource);Wildlands::PostReplay::TraceWrite(c,"clear_uav_uint",resource.Get(),nullptr,v,bits,_ReturnAddress());if(!Wildlands::PostReplay::ClearPrivate(c,v,bits,true,false))Wildlands::PostReplay::MutationView(c,v,"clear_uav");ClearUint(c,v,values);}
using ClearFloatFn=void(WINAPI*)(ID3D11DeviceContext*,ID3D11UnorderedAccessView* v,const FLOAT values[4]);
inline ClearFloatFn ClearFloat=nullptr;
inline void WINAPI OnClearFloat(ID3D11DeviceContext* c,ID3D11UnorderedAccessView* v,const FLOAT values[4]){CommandLists::Record<52>(c,v,values);UINT bits[4]{};memcpy(bits,values,sizeof(bits));Microsoft::WRL::ComPtr<ID3D11Resource> resource;v->GetResource(&resource);Wildlands::PostReplay::TraceWrite(c,"clear_uav_float",resource.Get(),nullptr,v,bits,_ReturnAddress());if(!Wildlands::PostReplay::ClearPrivate(c,v,bits,false,false))Wildlands::PostReplay::MutationView(c,v,"clear_uav");ClearFloat(c,v,values);}
using ResolveFn=void(WINAPI*)(ID3D11DeviceContext*,ID3D11Resource* d,UINT ds,ID3D11Resource* s,UINT ss,DXGI_FORMAT f);
inline ResolveFn Resolve=nullptr;
inline void WINAPI OnResolve(ID3D11DeviceContext* c,ID3D11Resource* d,UINT ds,ID3D11Resource* s,UINT ss,DXGI_FORMAT f){CommandLists::Record<57>(c,d,ds,s,ss,f);Wildlands::PostReplay::Mutation(c,d,"resolve_write");Resolve(c,d,ds,s,ss,f);}
using GenerateMipsFn=void(WINAPI*)(ID3D11DeviceContext*,ID3D11ShaderResourceView* v);
inline GenerateMipsFn GenerateMips=nullptr;
inline void WINAPI OnGenerateMips(ID3D11DeviceContext* c,ID3D11ShaderResourceView* v){CommandLists::Record<54>(c,v);Wildlands::PostReplay::MutationView(c,v,"generate_mips");GenerateMips(c,v);}
}
