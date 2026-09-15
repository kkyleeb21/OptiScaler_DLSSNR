#include <dlssnr/Dx11SameSizeColourCopy.h>
#include <vector>
#include <cstdio>
#pragma comment(lib,"d3d11.lib")
using Microsoft::WRL::ComPtr;
int main(){
 for(auto level:{D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_11_1})for(bool ten:{false,true}){
  ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL actual{};
  if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&d,&actual,&c)))return 1;
  constexpr UINT w=13,h=7;std::vector<UINT> pixels(w*h);for(UINT i=0;i<w*h;++i)pixels[i]=0x10203040u+i*1234567u;
  D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=ten?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
  D3D11_SUBRESOURCE_DATA init{pixels.data(),w*4,0};ComPtr<ID3D11Texture2D> source,target,wrong;
  if(FAILED(d->CreateTexture2D(&td,&init,&source))||FAILED(d->CreateTexture2D(&td,nullptr,&target)))return 2;
  if(!DlssNr::SameSizeColourCopyValid(c.Get(),source.Get(),target.Get(),w,h)||DlssNr::SameSizeColourCopyValid(c.Get(),source.Get(),source.Get(),w,h)||DlssNr::SameSizeColourCopyValid(c.Get(),source.Get(),target.Get(),w+1,h))return 3;
  auto different=td;different.Width++;if(FAILED(d->CreateTexture2D(&different,nullptr,&wrong)))return 4;
  if(DlssNr::SameSizeColourCopyValid(c.Get(),source.Get(),wrong.Get(),w,h))return 5;
  different=td;different.Format=DXGI_FORMAT_D32_FLOAT;different.BindFlags=D3D11_BIND_DEPTH_STENCIL;wrong.Reset();if(FAILED(d->CreateTexture2D(&different,nullptr,&wrong)))return 6;
  if(DlssNr::SameSizeColourCopyValid(c.Get(),source.Get(),wrong.Get(),w,h))return 7;
  ComPtr<ID3D11DeviceContext> deferred;if(FAILED(d->CreateDeferredContext(0,&deferred))||DlssNr::SameSizeColourCopyValid(deferred.Get(),source.Get(),target.Get(),w,h))return 8;
  c->CopyResource(target.Get(),source.Get());td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;if(FAILED(d->CreateTexture2D(&td,nullptr,&read)))return 9;
  c->CopyResource(read.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&map)))return 10;
  for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x)if(reinterpret_cast<UINT*>(static_cast<unsigned char*>(map.pData)+y*map.RowPitch)[x]!=pixels[y*w+x])return 11;
  c->Unmap(read.Get(),0);if(FAILED(d->GetDeviceRemovedReason()))return 12;
  printf("PASS level=%x format=%s exact_rgba=1 size_alias_depth_deferred_rejected=1\n",level,ten?"R10":"RGBA8");
 }
}
