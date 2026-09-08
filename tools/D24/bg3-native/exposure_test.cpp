#include "D24Native.cpp"
#include <limits>
static bool finish(Session& s){
 ComPtr<ID3D11Query> query;D3D11_QUERY_DESC d{D3D11_QUERY_EVENT,0};
 if(FAILED(s.device->CreateQuery(&d,&query)))return false;
 s.context->End(query.Get());s.context->Flush();const auto end=GetTickCount64()+2000;
 while(GetTickCount64()<end){BOOL done=FALSE;HRESULT hr=s.context->GetData(query.Get(),&done,sizeof(done),0);if(hr==S_OK)return done!=FALSE;if(FAILED(hr))return false;Sleep(0);}return false;
}
int main(){
 Session s;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL level;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&s.device,&level,&ctx)))return 1;ctx.As(&s.context);
 auto check=[&](unsigned width,DXGI_FORMAT format,float a,float b,bool profile,float expected){
  float values[2]={a,b};D3D11_TEXTURE2D_DESC d{width,1,1,1,format,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0};D3D11_SUBRESOURCE_DATA init{values,width*4,0};
  ComPtr<ID3D11Texture2D> tex;if(FAILED(s.device->CreateTexture2D(&d,&init,&tex)))return false;
  ProbeParameters p;p.Set("ExposureTexture",static_cast<ID3D11Resource*>(tex.Get()));p.Set("DLSS.Pre.Exposure",1.0f);
  sampleExposure(s,&p,profile);if(!finish(s))return false;sampleExposure(s,&p,profile);if(!finish(s))return false;
  if(std::abs(s.exposure-expected)>1e-7f)return false;
  printf("width=%u format=%u profile=%u gain=%.9g pass\n",width,unsigned(format),unsigned(profile),s.exposure);return true;
 };
 if(!check(2,DXGI_FORMAT_R32_TYPELESS,147.033187866f,0.00680118566f,true,0.00680118566f))return 2;
 if(!check(1,DXGI_FORMAT_R32_FLOAT,4,0,false,4))return 3;
 if(!check(2,DXGI_FORMAT_R32_TYPELESS,147.033187866f,0.00680118566f,false,0))return 4;
 if(!check(2,DXGI_FORMAT_R32_TYPELESS,147,0.25f,true,0))return 5;
 if(!check(2,DXGI_FORMAT_R32_TYPELESS,147,std::numeric_limits<float>::quiet_NaN(),true,0))return 6;
 if(!check(2,DXGI_FORMAT_R32_FLOAT,100,0.01f,true,0.01f))return 7;
 ProbeParameters missing;sampleExposure(s,&missing,true);if(s.exposure!=0)return 8;
 puts("exposure copy/readback, layout changes, invalid/missing input and legacy path: PASS");return 0;
}
