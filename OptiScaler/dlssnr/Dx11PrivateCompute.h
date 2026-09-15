#pragma once
#include <d3d11_1.h>
#include <cstring>
#include <d3dcompiler.h>
#include <wrl/client.h>

// Private GPU-only preparation. Caller owns the context transaction/state snapshot,
// suppresses observers, and keeps this workspace alive until its GPU work retires.
namespace DlssNr {
class Dx11PrivateCompute {
 using MicrosoftBuffer = Microsoft::WRL::ComPtr<ID3D11Buffer>;
 MicrosoftBuffer raw,constants,parameters;
 Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> rawUav,scalarUav;
 Microsoft::WRL::ComPtr<ID3D11Texture2D> scalar;
 Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> scalarSrv;
 Microsoft::WRL::ComPtr<ID3D11ComputeShader> patch,resize;
 UINT bytes=0,w=0,h=0;
 static HRESULT Compile(ID3D11Device* d,const char* text,ID3D11ComputeShader** out){
  Microsoft::WRL::ComPtr<ID3DBlob> code,error;
  auto hr=D3DCompile(text,strlen(text),nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
  return FAILED(hr)?hr:d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,out);
 }
 public:
 ID3D11Buffer* Constants()const{return constants.Get();}
 ID3D11ShaderResourceView* Scalar()const{return scalarSrv.Get();}
 HRESULT Prepare(ID3D11DeviceContext* c,ID3D11Buffer* source,UINT first,UINT outWidth,UINT outHeight,ID3D11ShaderResourceView* guide){
  if(!c||!source||!guide||!outWidth||!outHeight||outWidth>8192||outHeight>8192||UINT64(outWidth)*outHeight>16777216)return E_INVALIDARG;
  D3D11_BUFFER_DESC bd{};source->GetDesc(&bd);
  if(!bd.ByteWidth||bd.ByteWidth>65536||bd.ByteWidth%16||!(bd.BindFlags&D3D11_BIND_CONSTANT_BUFFER)||UINT64(first)*16+48>bd.ByteWidth)return E_INVALIDARG;
  if((bytes&&bytes!=bd.ByteWidth)||(w&&(w!=outWidth||h!=outHeight)))return E_INVALIDARG;
  D3D11_SHADER_RESOURCE_VIEW_DESC sv{};guide->GetDesc(&sv);
  Microsoft::WRL::ComPtr<ID3D11Resource> gr;guide->GetResource(&gr);
  Microsoft::WRL::ComPtr<ID3D11Texture2D> gt;if(FAILED(gr.As(&gt)))return E_INVALIDARG;
  D3D11_TEXTURE2D_DESC td{};gt->GetDesc(&td);
  if(sv.Format!=DXGI_FORMAT_R32_FLOAT||sv.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||sv.Texture2D.MostDetailedMip!=0||sv.Texture2D.MipLevels!=1||td.MipLevels!=1||td.ArraySize!=1||td.SampleDesc.Count!=1)return E_INVALIDARG;
  Microsoft::WRL::ComPtr<ID3D11Device> d;c->GetDevice(&d);HRESULT hr=S_OK;
  if(!patch)hr=Compile(d.Get(),"RWByteAddressBuffer b:register(u0);cbuffer P:register(b0){float2 invSize;uint offset;uint pad;}[numthreads(1,1,1)]void main(){b.Store2(offset+4,asuint(invSize));}",&patch);
  if(SUCCEEDED(hr)&&!resize)hr=Compile(d.Get(),"Texture2D<float> s:register(t0);RWTexture2D<float> d:register(u0);[numthreads(8,8,1)]void main(uint3 p:SV_DispatchThreadID){uint sw,sh,w,h;s.GetDimensions(sw,sh);d.GetDimensions(w,h);if(p.x<w&&p.y<h){uint2 q=min(uint2((float2(p.xy)+0.5)*float2(sw,sh)/float2(w,h)),uint2(sw-1,sh-1));d[p.xy]=s.Load(int3(q,0));}}",&resize);
  if(FAILED(hr))return hr;
  if(!raw){D3D11_BUFFER_DESC b{};b.ByteWidth=bd.ByteWidth;b.BindFlags=D3D11_BIND_UNORDERED_ACCESS;b.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
   hr=d->CreateBuffer(&b,nullptr,&raw);D3D11_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=DXGI_FORMAT_R32_TYPELESS;u.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=b.ByteWidth/4;u.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
   if(SUCCEEDED(hr))hr=d->CreateUnorderedAccessView(raw.Get(),&u,&rawUav);
   b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.MiscFlags=0;if(SUCCEEDED(hr))hr=d->CreateBuffer(&b,nullptr,&constants);
   b.ByteWidth=16;if(SUCCEEDED(hr))hr=d->CreateBuffer(&b,nullptr,&parameters);
   if(FAILED(hr)){raw.Reset();rawUav.Reset();constants.Reset();parameters.Reset();return hr;}bytes=bd.ByteWidth;
  }
  if(!scalar){D3D11_TEXTURE2D_DESC t{};t.Width=outWidth;t.Height=outHeight;t.MipLevels=t.ArraySize=t.SampleDesc.Count=1;t.Format=DXGI_FORMAT_R32_FLOAT;t.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
   hr=d->CreateTexture2D(&t,nullptr,&scalar);if(SUCCEEDED(hr))hr=d->CreateShaderResourceView(scalar.Get(),nullptr,&scalarSrv);if(SUCCEEDED(hr))hr=d->CreateUnorderedAccessView(scalar.Get(),nullptr,&scalarUav);
   if(FAILED(hr)){scalar.Reset();scalarSrv.Reset();scalarUav.Reset();return hr;}w=outWidth;h=outHeight;
  }
  struct Parameters {float x,y;UINT offset,pad;} p{1.0f/float(outWidth),1.0f/float(outHeight),first*16,0};
  c->UpdateSubresource(parameters.Get(),0,nullptr,&p,0,0);
  // Entire-buffer copies preserve every original constant, including ranged bindings.
  c->CopyResource(raw.Get(),source);auto u=rawUav.Get();auto cb=parameters.Get();
  c->CSSetShader(patch.Get(),nullptr,0);c->CSSetConstantBuffers(0,1,&cb);c->CSSetUnorderedAccessViews(0,1,&u,nullptr);c->Dispatch(1,1,1);
  u=nullptr;c->CSSetUnorderedAccessViews(0,1,&u,nullptr);c->CopyResource(constants.Get(),raw.Get());
  c->CSSetShader(resize.Get(),nullptr,0);c->CSSetShaderResources(0,1,&guide);u=scalarUav.Get();c->CSSetUnorderedAccessViews(0,1,&u,nullptr);c->Dispatch((w+7)/8,(h+7)/8,1);
  u=nullptr;ID3D11ShaderResourceView* empty=nullptr;c->CSSetUnorderedAccessViews(0,1,&u,nullptr);c->CSSetShaderResources(0,1,&empty);
  return S_OK;
 }
};
}
