#include "D24Native.cpp"
static std::vector<unsigned char> readback(Session& s,ID3D11Texture2D* texture,unsigned bytes){D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;if(FAILED(s.device->CreateTexture2D(&td,nullptr,&staging)))throw 1;s.context->CopyResource(staging.Get(),texture);D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(s.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))throw 2;std::vector<unsigned char> data(size_t(td.Width)*td.Height*bytes);for(unsigned y=0;y<td.Height;y++)memcpy(data.data()+size_t(y)*td.Width*bytes,static_cast<unsigned char*>(map.pData)+size_t(y)*map.RowPitch,size_t(td.Width)*bytes);s.context->Unmap(staging.Get(),0);return data;}
int main(){Session s;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL fl;if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&s.device,&fl,&ctx)))return 1;ctx.As(&s.context);
 for(unsigned w:{256u,1920u,3840u}){unsigned h=w==256?257:w*9/16;
 D3D11_TEXTURE2D_DESC td{w,h,1,1,DXGI_FORMAT_R11G11B10_FLOAT,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,0,0};
 std::vector<unsigned> packed(size_t(w)*h);unsigned seed=1;for(auto& p:packed){seed=seed*1664525u+1013904223u;unsigned r=seed%1984;seed=seed*1664525u+1013904223u;unsigned g=seed%1984;seed=seed*1664525u+1013904223u;unsigned b=seed%992;p=r|(g<<11)|(b<<22);}
 D3D11_SUBRESOURCE_DATA data{packed.data(),w*4,0};ComPtr<ID3D11Texture2D> original,half,back;if(FAILED(s.device->CreateTexture2D(&td,&data,&original)))return 2;if(FAILED(s.device->CreateTexture2D(&td,nullptr,&back)))return 3;td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;if(FAILED(s.device->CreateTexture2D(&td,nullptr,&half)))return 4;
 if(!convert(s,original.Get(),half.Get())||!convert(s,half.Get(),back.Get()))return 5;auto got=readback(s,back.Get(),4);unsigned mismatch=0;for(size_t i=0;i<packed.size();i++)if(packed[i]!=reinterpret_cast<unsigned*>(got.data())[i])mismatch++;
 printf("roundtrip %ux%u finite_R11_random_patterns mismatches=%u/%zu\n",w,h,mismatch,packed.size());if(mismatch)return 6;
 }
 s.enabled=true;int owner=0;const unsigned w=512,h=256;
 D3D11_TEXTURE2D_DESC td{w,h,1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,0,0};
 ComPtr<ID3D11Texture2D> source,output,depth,motion;s.device->CreateTexture2D(&td,nullptr,&source);td.Format=DXGI_FORMAT_R11G11B10_FLOAT;s.device->CreateTexture2D(&td,nullptr,&output);
 td.Width=w/2;td.Height=h/2;td.Format=DXGI_FORMAT_R24G8_TYPELESS;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_DEPTH_STENCIL;s.device->CreateTexture2D(&td,nullptr,&depth);
 D3D11_DEPTH_STENCIL_VIEW_DESC ds{};ds.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;ds.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;ComPtr<ID3D11DepthStencilView> dv;s.device->CreateDepthStencilView(depth.Get(),&ds,&dv);s.context->ClearDepthStencilView(dv.Get(),D3D11_CLEAR_DEPTH,0.5f,0);
 td.Format=DXGI_FORMAT_R16G16_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;s.device->CreateTexture2D(&td,nullptr,&motion);ComPtr<ID3D11UnorderedAccessView> mv;s.device->CreateUnorderedAccessView(motion.Get(),nullptr,&mv);float zero[4]{};s.context->ClearUnorderedAccessViewFloat(mv.Get(),zero);
 ProbeParameters p;p.Set("Output",static_cast<ID3D11Resource*>(output.Get()));p.Set("Depth",static_cast<ID3D11Resource*>(depth.Get()));p.Set("MotionVectors",static_cast<ID3D11Resource*>(motion.Get()));
 for(float peak:{0.75f,8.0f,64.0f}){
 std::vector<float> pattern(size_t(w)*h*4);for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){size_t i=(size_t(y)*w+x)*4;pattern[i]=peak*float(x%73)/72;pattern[i+1]=peak*float(y%47)/46;pattern[i+2]=((x/16+y/16)%2)?peak:0.01f;pattern[i+3]=1;}
 s.context->UpdateSubresource(source.Get(),0,nullptr,pattern.data(),w*16,0);
 s.mode=1;if(!convert(s,source.Get(),output.Get()))return 20;auto before=readback(s,output.Get(),4);if(process(s,&owner,s.context.Get(),&p,NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)!=1)return 21;auto after=readback(s,output.Get(),4);if(before!=after)return 22;s.mode=2;s.frames=0;printf("pure_conversion_mode peak=%.2f exact=pass\n",peak);
 std::vector<std::vector<unsigned char>> first(30);unsigned nonfinite=0,negative=0;size_t different=0;
 for(unsigned frame=0;frame<60;frame++){if(!convert(s,source.Get(),output.Get()))return 10;p.Set("Reset",frame%30==0?1u:0u);if(process(s,&owner,s.context.Get(),&p,NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|(peak>1?NVSDK_NGX_DLSS_Feature_Flags_IsHDR:0))!=1)return 11;auto data=readback(s,s.output.Get(),8);auto vals=reinterpret_cast<unsigned short*>(data.data());for(size_t i=0;i<data.size()/2;i++){if((vals[i]&0x7c00)==0x7c00)nonfinite++;if((vals[i]&0x8000)&&(vals[i]&0x7fff))negative++;}if(frame<30)first[frame]=data;else for(size_t i=0;i<data.size();i++)different+=data[i]!=first[frame-30][i];}
 printf("model spatial_peak=%.2f frames=60 nonfinite=%u negative=%u reset_replay_byte_mismatch=%zu\n",peak,nonfinite,negative,different);fflush(stdout);
 }
 releaseFeature(s);
 return 0;
}



