#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "DlssNr_Common.h"
#include "Sh0_Native_Shader.h"
#include "Sh0_Native_Constants.h"
#include <algorithm>
#include <cstring>
namespace DlssNr::Sh0Native {
using Microsoft::WRL::ComPtr;
struct Dx11Image {
 ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11UnorderedAccessView> uav;
};
class Dx11Renderer {
 ComPtr<ID3D11Device> device;ComPtr<ID3D11ComputeShader> shader;ComPtr<ID3D11Buffer> constants;
 Dx11Image input,fullA,fullB,halfA,halfB;UINT width=0,height=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 bool make(Dx11Image& image,UINT w,UINT h,DXGI_FORMAT f){
  if(image.texture)return true;
  D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.Format=f;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
  Dx11Image next;if(FAILED(device->CreateTexture2D(&d,nullptr,&next.texture))||FAILED(device->CreateShaderResourceView(next.texture.Get(),nullptr,&next.srv))||FAILED(device->CreateUnorderedAccessView(next.texture.Get(),nullptr,&next.uav)))return false;
  image=std::move(next);return true;
 }
 public:
 // Caller owns frame completion and graphics/compute state restoration.
 // Native DX11 completes the previous compose before retiring/resizing this object.
 bool Apply(ID3D11Device* dev,ID3D11DeviceContext* ctx,ID3D11Texture2D* target,const DlssNrConstants& base,bool half){
  if(!dev||!ctx||!target)return false;
  D3D11_TEXTURE2D_DESC desc{};target->GetDesc(&desc);
  if(desc.SampleDesc.Count!=1||desc.ArraySize!=1||desc.MipLevels!=1)return false;
  if(device.Get()!=dev){*this=Dx11Renderer{};device=dev;}
  if(!shader){ComPtr<ID3DBlob> code,error;if(FAILED(D3DCompile(Shader,std::strlen(Shader),"D18_SH0_Native",nullptr,nullptr,"CSMain","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error)))return false;
   if(FAILED(dev->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader)))return false;
  }
  if(!constants){D3D11_BUFFER_DESC d{};d.ByteWidth=sizeof(DlssNrConstants);d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;if(FAILED(dev->CreateBuffer(&d,nullptr,&constants)))return false;}
  if(width!=desc.Width||height!=desc.Height||format!=desc.Format){input={};fullA={};fullB={};halfA={};halfB={};width=desc.Width;height=desc.Height;format=desc.Format;}
  if(!make(input,width,height,format)||!make(fullA,width,height,DXGI_FORMAT_R32G32B32A32_FLOAT))return false;
  if(half){if(!make(halfA,(width+1)/2,(height+1)/2,DXGI_FORMAT_R32G32B32A32_FLOAT)||!make(halfB,(width+1)/2,(height+1)/2,DXGI_FORMAT_R32G32B32A32_FLOAT))return false;}
  else if(!make(fullB,width,height,DXGI_FORMAT_R32G32B32A32_FLOAT))return false;
  ComPtr<ID3D11UnorderedAccessView> out;if(FAILED(dev->CreateUnorderedAccessView(target,nullptr,&out)))return false;
  ctx->CopyResource(input.texture.Get(),target);
  auto dispatch=[&](UINT mode,ID3D11ShaderResourceView* color,ID3D11ShaderResourceView* filtered,ID3D11ShaderResourceView* coarse,ID3D11UnorderedAccessView* dst,UINT w,UINT h){
   auto c=Constants(base);c.Mode=mode;c.Width=width;c.Height=height;c.DebugView=0;
   ctx->UpdateSubresource(constants.Get(),0,nullptr,&c,0,0);auto cb=constants.Get();ctx->CSSetShader(shader.Get(),nullptr,0);ctx->CSSetConstantBuffers(0,1,&cb);
   ID3D11ShaderResourceView* srvs[]={color,filtered,coarse};ID3D11UnorderedAccessView* uavs[]={dst,nullptr};ctx->CSSetShaderResources(0,3,srvs);ctx->CSSetUnorderedAccessViews(0,2,uavs,nullptr);ctx->Dispatch((w+7)/8,(h+7)/8,1);
   std::memset(srvs,0,sizeof(srvs));std::memset(uavs,0,sizeof(uavs));ctx->CSSetShaderResources(0,3,srvs);ctx->CSSetUnorderedAccessViews(0,2,uavs,nullptr);
  };
  if(half){UINT w=(width+1)/2,h=(height+1)/2;
   dispatch(23,input.srv.Get(),nullptr,nullptr,halfA.uav.Get(),w,h);dispatch(24,nullptr,halfA.srv.Get(),nullptr,halfB.uav.Get(),w,h);dispatch(25,nullptr,halfB.srv.Get(),nullptr,halfA.uav.Get(),w,h);
   dispatch(26,input.srv.Get(),nullptr,nullptr,fullA.uav.Get(),width,height);dispatch(27,input.srv.Get(),fullA.srv.Get(),halfA.srv.Get(),out.Get(),width,height);
  }else{dispatch(20,input.srv.Get(),nullptr,nullptr,fullA.uav.Get(),width,height);dispatch(21,nullptr,fullA.srv.Get(),nullptr,fullB.uav.Get(),width,height);dispatch(22,input.srv.Get(),fullB.srv.Get(),nullptr,out.Get(),width,height);}
  return true;
 }
};
}
