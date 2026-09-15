#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include "Dx11DrawReplay.h"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
#define CHECK(x) if(FAILED(x)){printf("failed line %d\n",__LINE__);return 1;}
int main(){
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> base;D3D_FEATURE_LEVEL fl=D3D_FEATURE_LEVEL_11_0;
 CHECK(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&fl,1,D3D11_SDK_VERSION,&d,nullptr,&base));
 ComPtr<ID3D11DeviceContext1> c;CHECK(base.As(&c));
 const char vs[]="cbuffer Offset:register(b0){float4 shift;} float4 main(float2 p:POSITION):SV_Position{return float4(p+shift.xy,0,1);}";
 const char ps[]="float4 main():SV_Target{return float4(0,1,0,1);}";
 ComPtr<ID3DBlob> vc,pc,err;CHECK(D3DCompile(vs,sizeof(vs)-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&vc,&err));CHECK(D3DCompile(ps,sizeof(ps)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&pc,&err));
 ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;CHECK(d->CreateVertexShader(vc->GetBufferPointer(),vc->GetBufferSize(),nullptr,&vertex));CHECK(d->CreatePixelShader(pc->GetBufferPointer(),pc->GetBufferSize(),nullptr,&pixel));
 D3D11_INPUT_ELEMENT_DESC el{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};ComPtr<ID3D11InputLayout> layout;CHECK(d->CreateInputLayout(&el,1,vc->GetBufferPointer(),vc->GetBufferSize(),&layout));
 // Padding proves nonzero VB byte offsets, start vertices, index offsets and base vertices.
 float verts[][2]={{99,99},{99,99},{99,99},{99,99},{-1,1},{0,1},{-1,-1},{-1,-1},{0,1},{0,-1}};
 UINT indices[]={99,99,0,1,2,3,4,5};
 float cbData[128]{};cbData[0]=cbData[1]=100;
 auto buffer=[&](const void* data,UINT size,UINT bind,ComPtr<ID3D11Buffer>& out){D3D11_BUFFER_DESC bd{};bd.ByteWidth=size;bd.BindFlags=bind;D3D11_SUBRESOURCE_DATA sd{data,0,0};return d->CreateBuffer(&bd,&sd,&out);};
 ComPtr<ID3D11Buffer> vb,ib,cb;CHECK(buffer(verts,sizeof(verts),D3D11_BIND_VERTEX_BUFFER,vb));CHECK(buffer(indices,sizeof(indices),D3D11_BIND_INDEX_BUFFER,ib));CHECK(buffer(cbData,sizeof(cbData),D3D11_BIND_CONSTANT_BUFFER,cb));
 D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=64;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> target,read;ComPtr<ID3D11RenderTargetView> rt;CHECK(d->CreateTexture2D(&td,nullptr,&target));CHECK(d->CreateRenderTargetView(target.Get(),nullptr,&rt));td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;CHECK(d->CreateTexture2D(&td,nullptr,&read));
 D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;rd.ScissorEnable=TRUE;ComPtr<ID3D11RasterizerState> raster;CHECK(d->CreateRasterizerState(&rd,&raster));
 Dx11DrawReplay::Command commands[]={{Dx11DrawReplay::Kind::Draw,6,3},{Dx11DrawReplay::Kind::Indexed,6,1,1,0,3},{Dx11DrawReplay::Kind::Instanced,6,3,2,5},{Dx11DrawReplay::Kind::IndexedInstanced,6,1,2,5,3},{Dx11DrawReplay::Kind::Draw,4,0}};
 for(UINT test=0;test<5;++test){
  c->ClearState();auto v=vb.Get();UINT stride=8,offset=8;c->IASetVertexBuffers(0,1,&v,&stride,&offset);c->IASetInputLayout(layout.Get());c->IASetIndexBuffer(ib.Get(),DXGI_FORMAT_R32_UINT,4);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ComPtr<ID3D11Buffer> strip;
  if(test==4){float stripData[][2]={{-1,1},{0,1},{-1,-1},{0,-1}};CHECK(buffer(stripData,sizeof(stripData),D3D11_BIND_VERTEX_BUFFER,strip));v=strip.Get();offset=0;c->IASetVertexBuffers(0,1,&v,&stride,&offset);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);}
  c->VSSetShader(vertex.Get(),nullptr,0);auto constant=cb.Get();UINT first=16,count=16;c->VSSetConstantBuffers1(0,1,&constant,&first,&count);
  D3D11_VIEWPORT viewport{0,0,64,64,0,1};D3D11_RECT rect{0,0,64,64};c->RSSetViewports(1,&viewport);c->RSSetState(raster.Get());c->RSSetScissorRects(1,&rect);
  Dx11DrawReplay::Geometry geometry;if(!geometry.Capture(c.Get()))return 2;
  c->ClearState();geometry.Apply(c.Get());auto raw=rt.Get();c->OMSetRenderTargets(1,&raw,nullptr);c->PSSetShader(pixel.Get(),nullptr,0);float red[]={1,0,0,1};c->ClearRenderTargetView(raw,red);commands[test].Run(c.Get());c->CopyResource(read.Get(),target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};CHECK(c->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped));UINT wrong=0;
  for(UINT y=0;y<64;++y)for(UINT x=0;x<64;++x){auto p=static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch+4*x;if(x<32?(p[0]!=0||p[1]!=255):(p[0]!=255||p[1]!=0))++wrong;}
  c->Unmap(read.Get(),0);printf("case=%u wrong_pixels=%u outside_preserved=%u\n",test,wrong,wrong==0);if(wrong)return 3;
 }
 return 0;
}
