#pragma once
// Geometry replay for adapters whose replacement PS preserves the original
// coverage semantics (no discard/depth export). No vertex-count whitelist.
namespace Dx11DrawReplay {
using Microsoft::WRL::ComPtr;
enum class Kind { Unsupported, Draw, Indexed, Instanced, IndexedInstanced };
struct Command {
 Kind kind=Kind::Unsupported;UINT count=0,first=0,instances=1,firstInstance=0;INT baseVertex=0;
 bool Supported()const{return kind!=Kind::Unsupported&&count&&instances;}
 void Run(ID3D11DeviceContext* c)const{
  switch(kind){
   case Kind::Draw:c->Draw(count,first);break;
   case Kind::Indexed:c->DrawIndexed(count,first,baseVertex);break;
   case Kind::Instanced:c->DrawInstanced(count,instances,first,firstInstance);break;
   case Kind::IndexedInstanced:c->DrawIndexedInstanced(count,instances,first,baseVertex,firstInstance);break;
   default:break;
  }
 }
};
// Capture before switching to a private state. Replay only IA/VS/raster state;
// the adapter owns its replacement PS and outputs. SO/GS/HS/DS must be rejected
// by the adapter so replay cannot write stream-output or repeat shader stages.
struct Geometry {
 ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11Buffer> index;
 DXGI_FORMAT indexFormat=DXGI_FORMAT_UNKNOWN;UINT indexOffset=0;
 D3D11_PRIMITIVE_TOPOLOGY topology{};
 ComPtr<ID3D11Buffer> vertices[32],constants[14];UINT strides[32]{},offsets[32]{},firsts[14]{},counts[14]{};
 ComPtr<ID3D11ShaderResourceView> views[128];ComPtr<ID3D11SamplerState> samplers[16];
 ComPtr<ID3D11VertexShader> shader;ComPtr<ID3D11ClassInstance> classes[256];UINT classCount=256;
 ComPtr<ID3D11RasterizerState> raster;D3D11_VIEWPORT viewport{};D3D11_RECT scissors[16]{};UINT scissorCount=16;
 bool Capture(ID3D11DeviceContext* c){
  ComPtr<ID3D11DeviceContext1> c1;if(FAILED(c->QueryInterface(IID_PPV_ARGS(&c1))))return false;
  c->IAGetInputLayout(&layout);c->IAGetIndexBuffer(&index,&indexFormat,&indexOffset);c->IAGetPrimitiveTopology(&topology);
  ID3D11Buffer* vb[32]{};c->IAGetVertexBuffers(0,32,vb,strides,offsets);for(UINT i=0;i<32;++i)vertices[i].Attach(vb[i]);
  ID3D11Buffer* cb[14]{};c1->VSGetConstantBuffers1(0,14,cb,firsts,counts);for(UINT i=0;i<14;++i)constants[i].Attach(cb[i]);
  ID3D11ShaderResourceView* sv[128]{};c->VSGetShaderResources(0,128,sv);for(UINT i=0;i<128;++i)views[i].Attach(sv[i]);
  ID3D11SamplerState* ss[16]{};c->VSGetSamplers(0,16,ss);for(UINT i=0;i<16;++i)samplers[i].Attach(ss[i]);
  ID3D11ClassInstance* ci[256]{};c->VSGetShader(&shader,ci,&classCount);for(UINT i=0;i<classCount;++i)classes[i].Attach(ci[i]);
  c->RSGetState(&raster);UINT n=1;c->RSGetViewports(&n,&viewport);c->RSGetScissorRects(&scissorCount,scissors);
  return shader&&n==1;
 }
 void Apply(ID3D11DeviceContext1* c)const{
  c->IASetInputLayout(layout.Get());c->IASetIndexBuffer(index.Get(),indexFormat,indexOffset);c->IASetPrimitiveTopology(topology);
  ID3D11Buffer* vb[32]{};for(UINT i=0;i<32;++i)vb[i]=vertices[i].Get();c->IASetVertexBuffers(0,32,vb,strides,offsets);
  ID3D11Buffer* cb[14]{};for(UINT i=0;i<14;++i)cb[i]=constants[i].Get();c->VSSetConstantBuffers1(0,14,cb,firsts,counts);
  ID3D11ShaderResourceView* sv[128]{};for(UINT i=0;i<128;++i)sv[i]=views[i].Get();c->VSSetShaderResources(0,128,sv);
  ID3D11SamplerState* ss[16]{};for(UINT i=0;i<16;++i)ss[i]=samplers[i].Get();c->VSSetSamplers(0,16,ss);
  ID3D11ClassInstance* ci[256]{};for(UINT i=0;i<classCount;++i)ci[i]=classes[i].Get();c->VSSetShader(shader.Get(),ci,classCount);
  c->RSSetState(raster.Get());c->RSSetViewports(1,&viewport);c->RSSetScissorRects(scissorCount,scissors);
 }
};
}
