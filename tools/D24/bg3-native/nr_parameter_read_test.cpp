// Standalone host only. Audit actual feature parameter reads without changing game DLLs.
#define D24_PARAMETER_READ_AUDIT
#include "D24Native.cpp"
int main(int argc,char**){
 auto& s=session();ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL level;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&ctx)))return 1;
 if(!LoadLibraryW(L"E:\\DLSSNR\\builds\\D24_BG3\\nvngx_dlssnr.dll"))return 2;
 DlssNrNative::Settings settings;settings.mode=2;settings.capture=0;settings.diagnostics=0;
 settings.networkRatio=argc>1?.5f:1.f;settings.customFilter=0;settings.useExposure=0;
 if(!D24Configure(&settings))return 3;
 D3D11_TEXTURE2D_DESC td{256,256,1,1,DXGI_FORMAT_R16G16B16A16_FLOAT,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,0,0};
 ComPtr<ID3D11Texture2D> color,depth,motion;
 if(FAILED(device->CreateTexture2D(&td,nullptr,&color)))return 4;
 td.Width=td.Height=128;td.Format=DXGI_FORMAT_R32_FLOAT;if(FAILED(device->CreateTexture2D(&td,nullptr,&depth)))return 5;
 td.Format=DXGI_FORMAT_R32G32_FLOAT;if(FAILED(device->CreateTexture2D(&td,nullptr,&motion)))return 6;
 ComPtr<ID3D11UnorderedAccessView> cv,dv,mv;
 if(FAILED(device->CreateUnorderedAccessView(color.Get(),nullptr,&cv))||FAILED(device->CreateUnorderedAccessView(depth.Get(),nullptr,&dv))||FAILED(device->CreateUnorderedAccessView(motion.Get(),nullptr,&mv)))return 7;
 ProbeParameters game;game.Set("Output",static_cast<ID3D11Resource*>(color.Get()));game.Set("Depth",static_cast<ID3D11Resource*>(depth.Get()));game.Set("MotionVectors",static_cast<ID3D11Resource*>(motion.Get()));
 game.Set("MV.Scale.X",1.f);game.Set("MV.Scale.Y",1.f);game.Set("DLSS.Render.Subrect.Dimensions.Width",128u);game.Set("DLSS.Render.Subrect.Dimensions.Height",128u);
 // Supply generic jitter and init flags too: observing reads distinguishes a
 // native parameter path from a caller merely storing keys that are never consumed.
 s.parameters.Set("Jitter.Offset.X",.25f);s.parameters.Set("Jitter.Offset.Y",-.25f);s.parameters.Set("DLSS.Feature.Create.Flags",15u);
 int owner=0;
 for(unsigned frame=0;frame<4;++frame){
  float c[4]={.3f,.4f,.5f,1},d[4]={.5f},m[4]={.25f,-.25f};ctx->ClearUnorderedAccessViewFloat(cv.Get(),c);ctx->ClearUnorderedAccessViewFloat(dv.Get(),d);ctx->ClearUnorderedAccessViewFloat(mv.Get(),m);
  game.Set("Reset",frame==0?1u:0u);game.Set("Jitter.Offset.X",frame%2?.25f:-.25f);game.Set("Jitter.Offset.Y",frame%2?-.25f:.25f);
  s.parameters.readKeys.clear();const int result=D24Process(&owner,ctx.Get(),&game,15u);if(result!=1)return 8;
  printf("{\"frame\":%u,\"result\":%d,\"phase\":\"%s\",\"read_keys\":[",frame,result,frame?"evaluate":"create_and_evaluate");bool first=true;
  for(const auto& key:s.parameters.readKeys){printf("%s\"%s\"",first?"":",",key.c_str());first=false;}puts("]}");
 }
 return D24Release(&owner)==1?0:9;
}
