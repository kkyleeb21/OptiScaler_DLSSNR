#ifndef D24_JITTER_PROFILE_OFF_TEST
#define D24_JITTER_TEST_HOST
#endif
#include "D24Native.cpp"
#include <cassert>
static bool readEquals(Session& s,ID3D11Texture2D* t,const std::vector<float>& expected){
 D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> stage;if(FAILED(s.device->CreateTexture2D(&d,nullptr,&stage)))return false;
 s.context->CopyResource(stage.Get(),t);D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(s.context->Map(stage.Get(),0,D3D11_MAP_READ,0,&m)))return false;
 bool okay=true;for(unsigned y=0;y<d.Height;++y){const auto row=reinterpret_cast<const float*>(static_cast<const unsigned char*>(m.pData)+size_t(y)*m.RowPitch);for(unsigned x=0;x<d.Width*2;++x)if(std::abs(row[x]-expected[size_t(y)*d.Width*2+x])>2e-6f)okay=false;}
 s.context->Unmap(stage.Get(),0);return okay;
}
static void policyTest(){
 NrJitterHistory h;NrJitterSample s;s.eligible=true;s.call=1;s.x=.25f;s.y=-.25f;s.sx=2;s.sy=-2;
 auto p=h.plan(s);assert(p.active&&!p.apply&&p.reset);h.commit(s,p);
 s.call=2;s.x=.75f;s.y=.25f;p=h.plan(s);assert(p.apply&&p.dx==.25f&&p.dy==-.25f);h.commit(s,p);
 s.call=4;assert(h.plan(s).reset);s.call=3;s.reset=true;assert(h.plan(s).reset);s.reset=false;
 s.rw=1920;assert(h.plan(s).reset);s.rw=0;s.epoch=2;assert(h.plan(s).reset);s.epoch=0;
 s.eligible=false;p=h.plan(s);assert(!p.active&&p.reset);h.commit(s,p);assert(!h.plan(s).reset);
 s.eligible=true;s.sx=0;assert(!h.plan(s).active);s.sx=NAN;assert(!h.plan(s).active);s.sx=1;s.x=NAN;assert(!h.plan(s).active);
 h.clear();s.x=0;assert(h.plan(s).reset);puts("policy history, scale, missing/invalid inputs: PASS");
}
int main(int argc,char**){policyTest();const bool half=argc>1;
 auto& s=session();ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL fl;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&fl,&ctx)))return 1;
 if(!LoadLibraryW(L"E:\\DLSSNR\\builds\\D24_BG3\\nvngx_dlssnr.dll"))return 2;
 D3D11_TEXTURE2D_DESC d{256,256,1,1,DXGI_FORMAT_R16G16B16A16_FLOAT,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,0,0};
 ComPtr<ID3D11Texture2D> color,depth,motion;device->CreateTexture2D(&d,nullptr,&color);d.Width=d.Height=128;d.Format=DXGI_FORMAT_R32_FLOAT;device->CreateTexture2D(&d,nullptr,&depth);d.Format=DXGI_FORMAT_R32G32_FLOAT;device->CreateTexture2D(&d,nullptr,&motion);
 if(half){motion.Reset();d.Format=DXGI_FORMAT_R16G16_TYPELESS;device->CreateTexture2D(&d,nullptr,&motion);}
 if(!color||!depth||!motion)return 3;
 ComPtr<ID3D11UnorderedAccessView> cv,dv;device->CreateUnorderedAccessView(color.Get(),nullptr,&cv);device->CreateUnorderedAccessView(depth.Get(),nullptr,&dv);if(!cv||!dv)return 4;
 DlssNrNative::Settings settings;settings.capture=1;settings.captureSize=64;settings.diagnostics=1;settings.customFilter=0;settings.useExposure=0;settings.networkRatio=.5f;
 float oldx=0,oldy=0;int owner=0;
 const bool defaultOn=jitterDefault();
 if(D24ConfigureJitter(3)!=0||D24ReadJitterStatus(nullptr)!=0)return 13;
 unsigned stableEpoch=0;
 for(unsigned f=0;f<22;++f){
  const float jx=f%2?.25f:-.25f,jy=f%3?.125f:-.125f,sx=2,sy=-2;
  std::vector<float> raw(128*128*2),real(raw.size());
  for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x){auto i=size_t(y*128+x)*2;real[i]=.125f+float(x)/1024;real[i+1]=-.25f+float(y)/1024;raw[i]=real[i]-(jx-oldx)/sx;raw[i+1]=real[i+1]-(jy-oldy)/sy;}
  oldx=jx;oldy=jy;
  if(half){std::vector<unsigned short> packed(raw.size());for(size_t i=0;i<raw.size();++i){unsigned b=0;memcpy(&b,&raw[i],4);packed[i]=(b&0x7fffffff)?static_cast<unsigned short>(((b>>16)&0x8000)|((((b>>23)&255)-127+15)<<10)|((b>>13)&1023)):static_cast<unsigned short>(b>>16);}ctx->UpdateSubresource(motion.Get(),0,nullptr,packed.data(),128*4,0);}
  else ctx->UpdateSubresource(motion.Get(),0,nullptr,raw.data(),128*8,0);
  float c[4]={.3f,.4f,.5f,1},z[4]={.5f};ctx->ClearUnorderedAccessViewFloat(cv.Get(),c);ctx->ClearUnorderedAccessViewFloat(dv.Get(),z);
  ProbeParameters game;game.Set("Output",static_cast<ID3D11Resource*>(color.Get()));game.Set("Depth",static_cast<ID3D11Resource*>(depth.Get()));game.Set("MotionVectors",static_cast<ID3D11Resource*>(motion.Get()));
  if(f!=13)game.Set("DLSS.Feature.Create.Flags",f==10||f==11?11u:15u);
  game.Set("MV.Scale.X",sx);game.Set("MV.Scale.Y",sy);game.Set("Jitter.Offset.X",jx);game.Set("Jitter.Offset.Y",jy);game.Set("Reset",f==6?1u:0u);
  settings.mode=f==7?0u:2u;if(f==9)settings.networkRatio=1;D24Configure(&settings);
  if(f>=16){if(!D24ConfigureJitter(f==16||f==17||f==20?1u:2u))return 14;}
  const int result=D24Process(&owner,ctx.Get(),&game,11);
  if(f==7){if(result!=0)return 5;continue;}if(result!=1)return 6;
  const bool active=f>=16?(f==18||f==19||f==21):(defaultOn&&f!=10&&f!=11&&f!=13);
  if(s.jitterPlan.active!=active)return 7;
  if(!half&&!readEquals(s,motion.Get(),raw))return 8;
  if(!readEquals(s,s.ownedMotion.Get(),raw))return 8;
  ID3D11Resource* nr=nullptr;s.parameters.Get("DLSSNR.MVec",&nr);
  if(active){if(nr!=s.nrMotion.Get()||!readEquals(s,s.nrMotion.Get(),s.jitterPlan.apply?real:raw))return 9;}
  else if(nr!=s.ownedMotion.Get())return 10;
  unsigned reset=0;s.parameters.Get("DLSSNR.Reset",&reset);
  if((f==0||f==6||f==8||f==9||(defaultOn&&(f==10||f==12||f==13||f==14||f==16))||f==18||f==20||f==21)&&reset!=1)return 11;
  if((f==17||f==19)&&reset!=0)return 15;
  if(f==15)stableEpoch=s.epoch;if(f>=16&&s.epoch!=stableEpoch)return 16;
  DlssNrNative::JitterStatus status;if(!D24ReadJitterStatus(&status))return 17;
  using R=DlssNrNative::JitterReason;
  if(active&&status.reason!=(s.jitterPlan.apply?R::Active:R::HistoryReset))return 18;
  if(f==16||f==17||f==20){if(status.reason!=R::Off||status.requested)return 19;}
  if(defaultOn&&f==10&&status.reason!=R::NotJittered)return 20;
  if(defaultOn&&f==13&&status.reason!=R::MissingFlags)return 21;
  printf("frame=%u active=%u apply=%u reset=%u original_and_compose_unchanged=pass model_mv=pass\n",f,unsigned(active),unsigned(s.jitterPlan.apply),reset);
 }
 return D24Release(&owner)==1?0:12;
}
