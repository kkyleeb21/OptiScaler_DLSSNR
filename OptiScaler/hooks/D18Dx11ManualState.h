#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include "D18Dx11Bindings.h"
// Shared UI/SR snapshot. No context-state creation or switching.
// SO offsets cannot be queried: reject bound stream-output before mutation.
namespace D18Dx11ManualState {
template<class T> inline void Release(T*& p){if(p){p->Release();p=nullptr;}}
template<class T,size_t N> inline void Release(T* (&p)[N]){for(auto& v:p)Release(v);}
struct StageResources {ID3D11Buffer* cb[14]{};UINT first[14]{},count[14]{};ID3D11ShaderResourceView* srv[128]{};ID3D11SamplerState* samplers[16]{};ID3D11ClassInstance* classes[256]{};UINT classCount=256;~StageResources(){Release(cb);Release(srv);Release(samplers);Release(classes);}};
class Snapshot {
 Microsoft::WRL::ComPtr<ID3D11DeviceContext1> c;
 UINT slots=8;bool valid=false;
 ID3D11InputLayout* layout=nullptr;ID3D11Buffer* vb[32]{},*ib=nullptr;UINT strides[32]{},offsets[32]{},ibOffset=0;DXGI_FORMAT ibFormat{};D3D11_PRIMITIVE_TOPOLOGY topology{};
 ID3D11RasterizerState* raster=nullptr;D3D11_VIEWPORT viewports[16]{};D3D11_RECT scissors[16]{};UINT viewportCount=16,scissorCount=16;
 ID3D11RenderTargetView* rt[8]{};ID3D11DepthStencilView* ds=nullptr;ID3D11UnorderedAccessView* omUav[64]{},*csUav[64]{};
 ID3D11BlendState* blend=nullptr;FLOAT factors[4]{};UINT mask=0;ID3D11DepthStencilState* depth=nullptr;UINT stencil=0;ID3D11Predicate* predicate=nullptr;BOOL predValue=FALSE;
 StageResources VS;ID3D11VertexShader* shaderVS=nullptr;
 StageResources HS;ID3D11HullShader* shaderHS=nullptr;
 StageResources DS;ID3D11DomainShader* shaderDS=nullptr;
 StageResources GS;ID3D11GeometryShader* shaderGS=nullptr;
 StageResources PS;ID3D11PixelShader* shaderPS=nullptr;
 StageResources CS;ID3D11ComputeShader* shaderCS=nullptr;
 public:
 HRESULT Capture(ID3D11DeviceContext* input){
  if(!input||input->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return E_NOINTERFACE;
  auto hr=input->QueryInterface(IID_PPV_ARGS(&c));if(FAILED(hr))return hr;
  ID3D11Buffer* so[4]{};c->SOGetTargets(4,so);bool active=false;for(auto b:so)if(b)active=true;Release(so);if(active)return E_NOTIMPL;
  Microsoft::WRL::ComPtr<ID3D11Device> d;c->GetDevice(&d);if(d->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0)return E_NOTIMPL;slots=d->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?64:8;
  c->IAGetInputLayout(&layout);c->IAGetVertexBuffers(0,32,vb,strides,offsets);c->IAGetIndexBuffer(&ib,&ibFormat,&ibOffset);c->IAGetPrimitiveTopology(&topology);
  c->RSGetState(&raster);c->RSGetViewports(&viewportCount,viewports);c->RSGetScissorRects(&scissorCount,scissors);
  c->OMGetRenderTargets(8,rt,&ds);c->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,slots,omUav);c->CSGetUnorderedAccessViews(0,slots,csUav);
  c->OMGetBlendState(&blend,factors,&mask);c->OMGetDepthStencilState(&depth,&stencil);c->GetPredication(&predicate,&predValue);
 c->VSGetShader(&shaderVS,VS.classes,&VS.classCount);c->VSGetConstantBuffers1(0,14,VS.cb,VS.first,VS.count);c->VSGetShaderResources(0,128,VS.srv);c->VSGetSamplers(0,16,VS.samplers);
 c->HSGetShader(&shaderHS,HS.classes,&HS.classCount);c->HSGetConstantBuffers1(0,14,HS.cb,HS.first,HS.count);c->HSGetShaderResources(0,128,HS.srv);c->HSGetSamplers(0,16,HS.samplers);
 c->DSGetShader(&shaderDS,DS.classes,&DS.classCount);c->DSGetConstantBuffers1(0,14,DS.cb,DS.first,DS.count);c->DSGetShaderResources(0,128,DS.srv);c->DSGetSamplers(0,16,DS.samplers);
 c->GSGetShader(&shaderGS,GS.classes,&GS.classCount);c->GSGetConstantBuffers1(0,14,GS.cb,GS.first,GS.count);c->GSGetShaderResources(0,128,GS.srv);c->GSGetSamplers(0,16,GS.samplers);
 c->PSGetShader(&shaderPS,PS.classes,&PS.classCount);c->PSGetConstantBuffers1(0,14,PS.cb,PS.first,PS.count);c->PSGetShaderResources(0,128,PS.srv);c->PSGetSamplers(0,16,PS.samplers);
 c->CSGetShader(&shaderCS,CS.classes,&CS.classCount);c->CSGetConstantBuffers1(0,14,CS.cb,CS.first,CS.count);c->CSGetShaderResources(0,128,CS.srv);c->CSGetSamplers(0,16,CS.samplers);
 valid=true;return S_OK;
 }
 void Reset(){D18Dx11Bindings::Reset(c.Get(),slots);}
 void Restore(){if(!valid)return;Reset();
 c->IASetInputLayout(layout);c->IASetVertexBuffers(0,32,vb,strides,offsets);c->IASetIndexBuffer(ib,ibFormat,ibOffset);c->IASetPrimitiveTopology(topology);
 c->RSSetState(raster);c->RSSetViewports(viewportCount,viewports);c->RSSetScissorRects(scissorCount,scissors);
 UINT rtCount=0;for(UINT i=0;i<8;++i)if(rt[i])rtCount=i+1;
 c->OMSetRenderTargetsAndUnorderedAccessViews(rtCount,rt,ds,rtCount,slots-rtCount,omUav+rtCount,nullptr);c->CSSetUnorderedAccessViews(0,slots,csUav,nullptr);
 c->VSSetShader(shaderVS,VS.classes,VS.classCount);c->VSSetConstantBuffers1(0,14,VS.cb,VS.first,VS.count);c->VSSetShaderResources(0,128,VS.srv);c->VSSetSamplers(0,16,VS.samplers);
 c->HSSetShader(shaderHS,HS.classes,HS.classCount);c->HSSetConstantBuffers1(0,14,HS.cb,HS.first,HS.count);c->HSSetShaderResources(0,128,HS.srv);c->HSSetSamplers(0,16,HS.samplers);
 c->DSSetShader(shaderDS,DS.classes,DS.classCount);c->DSSetConstantBuffers1(0,14,DS.cb,DS.first,DS.count);c->DSSetShaderResources(0,128,DS.srv);c->DSSetSamplers(0,16,DS.samplers);
 c->GSSetShader(shaderGS,GS.classes,GS.classCount);c->GSSetConstantBuffers1(0,14,GS.cb,GS.first,GS.count);c->GSSetShaderResources(0,128,GS.srv);c->GSSetSamplers(0,16,GS.samplers);
 c->PSSetShader(shaderPS,PS.classes,PS.classCount);c->PSSetConstantBuffers1(0,14,PS.cb,PS.first,PS.count);c->PSSetShaderResources(0,128,PS.srv);c->PSSetSamplers(0,16,PS.samplers);
 c->CSSetShader(shaderCS,CS.classes,CS.classCount);c->CSSetConstantBuffers1(0,14,CS.cb,CS.first,CS.count);c->CSSetShaderResources(0,128,CS.srv);c->CSSetSamplers(0,16,CS.samplers);
 c->OMSetBlendState(blend,factors,mask);c->OMSetDepthStencilState(depth,stencil);c->SetPredication(predicate,predValue);valid=false;
 }
 ~Snapshot(){Restore();Release(layout);Release(vb);Release(ib);Release(raster);Release(rt);Release(ds);Release(omUav);Release(csUav);Release(blend);Release(depth);Release(predicate);
 Release(shaderVS);
 Release(shaderHS);
 Release(shaderDS);
 Release(shaderGS);
 Release(shaderPS);
 Release(shaderCS);
 }
 Snapshot()=default;Snapshot(const Snapshot&)=delete;Snapshot& operator=(const Snapshot&)=delete;
};
}
