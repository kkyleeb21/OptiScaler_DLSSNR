#include <windows.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <vector>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "../../builds/D18_Wildlands_SRPreview_20260912/core-source/OptiScaler/hooks/D18WildlandsSrBytecode.h"
#pragma comment(lib,"d3d11.lib")
using Microsoft::WRL::ComPtr;
static float half(uint16_t v){const unsigned exp=(v>>10)&31,m=v&1023;float x=exp?std::ldexp(float(1024+m),int(exp)-25):std::ldexp(float(m),-24);return v&32768?-x:x;}
int main(){
 const UINT w=32,h=24;ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;D3D_FEATURE_LEVEL fl=D3D_FEATURE_LEVEL_11_0,got{};
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,&fl,1,D3D11_SDK_VERSION,&d,&got,&c)))return 1;
 ComPtr<ID3D11ComputeShader>cs;if(FAILED(d->CreateComputeShader(numericBytecode,sizeof(numericBytecode),nullptr,&cs)))return 2;
 ComPtr<ID3D11Texture2D>in[3],out[3],read[3];ComPtr<ID3D11ShaderResourceView>srv[3];ComPtr<ID3D11UnorderedAccessView>uav[3];
 std::vector<UINT>colour(w*h),depth(w*h),motion(w*h,0xa8002c00); // half2(0.0625,-0.03125)
 for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){UINT i=y*w+x;colour[i]=256|(512<<10)|(768<<20)|((x%2?3u:0u)<<30);depth[i]=y==0?0:0x3e800000;}
 const DXGI_FORMAT inf[]={DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT},outf[]={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT};
 const void* data[]={colour.data(),depth.data(),motion.data()};
 for(unsigned i=0;i<3;++i){D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;td.Format=inf[i];td.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA init{data[i],w*4,0};
  if(FAILED(d->CreateTexture2D(&td,&init,&in[i]))||FAILED(d->CreateShaderResourceView(in[i].Get(),nullptr,&srv[i])))return 3;
  td.Format=outf[i];td.BindFlags=D3D11_BIND_UNORDERED_ACCESS;if(FAILED(d->CreateTexture2D(&td,nullptr,&out[i]))||FAILED(d->CreateUnorderedAccessView(out[i].Get(),nullptr,&uav[i])))return 4;
  td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;if(FAILED(d->CreateTexture2D(&td,nullptr,&read[i])))return 5;
 }
 float cb[80]{};cb[48]=.25f;cb[49]=-.25f;cb[50]=.25f/w;cb[51]=-.25f/h;cb[52]=1;cb[55]=.02f;cb[57]=1;cb[59]=-.04f;cb[67]=1;
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(cb);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA init{cb,0,0};ComPtr<ID3D11Buffer>b;if(FAILED(d->CreateBuffer(&bd,&init,&b)))return 6;
 ID3D11ShaderResourceView* ss[]={srv[0].Get(),srv[1].Get(),srv[2].Get()};ID3D11UnorderedAccessView* uu[]={uav[0].Get(),uav[1].Get(),uav[2].Get()};auto bb=b.Get();c->CSSetShader(cs.Get(),nullptr,0);c->CSSetConstantBuffers(0,1,&bb);c->CSSetShaderResources(0,3,ss);c->CSSetUnorderedAccessViews(0,3,uu,nullptr);c->Dispatch(4,3,1);c->ClearState();for(unsigned i=0;i<3;++i)c->CopyResource(read[i].Get(),out[i].Get());
 // Blocking Map is confined to this offline test process.
 unsigned checked=0;
 for(unsigned i=0;i<3;++i){D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(c->Map(read[i].Get(),0,D3D11_MAP_READ,0,&m)))return 7;
  for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){auto row=static_cast<unsigned char*>(m.pData)+y*m.RowPitch;float actual=0,expected=0;
   if(i==0){auto p=reinterpret_cast<uint16_t*>(row)+x*4;if(fabs(half(p[0])-256.f/1023)>.0005||fabs(half(p[1])-512.f/1023)>.0005||fabs(half(p[2])-768.f/1023)>.0005||half(p[3])!=1)return 8;}
   if(i==1){memcpy(&actual,row+x*4,4);expected=y==0?0:.25f;if(actual!=expected)return 9;}
   if(i==2){auto p=reinterpret_cast<uint16_t*>(row)+x*2;const bool object=(x%2)&&y>0;float ex=object?1.25f:.32f,ey=object?-.625f:.48f;if(fabs(half(p[0])-ex)>.001||fabs(half(p[1])-ey)>.001){printf("motion mismatch %u %u %.8f %.8f\n",x,y,half(p[0]),half(p[1]));return 10;}}
   ++checked;
  }c->Unmap(read[i].Get(),0);
 }
 ComPtr<ID3D11InfoQueue>info;d.As(&info);for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<char>bytes(size);auto m=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());info->GetMessage(i,m,&size);if(m->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){puts(m->pDescription);return 11;}}
 printf("checked=%u object_camera_depth_colour=passed removed_reason=%08lx\n",checked,d->GetDeviceRemovedReason());return FAILED(d->GetDeviceRemovedReason())?12:0;
}
