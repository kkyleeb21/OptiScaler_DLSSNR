// Bounded real-GPU feature isolation fixture. Include candidate production code.
#define D18_NATIVE_ADVANCED_TEST 1
#include "D24Native.cpp"
#include <d3d11sdklayers.h>
static DXGI_FORMAT hostFormat=DXGI_FORMAT_R32G32B32A32_FLOAT;
static bool expectAllocationFailure=false;

static bool texture(Session& s, unsigned w,unsigned h,DXGI_FORMAT fmt,ComPtr<ID3D11Texture2D>& out){
    D3D11_TEXTURE2D_DESC d{w,h,1,1,fmt,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,0,0};
    return SUCCEEDED(s.device->CreateTexture2D(&d,nullptr,&out));
}
static bool frame(Session& s,unsigned index,unsigned lane,FILE* pixels){
    const unsigned w=lane?320:256,h=192;
    ComPtr<ID3D11Texture2D> color,depth,motion;
    if(!texture(s,w,h,hostFormat,color)||!texture(s,w/2,h/2,DXGI_FORMAT_R32_FLOAT,depth)||!texture(s,w/2,h/2,DXGI_FORMAT_R32G32_FLOAT,motion))return false;
    std::vector<float> values(w*h*4),z(w*h/4,.5f),mv(w*h/2,0);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto i=(y*w+x)*4;float a=float((x+index*3)%w)/w,b=float(y)/h;
        values[i]=.08f+.45f*(lane?1-a:a);values[i+1]=.1f+.4f*b;values[i+2]=.15f+.25f*float((x/16+y/16+index)%2);values[i+3]=1;}
    if(hostFormat==DXGI_FORMAT_R32G32B32A32_FLOAT)s.context->UpdateSubresource(color.Get(),0,nullptr,values.data(),w*16,0);
    else{ComPtr<ID3D11Texture2D> upload;if(!texture(s,w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,upload))return false;
        s.context->UpdateSubresource(upload.Get(),0,nullptr,values.data(),w*16,0);if(!convert(s,upload.Get(),color.Get()))return false;}
    s.context->UpdateSubresource(depth.Get(),0,nullptr,z.data(),w/2*4,0);
    s.context->UpdateSubresource(motion.Get(),0,nullptr,mv.data(),w/2*8,0);
    ProbeParameters p;p.Set("Output",static_cast<ID3D11Resource*>(color.Get()));p.Set("Depth",static_cast<ID3D11Resource*>(depth.Get()));p.Set("MotionVectors",static_cast<ID3D11Resource*>(motion.Get()));
    p.Set("DLSS.Render.Subrect.Dimensions.Width",w/2);p.Set("DLSS.Render.Subrect.Dimensions.Height",h/2);
    p.Set("MV.Scale.X",1.f);p.Set("MV.Scale.Y",1.f);
    ++s.calls;
    int result=protectedProcess(s,&s,s.context.Get(),&p,NVSDK_NGX_DLSS_Feature_Flags_MVLowRes);
    printf("{\"event\":\"instance_frame\",\"lane\":%u,\"frame\":%u,\"result\":%d,\"resident\":%zu,\"va\":%zu}\n",lane,index,result,s.resources.resident.size(),s.resources.va.size());fflush(stdout);
    if(expectAllocationFailure?(result!=-26):(result!=1))return false;
    if(hostFormat!=DXGI_FORMAT_R32G32B32A32_FLOAT){ComPtr<ID3D11Texture2D> decoded;if(!texture(s,w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,decoded)||!convert(s,color.Get(),decoded.Get()))return false;color=decoded;}
    D3D11_TEXTURE2D_DESC rd{};color->GetDesc(&rd);rd.Usage=D3D11_USAGE_STAGING;rd.BindFlags=0;rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> read;if(FAILED(s.device->CreateTexture2D(&rd,nullptr,&read)))return false;
    s.context->CopyResource(read.Get(),color.Get());D3D11_MAPPED_SUBRESOURCE m{};
    if(FAILED(s.context->Map(read.Get(),0,D3D11_MAP_READ,0,&m)))return false;
    bool finite=true;double response=0;
    for(unsigned y=0;y<h;++y){auto row=(float*)((unsigned char*)m.pData+y*m.RowPitch);for(unsigned x=0;x<w*4;++x){finite&=std::isfinite(row[x]);response+=std::abs(row[x]-values[y*w*4+x]);}if(pixels)fwrite(row,sizeof(float),w*4,pixels);}
    s.context->Unmap(read.Get(),0);return finite&&(expectAllocationFailure?response==0:response>.001);
}
int main(int argc,char** argv){
    bool dual=argc>1&&strcmp(argv[1],"dual")==0;
    bool chain=argc>1&&strcmp(argv[1],"chain")==0;
    bool failure=argc>1&&strcmp(argv[1],"failure")==0;
    if(argc>2)hostFormat=DXGI_FORMAT(atoi(argv[2]));
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL got;
    D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,levels,1,D3D11_SDK_VERSION,&device,&got,&context)))return 2;
    if(!nativeColourSupport(device.Get(),hostFormat)){printf("unsupported_device_format=%u\n",unsigned(hostFormat));return 77;}
    if(!LoadLibraryW((directory()/L"nvngx_dlssnr.dll").c_str()))return 3;
    logFile=_wfsopen((directory()/L"D24Native.log").c_str(),L"wb",_SH_DENYNO);
    managed=true;control.mode=2;control.networkRatio=.5f;control.diagnostics=1;modelDirty=false;
    Session a,b;a.enabled=b.enabled=true;a.device=b.device=device;context.As(&a.context);context.As(&b.context);
    FILE* out=nullptr;fopen_s(&out,chain?"chain.f32":dual?"dual.f32":"single.f32","wb");if(!out)return 4;
    for(unsigned f=0;f<(failure?8u:chain?32u:16u);++f){
        if(failure){DlssNrNative::AdvancedSettings settings;settings.count=f==1||f==2||f>=5?2:1;settings.highResolution=f==3;
            if(!D24ConfigureAdvanced(&settings))return 32;
            expectAllocationFailure=f==1||f==3;failAdvancedAllocation=expectAllocationFailure?2:-1;
            if(f==2)advancedDirty=true; // explicit retry after injected allocation failure
        }
        if(chain){
            unsigned phase=f/4;DlssNrNative::AdvancedSettings settings;
            settings.count=phase==0?2:phase==1?3:phase==2||phase==3?4:phase==7?2:1;
            settings.shared=phase==3;settings.highResolution=phase==4||phase==5;settings.scale=phase==5?1.5f:1.25f;
            for(unsigned i=0;i<4;++i){settings.passes[i].ratio=settings.shared?.5f:(i==1?.667f:i==2?1.f:.5f);settings.passes[i].intensity=settings.shared?1.f:1.f-.1f*i;}
            if(!D24ConfigureAdvanced(&settings))return 30;
            control.customFilter=phase==1||phase==2;modelDirty|=f==4||f==12;
        }
        if(!frame(a,f,0,out))return 10;
        if(chain&&advancedRequested()){
            unsigned expected=advancedControl.highResolution?1:advancedControl.count;
            printf("{\"event\":\"chain_frame\",\"frame\":%u,\"requested\":%u,\"recorded\":%u,\"ready\":%u,\"high\":%u,\"width\":%u,\"height\":%u}\n",f,expected,advancedStatus.recorded,advancedStatus.ready,advancedStatus.highResolution,advancedStatus.width,advancedStatus.height);
            if(advancedStatus.recorded!=expected||advancedStatus.ready!=expected)return 31;
        }
        if(dual){if(!frame(b,f,1,nullptr))return 11;
            if(f==7){auto residents=a.resources.resident;auto va=a.resources.va;
                if(!releaseFeature(b)||a.resources.resident!=residents||a.resources.va!=va)return 12;
                puts("{\"event\":\"release_other_preserves_resources\",\"passed\":true}");}}
    }
    fclose(out);
    if(!retireAdvanced()||!releaseFeature(a)||(dual&&!releaseFeature(b)))return 13;
    if(!a.resources.resident.empty()||!b.resources.resident.empty())return 14;
    ComPtr<ID3D11InfoQueue> queue;device.As(&queue);unsigned errors=0;
    if(queue)for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){SIZE_T size=0;queue->GetMessage(i,nullptr,&size);std::vector<unsigned char> bytes(size);auto msg=(D3D11_MESSAGE*)bytes.data();queue->GetMessage(i,msg,&size);if(msg->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){++errors;printf("D3D11_ERROR %s\n",msg->pDescription);}}
    printf("{\"event\":\"complete\",\"dual\":%s,\"debug_errors\":%u,\"dispatches\":%u}\n",dual?"true":"false",errors,registeredDispatches);
    return errors?15:0;
}
