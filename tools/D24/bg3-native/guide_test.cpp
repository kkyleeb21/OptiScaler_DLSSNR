#include "D24Native.cpp"
static bool verify(Session& s,ID3D11Texture2D* texture,unsigned channels){
 D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> read;if(FAILED(s.device->CreateTexture2D(&d,nullptr,&read)))return false;
 s.context->CopyResource(read.Get(),texture);D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(s.context->Map(read.Get(),0,D3D11_MAP_READ,0,&m)))return false;
 bool okay=true;for(unsigned y=0;y<d.Height;++y){auto row=reinterpret_cast<float*>(static_cast<unsigned char*>(m.pData)+size_t(y)*m.RowPitch);for(unsigned x=0;x<d.Width;++x){if(row[x*channels]!=0.25f)okay=false;if(channels==2&&row[x*channels+1]!=-0.5f)okay=false;}}
 s.context->Unmap(read.Get(),0);return okay;
}
int main(){Session s;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL level;
 _wfopen_s(&logFile,(directory()/L"guide-capture-test.log").c_str(),L"w");if(!logFile)return 9;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&s.device,&level,&ctx)))return 1;ctx.As(&s.context);
 for(unsigned w:{64u,96u,128u}){
 D3D11_TEXTURE2D_DESC d{w,48,1,1,DXGI_FORMAT_R32G8X24_TYPELESS,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0};
 std::vector<unsigned> z(size_t(w)*48*2);for(size_t i=0;i<z.size();i+=2){z[i]=0x3e800000;z[i+1]=17;}
 D3D11_SUBRESOURCE_DATA zd{z.data(),w*8,0};ComPtr<ID3D11Texture2D> depth,motion;
 if(FAILED(s.device->CreateTexture2D(&d,&zd,&depth)))return 2;
 d.Format=w==64?DXGI_FORMAT_R16G16_TYPELESS:w==96?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R32G32_TYPELESS;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;std::vector<unsigned> mv(size_t(w)*48*(w==128?2:1),0xb8003400);
 if(w==128)for(size_t i=0;i<mv.size();i+=2){mv[i]=0x3e800000;mv[i+1]=0xbf000000;}
 D3D11_SUBRESOURCE_DATA md{mv.data(),w*(w==128?8u:4u),0};
 if(FAILED(s.device->CreateTexture2D(&d,&md,&motion)))return 3;
 for(unsigned frame=0;frame<4;++frame){if(!ownGuide(s,depth.Get(),s.ownedDepth,DXGI_FORMAT_R32_FLOAT)||!ownGuide(s,motion.Get(),s.ownedMotion,DXGI_FORMAT_R32G32_FLOAT))return 4;
 if(!verify(s,s.ownedDepth.Get(),1)||!verify(s,s.ownedMotion.Get(),2))return 5;}
 s.desc.Width=w;s.desc.Height=48;captureExtent=64;
 captureCrop(s,depth.Get(),w,"depth_raw",w,48);captureCrop(s,motion.Get(),w,"motion_raw",w,48);
 captureCrop(s,s.ownedDepth.Get(),w,"depth_owned",w,48);captureCrop(s,s.ownedMotion.Get(),w,"motion_owned",w,48);
 printf("depth19_srv_only_motion_format=%u width=%u signed_values_exact=pass resize=pass\n",unsigned(d.Format),w);
 }fclose(logFile);logFile=nullptr;return 0;
}
