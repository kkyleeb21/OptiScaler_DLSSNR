#include <dlssnr/Dx11NativeLayer.h>
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
static unsigned Pack(float r,float g,float b,float a,bool ten){unsigned m=ten?1023:255,am=ten?3:255,shift=ten?10:8;return unsigned(std::lround(r*m))|(unsigned(std::lround(g*m))<<shift)|(unsigned(std::lround(b*m))<<(2*shift))|(unsigned(std::lround(a*am))<<(3*shift));}
static float Channel(unsigned p,unsigned i,bool ten){unsigned shift=ten?10:8,m=i==3&&ten?3:ten?1023:255;return float((p>>(shift*i))&m)/m;}
int main(){
 for(auto level:{D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_11_1})for(bool ten:{false,true}){
  ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL actual{};
  if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&d,&actual,&c)))return 2;
  constexpr unsigned w=5,h=3,ow=9,oh=7;
  const unsigned base=Pack(.25f,.5f,.75f,0,ten),distinct=Pack(.8f,.2f,.1f,0,ten),patch=Pack(.1f,.9f,.3f,1,ten);
  std::vector<unsigned> low(w*h,base),high(ow*oh,distinct),after=low;
  auto texture=[&](unsigned width,unsigned height,unsigned bind,const void* data,ComPtr<ID3D11Texture2D>& out){D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=ten?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=bind;D3D11_SUBRESOURCE_DATA init{data,width*4,0};return d->CreateTexture2D(&td,data?&init:nullptr,&out);};
  ComPtr<ID3D11Texture2D> native,output;ComPtr<ID3D11ShaderResourceView> nv,hv;
  if(FAILED(texture(w,h,D3D11_BIND_SHADER_RESOURCE,low.data(),native))||FAILED(texture(ow,oh,D3D11_BIND_SHADER_RESOURCE,high.data(),output))||FAILED(d->CreateShaderResourceView(native.Get(),nullptr,&nv))||FAILED(d->CreateShaderResourceView(output.Get(),nullptr,&hv)))return 3;
  DlssNr::Dx11NativeLayer layer;if(FAILED(layer.Prepare(d.Get(),native.Get(),output.Get())))return 4;
  D3D11_TEXTURE2D_DESC td{};output->GetDesc(&td);td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;if(FAILED(d->CreateTexture2D(&td,nullptr,&read)))return 5;
  unsigned untouched=0,changed=0;
  for(unsigned step=0;step<2;++step){
   c->UpdateSubresource(native.Get(),0,nullptr,low.data(),w*4,0);c->UpdateSubresource(output.Get(),0,nullptr,high.data(),ow*4,0);layer.Snapshot(c.Get(),native.Get());
   if(step){after[0]=patch;after[w+2]=patch;c->UpdateSubresource(native.Get(),0,nullptr,after.data(),w*4,0);}
   layer.Merge(c.Get(),nv.Get(),hv.Get(),output.Get());c->CopyResource(read.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&map)))return 6;
   for(unsigned y=0;y<oh;++y)for(unsigned x=0;x<ow;++x){
    float px=(float(x)+.5f)*w/ow-.5f,py=(float(y)+.5f)*h/oh-.5f;int ix=int(std::floor(px)),iy=int(std::floor(py));float fx=px-ix,fy=py-iy,expected[4]{};bool mask=false;
    for(int j=0;j<2;++j)for(int i=0;i<2;++i){unsigned k=unsigned(std::clamp(iy+j,0,int(h)-1))*w+unsigned(std::clamp(ix+i,0,int(w)-1));float weight=(i?fx:1-fx)*(j?fy:1-fy);mask|=weight>0&&low[k]!=after[k];for(unsigned ch=0;ch<4;++ch)expected[ch]+=weight*Channel(after[k],ch,ten);}
    const auto got=reinterpret_cast<const unsigned*>(static_cast<const char*>(map.pData)+y*map.RowPitch)[x];
    if(!mask){if(got!=distinct)return 7;++untouched;}else{++changed;for(unsigned ch=0;ch<4;++ch){float tolerance=1.1f/(ch==3&&ten?3:ten?1023:255);if(std::abs(Channel(got,ch,ten)-expected[ch])>tolerance)return 8;}}
   }c->Unmap(read.Get(),0);
  }
  if(layer.Prepare(d.Get(),output.Get(),native.Get())!=E_INVALIDARG||!changed||!untouched||FAILED(d->GetDeviceRemovedReason()))return 9;
  printf("PASS level=%x format=%s unchanged=%u native_region=%u edges=1 no_change=1 invalid_contract=1\n",level,ten?"R10":"RGBA8",untouched,changed);
 }
 return 0;
}
