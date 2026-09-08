#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>
#include <cmath>
#include "dx11_probe_parameters.h"
#include <dlssnr/NativeControlAbi.h>
#pragma comment(lib,"d3d11.lib")
using Microsoft::WRL::ComPtr;
int main(int argc,char**){
 ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;D3D_FEATURE_LEVEL fl;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&fl,&c)))return 2;
 if(!LoadLibraryW(L"E:\\DLSSNR\\builds\\D24_BG3\\nvngx_dlssnr.dll"))return 21;
 auto m=LoadLibraryW(L"E:\\DLSSNR\\builds\\D24_BG3\\D24Native.dll");if(!m)return 3;
 auto run=(int(*)(void*,ID3D11DeviceContext*,NVSDK_NGX_Parameter*,unsigned))GetProcAddress(m,"D24Process");
 auto configure=(int(*)(const DlssNrNative::Settings*))GetProcAddress(m,"D24Configure");
 auto status=(int(*)(DlssNrNative::Status*))GetProcAddress(m,"D24ReadStatus");if(!configure||!status)return 40;
 DlssNrNative::Settings settings;settings.diagnostics=1;settings.capture=1;settings.captureSize=64;
 auto enable=[&](int value){settings.mode=value?2u:0u;if(!configure(&settings))throw 41;};
 auto release=(int(*)(void*))GetProcAddress(m,"D24Release");
 int owner=0;enable(1);
 for(unsigned test=0;test<(argc>1?1u:6u);++test){
 unsigned w=test==5?3840:(test>=3?1920:(test==2?320:256)),h=test==5?2160:(test>=3?1080:w);DXGI_FORMAT fmt=test>=4?DXGI_FORMAT_R11G11B10_FLOAT:(test==0?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT);
 D3D11_TEXTURE2D_DESC td{w,h,1,1,fmt,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,0,0};
 ComPtr<ID3D11Texture2D>o,z,v; if(FAILED(d->CreateTexture2D(&td,nullptr,&o)))return 4;
 td.Width=w/2;td.Height=h/2;td.Format=test>=4?DXGI_FORMAT_R24G8_TYPELESS:DXGI_FORMAT_R32_FLOAT;td.BindFlags=test>=4?(D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_DEPTH_STENCIL):(D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);if(FAILED(d->CreateTexture2D(&td,nullptr,&z)))return 5;
 td.Format=test>=4?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R32G32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;if(FAILED(d->CreateTexture2D(&td,nullptr,&v)))return 6;
 ComPtr<ID3D11UnorderedAccessView>ou,zu,vu;d->CreateUnorderedAccessView(o.Get(),nullptr,&ou);d->CreateUnorderedAccessView(z.Get(),nullptr,&zu);d->CreateUnorderedAccessView(v.Get(),nullptr,&vu);
 float color[4]={0.3f,0.4f,0.5f,1},depth[4]={0.5f},zero[4]={};if(test>=4){D3D11_DEPTH_STENCIL_VIEW_DESC ds{};ds.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;ds.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;ComPtr<ID3D11DepthStencilView> view;if(FAILED(d->CreateDepthStencilView(z.Get(),&ds,&view)))return 19;c->ClearDepthStencilView(view.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,0.5f,0);}else c->ClearUnorderedAccessViewFloat(zu.Get(),depth);c->ClearUnorderedAccessViewFloat(vu.Get(),zero);
 ProbeParameters p;p.Set("Output",static_cast<ID3D11Resource*>(o.Get()));p.Set("Depth",static_cast<ID3D11Resource*>(z.Get()));p.Set("MotionVectors",static_cast<ID3D11Resource*>(v.Get()));
 p.Set("DLSS.Feature.Create.Flags",111u);p.Set("Jitter.Offset.X",.25f);p.Set("Jitter.Offset.Y",-.25f);
 D3D11_TEXTURE2D_DESC ed{};ed.Width=ed.Height=ed.MipLevels=ed.ArraySize=ed.SampleDesc.Count=1;ed.Format=DXGI_FORMAT_R32_FLOAT;ed.Usage=D3D11_USAGE_DEFAULT;ed.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 float exposureValue=4;D3D11_SUBRESOURCE_DATA ei{&exposureValue,4,4};ComPtr<ID3D11Texture2D> exposure;
 if(FAILED(d->CreateTexture2D(&ed,&ei,&exposure)))return 44;
 p.Set("ExposureTexture",static_cast<ID3D11Resource*>(exposure.Get()));p.Set("DLSS.Pre.Exposure",2.0f);
 D3D11_VIEWPORT vp{3,4,123,117,0.1f,0.9f};c->RSSetViewports(1,&vp);
 unsigned packedInput=0;
 if(test>=4){c->ClearUnorderedAccessViewFloat(ou.Get(),color);D3D11_TEXTURE2D_DESC one{};o->GetDesc(&one);one.Width=one.Height=1;one.Usage=D3D11_USAGE_STAGING;one.BindFlags=0;one.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> pixel;if(FAILED(d->CreateTexture2D(&one,nullptr,&pixel)))return 22;D3D11_BOX box{0,0,0,1,1,1};c->CopySubresourceRegion(pixel.Get(),0,0,0,0,o.Get(),0,&box);D3D11_MAPPED_SUBRESOURCE pm{};if(FAILED(c->Map(pixel.Get(),0,D3D11_MAP_READ,0,&pm)))return 23;packedInput=*static_cast<unsigned*>(pm.pData);c->Unmap(pixel.Get(),0);}
 for(int f=0;f<(argc>1?360:3);++f){if(argc>1)Sleep(16);settings.capture=f>0;settings.localStructure=f==1?0.8f:1.0f;
 settings.networkRatio=f==1?0.5f:1.0f;settings.customFilter=f==1;settings.linearResolve=f==1;
 settings.linearColorInput=f==2;settings.preset=f==2?0u:1u;settings.compare=f==1?2u:0u;settings.compareSplit=0.35f;
 settings.useExposure=f==2;
 if(!configure(&settings))return 43;c->ClearUnorderedAccessViewFloat(ou.Get(),color);int r=run(&owner,c.Get(),&p,NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|(test>=4?NVSDK_NGX_DLSS_Feature_Flags_IsHDR:0));printf("test=%u frame=%d result=%d\n",test,f,r);fflush(stdout);if(r!=1)return 10;
 DlssNrNative::Status live;if(!status(&live)||live.result!=1||live.mode!=2||live.width!=w||live.height!=h)return 42;
 if(f>0 && (live.exposure!=4.0f||live.preExposure!=2.0f))return 45;
 UINT n=1;D3D11_VIEWPORT got{};c->RSGetViewports(&n,&got);if(n!=1||memcmp(&vp,&got,sizeof(vp)))return 11;
 }
 D3D11_TEXTURE2D_DESC read{};o->GetDesc(&read);read.Usage=D3D11_USAGE_STAGING;read.BindFlags=0;read.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> staging;if(FAILED(d->CreateTexture2D(&read,nullptr,&staging)))return 14;c->CopyResource(staging.Get(),o.Get());
 D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))return 15;
 unsigned nonzero=0,changed=0;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w*(test>=4?1:4);++x){auto row=static_cast<unsigned char*>(map.pData)+y*map.RowPitch;
 if(test>=4){unsigned a=reinterpret_cast<unsigned*>(row)[x];if(((a>>6)&31)==31||((a>>17)&31)==31||((a>>27)&31)==31)return 20;if(a)++nonzero;if(a!=packedInput)++changed;}else if(test==0){float a=reinterpret_cast<float*>(row)[x];if(!std::isfinite(a))return 16;if(a!=0)++nonzero;}
 else{unsigned short a=reinterpret_cast<unsigned short*>(row)[x];if((a&0x7c00)==0x7c00)return 17;if(a&0x7fff)++nonzero;}}
 c->Unmap(staging.Get(),0);if(!nonzero)return 18;printf("pixels_finite_nonzero=%u\n",nonzero);
 if(test>=4){printf("changed_pixels=%u\n",changed);if(!changed)return 24;}
 settings.mode=1;configure(&settings);if(run(&owner,c.Get(),&p,0)!=1)return 43;DlssNrNative::Status converted;status(&converted);if(converted.mode!=1)return 44;
 enable(0);if(run(&owner,c.Get(),&p,0)!=0)return 12;enable(1);
 }
 int result=release(&owner);printf("release=%d state_restore=pass toggle=pass\n",result);return result==1?0:13;
}






