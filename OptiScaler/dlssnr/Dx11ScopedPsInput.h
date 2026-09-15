#pragma once
#include <d3d11.h>
#include <wrl/client.h>
namespace DlssNr {
// A profile supplies shader/UV semantics. This scope only checks the resource
// contract and restores the one input binding; the caller owns GPU lifetime.
class Dx11ScopedPsInput {
 using MicrosoftPtr=Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>;
 ID3D11DeviceContext* context=nullptr;UINT slot=0;
 MicrosoftPtr original,replacement;
 public:
 enum Result:UINT {Ready,Missing,ViewContract,SizeContract,SamplerContract,Alias};
 Result Bind(ID3D11DeviceContext* c,UINT inputSlot,UINT samplerSlot,
             ID3D11ShaderResourceView* view,UINT width,UINT height){
  if(!c||!view||context||!width||!height)return Missing;
  c->PSGetShaderResources(inputSlot,1,&original);if(!original)return Missing;
  D3D11_SHADER_RESOURCE_VIEW_DESC a{},b{};original->GetDesc(&a);view->GetDesc(&b);
  if(a.Format!=b.Format||b.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||b.Texture2D.MostDetailedMip!=0||b.Texture2D.MipLevels!=1)return ViewContract;
  Microsoft::WRL::ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture)))return ViewContract;
  D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);
  D3D11_VIEWPORT vp{};UINT count=1;c->RSGetViewports(&count,&vp);
  if(td.Width!=width||td.Height!=height||td.MipLevels!=1||td.ArraySize!=1||td.SampleDesc.Count!=1||count!=1||vp.TopLeftX!=0||vp.TopLeftY!=0||vp.Width!=float(width)||vp.Height!=float(height))return SizeContract;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;c->PSGetSamplers(samplerSlot,1,&sampler);if(!sampler)return Missing;
  D3D11_SAMPLER_DESC sd{};sampler->GetDesc(&sd);
  if(sd.Filter!=D3D11_FILTER_MIN_MAG_MIP_POINT&&sd.Filter!=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT&&sd.Filter!=D3D11_FILTER_MIN_MAG_MIP_LINEAR)return SamplerContract;
  ID3D11RenderTargetView* targets[8]{};Microsoft::WRL::ComPtr<ID3D11DepthStencilView> ds;c->OMGetRenderTargets(8,targets,&ds);
  bool alias=false;for(auto p:targets)if(p){Microsoft::WRL::ComPtr<ID3D11Resource> output;p->GetResource(&output);alias|=output==resource;p->Release();}
  if(ds){Microsoft::WRL::ComPtr<ID3D11Resource> depth;ds->GetResource(&depth);alias|=depth==resource;}
  if(alias)return Alias;
  replacement=view;slot=inputSlot;context=c;auto raw=replacement.Get();context->PSSetShaderResources(slot,1,&raw);return Ready;
 }
 void Restore(){if(context){auto raw=original.Get();context->PSSetShaderResources(slot,1,&raw);context=nullptr;}}
 ~Dx11ScopedPsInput(){Restore();}
 Dx11ScopedPsInput()=default;Dx11ScopedPsInput(const Dx11ScopedPsInput&)=delete;
 Dx11ScopedPsInput& operator=(const Dx11ScopedPsInput&)=delete;
};
}
