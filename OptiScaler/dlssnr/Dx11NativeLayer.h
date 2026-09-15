#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
namespace DlssNr {
// Preserve native changed regions without guessing blend factors or replaying draws.
// Caller serializes the context, saves/restores state, and owns GPU lifetime.
// Changed regions intentionally use native resolution; this is not exact SR blending.
class Dx11NativeLayer {
 template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
 Ptr<ID3D11Texture2D> before,scratch;
 Ptr<ID3D11ShaderResourceView> beforeView;
 Ptr<ID3D11UnorderedAccessView> scratchUav;
 Ptr<ID3D11ComputeShader> shader;
 UINT w=0,h=0,ow=0,oh=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 public:
 HRESULT Prepare(ID3D11Device* d,ID3D11Texture2D* original,ID3D11Texture2D* high){
  if(!d||!original||!high||original==high)return E_INVALIDARG;
  D3D11_TEXTURE2D_DESC a{},b{};original->GetDesc(&a);high->GetDesc(&b);
  if(a.Format!=b.Format||(a.Format!=DXGI_FORMAT_R10G10B10A2_UNORM&&a.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)||
     a.MipLevels!=1||b.MipLevels!=1||a.ArraySize!=1||b.ArraySize!=1||a.SampleDesc.Count!=1||b.SampleDesc.Count!=1||
     !a.Width||!a.Height||a.Width>b.Width||a.Height>b.Height||b.Width>8192||b.Height>8192||
     (UINT64(a.Width)*a.Height+UINT64(b.Width)*b.Height)*4>128ULL*1024*1024)return E_INVALIDARG;
  if(before)return a.Width==w&&a.Height==h&&b.Width==ow&&b.Height==oh&&a.Format==format?S_OK:E_INVALIDARG;
  const char* code=R"(
Texture2D<float4> oldNative:register(t0),newNative:register(t1),high:register(t2);
RWTexture2D<float4> output:register(u0);
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID){
 uint sw,sh,dw,dh;oldNative.GetDimensions(sw,sh);output.GetDimensions(dw,dh);
 if(id.x>=dw||id.y>=dh)return;
 float2 p=(float2(id.xy)+0.5)*float2(sw,sh)/float2(dw,dh)-0.5;
 int2 q=int2(floor(p));float2 f=frac(p);bool changed=false;float4 nativeColour=0;
 [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x){
  int2 k=clamp(q+int2(x,y),int2(0,0),int2(sw-1,sh-1));
  float weight=(x?f.x:1-f.x)*(y?f.y:1-f.y);
  float4 after=newNative.Load(int3(k,0));float4 before=oldNative.Load(int3(k,0));
  changed=changed||(weight>0&&any(before!=after));nativeColour+=after*weight;
 }
 output[id.xy]=changed?nativeColour:high.Load(int3(id.xy,0));
})";
  Ptr<ID3DBlob> bytecode,error;auto hr=D3DCompile(code,strlen(code),nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytecode,&error);
  Ptr<ID3D11ComputeShader> cs;Ptr<ID3D11Texture2D> low,out;Ptr<ID3D11ShaderResourceView> view;Ptr<ID3D11UnorderedAccessView> uav;
  if(SUCCEEDED(hr))hr=d->CreateComputeShader(bytecode->GetBufferPointer(),bytecode->GetBufferSize(),nullptr,&cs);
  a.Usage=D3D11_USAGE_DEFAULT;a.CPUAccessFlags=a.MiscFlags=0;a.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  if(SUCCEEDED(hr))hr=d->CreateTexture2D(&a,nullptr,&low);
  if(SUCCEEDED(hr))hr=d->CreateShaderResourceView(low.Get(),nullptr,&view);
  b.Usage=D3D11_USAGE_DEFAULT;b.CPUAccessFlags=b.MiscFlags=0;b.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
  if(SUCCEEDED(hr))hr=d->CreateTexture2D(&b,nullptr,&out);
  if(SUCCEEDED(hr))hr=d->CreateUnorderedAccessView(out.Get(),nullptr,&uav);
  if(FAILED(hr))return hr;
  before=low;beforeView=view;scratch=out;scratchUav=uav;shader=cs;w=a.Width;h=a.Height;ow=b.Width;oh=b.Height;format=a.Format;return S_OK;
 }
 void Snapshot(ID3D11DeviceContext* c,ID3D11Resource* original){c->CopyResource(before.Get(),original);}
 void Merge(ID3D11DeviceContext* c,ID3D11ShaderResourceView* nativeView,ID3D11ShaderResourceView* highView,ID3D11Resource* high){
  ID3D11ShaderResourceView* inputs[]={beforeView.Get(),nativeView,highView};auto u=scratchUav.Get();
  c->CSSetShader(shader.Get(),nullptr,0);c->CSSetShaderResources(0,3,inputs);c->CSSetUnorderedAccessViews(0,1,&u,nullptr);c->Dispatch((ow+7)/8,(oh+7)/8,1);
  ID3D11ShaderResourceView* empty[3]{};u=nullptr;c->CSSetShaderResources(0,3,empty);c->CSSetUnorderedAccessViews(0,1,&u,nullptr);c->CopyResource(high,scratch.Get());
 }
};
}
