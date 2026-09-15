// Native API extension: model instances own temporal state; this frame graph owns
// the full SR base and the only final composition. Called under the entry lock.
#include "advanced_shader.h"
#include <memory>
static DlssNrNative::AdvancedSettings advancedControl;
static DlssNrNative::AdvancedStatus advancedStatus;
static bool advancedDirty=false;
#ifdef D18_NATIVE_ADVANCED_TEST
static int failAdvancedAllocation=-1;
#endif
struct NativeAdvanced {
    std::array<std::unique_ptr<Session>,4> models;
    ComPtr<ID3D11Texture2D> proxy,keep,delta[2],largeInput,zeroMotion;
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    unsigned width=0,height=0,format=0,workWidth=0,workHeight=0,frames=0;
    bool failed=false;
    struct View { ID3D11Resource* resource=nullptr;ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11UnorderedAccessView> uav; };
    std::array<View,32> views;unsigned nextView=0;
};
static NativeAdvanced& advanced(){static auto* state=new NativeAdvanced;return *state;}
static bool advancedRequested(){return managed&&control.mode==2&&(advancedControl.highResolution||advancedControl.count>1);}
static bool retireAdvanced(){
    auto& a=advanced();
    for(auto& p:a.models)if(p&&!releaseFeature(*p))return false;
    for(auto& v:a.views)v={};a.nextView=0;
    // Keep initialized runtime common objects alive. Destroying/reinitializing
    // opaque runtime objects without its shutdown contract would leak state.
    a.proxy.Reset();a.keep.Reset();a.delta[0].Reset();a.delta[1].Reset();a.largeInput.Reset();a.zeroMotion.Reset();
    a.width=a.height=a.format=a.workWidth=a.workHeight=a.frames=0;a.failed=false;
    return true;
}
static bool ownedAdvanced(Session& s,ID3D11Resource* r){
    auto& a=advanced();if(!r)return false;
    if(s.ownViewResource(r)||r==a.proxy.Get()||r==a.keep.Get()||r==a.delta[0].Get()||r==a.delta[1].Get()||r==a.largeInput.Get()||r==a.zeroMotion.Get())return true;
    for(auto& p:a.models)if(p&&p->ownViewResource(r))return true;return false;
}
static NativeAdvanced::View* advancedView(Session& s,ID3D11Resource* r){
    if(!ownedAdvanced(s,r))return nullptr;auto& a=advanced();
    for(auto& v:a.views)if(v.resource==r)return &v;
    auto& v=a.views[a.nextView++%a.views.size()];v={};v.resource=r;return &v;
}
static bool advancedTexture(Session& s,ComPtr<ID3D11Texture2D>& target,unsigned w,unsigned h,DXGI_FORMAT format){
#ifdef D18_NATIVE_ADVANCED_TEST
    if(failAdvancedAllocation==0)return false;
    if(failAdvancedAllocation>0)--failAdvancedAllocation;
#endif
    D3D11_TEXTURE2D_DESC td{w,h,1,1,format,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,0,0};
    return SUCCEEDED(s.device->CreateTexture2D(&td,nullptr,&target));
}
static bool advancedDispatch(Session& s,const DlssNrConstants& c,ID3D11Resource* src,ID3D11Resource* model,
                            ID3D11Resource* original,ID3D11Resource* motion,ID3D11Resource* residual,
                            ID3D11Resource* target,ID3D11Resource* keep=nullptr){
    auto& a=advanced();
    if(!a.shader){
        ComPtr<ID3DBlob> code,errors;
        auto hr=D3DCompile(advancedSource,strlen(advancedSource),nullptr,nullptr,nullptr,"CSMain","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(FAILED(hr)){if(errors&&logFile)logPrint(logFile,"%s\n",(char*)errors->GetBufferPointer());return false;}
        if(FAILED(s.device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&a.shader)))return false;
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(c);bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(s.device->CreateBuffer(&bd,nullptr,&a.constants))){a.shader.Reset();return false;}
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        if(FAILED(s.device->CreateSamplerState(&sd,&a.sampler))){a.shader.Reset();return false;}
    }
    ID3D11Resource* sources[]={src,model,original,motion,residual};
    ComPtr<ID3D11ShaderResourceView> views[5];ID3D11ShaderResourceView* raw[5]{};
    for(unsigned i=0;i<5;++i)if(sources[i]){auto* cached=advancedView(s,sources[i]);
        if(cached&&cached->srv)views[i]=cached->srv;else{if(FAILED(s.device->CreateShaderResourceView(sources[i],nullptr,&views[i])))return false;if(cached)cached->srv=views[i];}raw[i]=views[i].Get();}
    ComPtr<ID3D11UnorderedAccessView> out,k;
    auto makeUav=[&](ID3D11Resource* r,ComPtr<ID3D11UnorderedAccessView>& v){auto* cached=advancedView(s,r);if(cached&&cached->uav){v=cached->uav;return true;}
        if(FAILED(s.device->CreateUnorderedAccessView(r,nullptr,&v)))return false;if(cached)cached->uav=v;return true;};
    if(!makeUav(target,out)||(keep&&!makeUav(keep,k)))return false;
    s.context->UpdateSubresource(a.constants.Get(),0,nullptr,&c,0,0);auto cb=a.constants.Get();auto sampler=a.sampler.Get();
    ID3D11UnorderedAccessView* outputs[]={out.Get(),k.Get()};
    s.context->CSSetShader(a.shader.Get(),nullptr,0);s.context->CSSetConstantBuffers(0,1,&cb);s.context->CSSetSamplers(0,1,&sampler);
    s.context->CSSetShaderResources(0,5,raw);s.context->CSSetUnorderedAccessViews(0,2,outputs,nullptr);s.context->Dispatch((c.Width+7)/8,(c.Height+7)/8,1);
    memset(raw,0,sizeof(raw));memset(outputs,0,sizeof(outputs));s.context->CSSetShaderResources(0,5,raw);s.context->CSSetUnorderedAccessViews(0,2,outputs,nullptr);
    return true;
}
static int evaluateAdvancedModel(Session& s,ID3D11Texture2D* input,ID3D11Resource* depth,ID3D11Resource* motion,
    unsigned rw,unsigned rh,unsigned mw,unsigned mh,float sx,float sy,bool reset,unsigned flags,float ratio){
    ResourceScope resources(s.resources);auto& p=s.parameters;
    p.Set("DLSSNR.Color",static_cast<ID3D11Resource*>(input));p.Set("DLSSNR.Output",static_cast<ID3D11Resource*>(s.output.Get()));
    p.Set("DLSSNR.Depth",depth);p.Set("DLSSNR.MVec",motion);
    setRect(p,"Color",s.desc.Width,s.desc.Height);setRect(p,"Output",s.desc.Width,s.desc.Height);setRect(p,"Depth",rw,rh);setRect(p,"MVec",mw,mh);
    p.Set("DLSSNR.MVecScaleX",sx);p.Set("DLSSNR.MVecScaleY",sy);p.Set("DLSSNR.DepthInverted",(flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)?1u:0u);
    p.Set("DLSSNR.Reset",reset||!s.frames?1u:0u);p.Set("DLSSNR.ScalingRatio",ratio);
    ID3D11Resource* borrowed[]={input,s.output.Get(),depth,motion};for(auto* r:borrowed)track(r);
    if(s.resources.resident.size()>10)return -10;
    s.inflight.clear();for(auto* r:s.resources.resident)s.inflight.emplace_back(static_cast<ID3D11Resource*>(r));
    memset(s.resources.pending,0,sizeof(s.resources.pending));void* feature=*reinterpret_cast<void**>(s.common+0x10);
    int code=reinterpret_cast<int(*)(void*,void*,void*,void*)>(nativeVtable[0xe0/8])(nativeBackend,s.context.Get(),nullptr,feature);
    if(code)return -11;
    *reinterpret_cast<void**>((unsigned char*)nativeBackend+0x140)=s.resources.pending;
    code=reinterpret_cast<int(*)(void*,void*,void*,void*,void*)>(imageBase+0x18620)(s.common,s.context.Get(),s.handle,&p,nullptr);
    if(!completed(s,0))return -12;
    for(auto* r:borrowed)s.resources.resident.erase(std::remove(s.resources.resident.begin(),s.resources.resident.end(),r),s.resources.resident.end());
    s.inflight.clear();if(code!=1)return -13;++s.frames;return 1;
}
struct ModelControlScope {
    DlssNrNative::Settings old=control;
    explicit ModelControlScope(const DlssNrNative::ModelSettings& p,bool high){
        control.networkRatio=high?1:p.ratio;control.preset=p.preset;control.intensity=p.intensity;control.style=p.style;
        control.localStructure=p.structure;control.localTone=p.tone;control.skinStructure=p.skin;control.autoMask=p.autoMask;
        control.customFilter=old.customFilter&&control.networkRatio<1;control.linearColorInput=!control.customFilter&&old.linearColorInput;
    }
    ~ModelControlScope(){control=old;}
};
static int processAdvanced(Session& s,void* owner,ID3D11Texture2D* game,ID3D11Texture2D* depth,ID3D11Texture2D* motion,NVSDK_NGX_Parameter* params,unsigned flags){
    auto& a=advanced();advancedStatus.recorded=0;advancedStatus.ready=0;advancedStatus.tick=GetTickCount64();
    const bool high=advancedControl.highResolution!=0,shared=advancedControl.shared!=0&&!high;
    unsigned count=high?1:advancedControl.count;advancedStatus.requested=count;advancedStatus.highResolution=high;
    if(shared)for(unsigned i=1;i<count;++i)if(!(advancedControl.passes[i]==advancedControl.passes[0]))return -30;
    D3D11_TEXTURE2D_DESC od{},dd{},md{};game->GetDesc(&od);depth->GetDesc(&dd);motion->GetDesc(&md);
    unsigned ww=od.Width,wh=od.Height;
    if(high){ww=(unsigned(ww*advancedControl.scale+.5f)+15)&~15u;wh=(unsigned(wh*advancedControl.scale+.5f)+7)&~7u;}
    if(ww>6144||wh>6144)return -5;
    advancedStatus.width=ww;advancedStatus.height=wh;
    if(advancedDirty||modelDirty||a.width!=od.Width||a.height!=od.Height||a.format!=unsigned(od.Format)||a.workWidth!=ww||a.workHeight!=wh){
        if(!retireAdvanced()||!releaseFeature(s))return -25;
        a.width=od.Width;a.height=od.Height;a.format=od.Format;a.workWidth=ww;a.workHeight=wh;
        advancedDirty=modelDirty=false;
        if(!advancedTexture(s,a.proxy,od.Width,od.Height,DXGI_FORMAT_R16G16B16A16_FLOAT)||
           !advancedTexture(s,a.keep,od.Width,od.Height,DXGI_FORMAT_R32G32B32A32_FLOAT)||
           !advancedTexture(s,a.delta[0],od.Width,od.Height,DXGI_FORMAT_R16G16B16A16_FLOAT)||
           !advancedTexture(s,a.delta[1],od.Width,od.Height,DXGI_FORMAT_R16G16B16A16_FLOAT)||
           (high&&!advancedTexture(s,a.largeInput,ww,wh,DXGI_FORMAT_R16G16B16A16_FLOAT))){a.failed=true;return -26;}
    }
    if(a.failed)return -31;
    s.desc=od;
    const unsigned modelCount=shared?1:count;
    for(unsigned i=0;i<modelCount;++i){
        if(!a.models[i])a.models[i]=std::make_unique<Session>();auto& p=*a.models[i];
        if(!initialize(p,s.context.Get())){a.failed=true;return -6;}
        D3D11_TEXTURE2D_DESC wd=od;wd.Width=ww;wd.Height=wh;wd.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        ModelControlScope tuning(advancedControl.passes[i],high);
        if(!makeFeature(p,wd)){a.failed=true;return p.allocationFailure?p.allocationFailure:-8;}
        ++advancedStatus.ready;
    }
    if(shared)advancedStatus.ready=count;
    unsigned rw=dd.Width,rh=dd.Height;params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,&rw);params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,&rh);
    bool low=flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;unsigned mw=low?rw:md.Width,mh=low?rh:md.Height;
    if(!rw||!rh||rw>dd.Width||rh>dd.Height||mw>md.Width||mh>md.Height)return -9;
    float sx=1,sy=1;params->Get(NVSDK_NGX_Parameter_MV_Scale_X,&sx);params->Get(NVSDK_NGX_Parameter_MV_Scale_Y,&sy);if(!std::isfinite(sx)||!std::isfinite(sy))return -23;
    unsigned reset=0;params->Get(NVSDK_NGX_Parameter_Reset,&reset);
    s.jitterSample=jitterInput(s,params,owner,s.context.Get(),rw,rh,md.Width,md.Height,reset);s.jitterPlan=s.jitterHistory.plan(s.jitterSample);
    if(!ownGuide(s,depth,s.ownedDepth,DXGI_FORMAT_R32_FLOAT))return -24;
    if(s.jitterPlan.active){
        if(!guideStorage(s,motion,s.ownedMotion,DXGI_FORMAT_R32G32_FLOAT)||!guideStorage(s,motion,s.nrMotion,DXGI_FORMAT_R32G32_FLOAT)||
            !convert(s,motion,s.ownedMotion.Get(),s.nrMotion.Get(),s.jitterPlan.dx,s.jitterPlan.dy,rw,rh))return -24;
    }else if(!ownGuide(s,motion,s.ownedMotion,DXGI_FORMAT_R32G32_FLOAT))return -24;
    if(shared){
        if(!guideStorage(s,motion,a.zeroMotion,DXGI_FORMAT_R32G32_FLOAT))return -26;
        ComPtr<ID3D11UnorderedAccessView> zero;if(FAILED(s.device->CreateUnorderedAccessView(a.zeroMotion.Get(),nullptr,&zero)))return -24;
        float value[4]{};s.context->ClearUnorderedAccessViewFloat(zero.Get(),value);
    }
    sampleExposure(s,params,niohExposureProfile(),control.useExposure!=0);
    if(control.useExposure&&niohExposureProfile()&&!s.exposureAllocationFailed&&s.exposure<=1e-6f)return 0;
    DlssNrConstants c{};c.Width=od.Width;c.Height=od.Height;c.ValidWidth=od.Width;c.ValidHeight=od.Height;
    c.SourceWidth=c.Width;c.SourceHeight=c.Height;c.WhitePoint=control.whitePoint;
    if(control.useExposure&&s.exposure>1e-6f)c.WhitePoint=std::clamp(s.exposurePre/s.exposure*control.whitePoint,.01f,4096.f);
    c.TransferStrength=control.transferStrength;c.ColourStrength=control.colourStrength;c.MaxRatio=control.maxRatio;
    c.Passthrough=(flags&NVSDK_NGX_DLSS_Feature_Flags_IsHDR)?0:1;c.RelativeColour=2;c.CompareZoom=1;
    c.NetworkRatioX=c.NetworkRatioY=1;
    if(!advancedDispatch(s,c,game,nullptr,nullptr,nullptr,nullptr,a.proxy.Get(),a.keep.Get()))return -15;
    ID3D11Texture2D* previous=a.proxy.Get();
    if(high){auto up=c;up.Mode=5;up.Width=ww;up.Height=wh;
        if(!advancedDispatch(s,up,a.proxy.Get(),nullptr,nullptr,nullptr,nullptr,a.largeInput.Get()))return -15;previous=a.largeInput.Get();}
    unsigned last=0;float minRatio=1;
    for(unsigned i=0;i<count;++i){auto& model=*a.models[shared?0:i];auto tuning=advancedControl.passes[shared?0:i];
        const float ratio=high?1:tuning.ratio;minRatio=(std::min)(minRatio,ratio);
        // Copy previous encoded answer, never compose/re-encode the SR target.
        s.context->CopyResource(model.input.Get(),previous);ID3D11Texture2D* input=model.input.Get();
        if(control.customFilter&&ratio<1){auto filter=c;filter.Mode=4;filter.Width=ww;filter.Height=wh;filter.SourceWidth=ww;filter.SourceHeight=wh;
            filter.NetworkRatioX=float((std::max)(16u,unsigned(ww*ratio+.5f)&~15u))/ww;
            filter.NetworkRatioY=float((std::max)(8u,unsigned(wh*ratio+.5f)&~7u))/wh;filter.CatmullRomInput=control.catmullRom;
            if(!advancedDispatch(s,filter,model.input.Get(),nullptr,nullptr,nullptr,nullptr,model.filtered.Get()))return -15;input=model.filtered.Get();}
        auto* mv=(shared&&i)?a.zeroMotion.Get():(s.jitterPlan.active?s.nrMotion.Get():s.ownedMotion.Get());
        int result=evaluateAdvancedModel(model,input,s.ownedDepth.Get(),mv,rw,rh,mw,mh,sx,sy,
            (reset||!a.frames||s.jitterPlan.reset)&&(!shared||!i),flags,ratio);
        if(result!=1){a.failed=true;return result;}++advancedStatus.recorded;
        auto delta=c;delta.Mode=high?6:(i?8:7);unsigned next=i?1-last:0;
        if(!advancedDispatch(s,delta,input,model.output.Get(),nullptr,nullptr,i?a.delta[last].Get():nullptr,a.delta[next].Get())){a.failed=true;return -15;}
        last=next;previous=model.output.Get();
    }
    c.Mode=1;c.NetworkRatioX=c.NetworkRatioY=minRatio;c.PreserveHighFrequency=advancedControl.preserveHighFrequency;
    c.DebugView=control.debugView;c.DebugScale=c.WhitePoint;c.CompareMode=control.compare;c.CompareSplit=control.compareSplit;c.CompareSwap=control.compareSwap;c.CompareZoom=control.compareZoom;
    if(!advancedDispatch(s,c,a.proxy.Get(),a.proxy.Get(),a.keep.Get(),s.ownedMotion.Get(),a.delta[last].Get(),game))return -16;
    if(!completed(s,1))return -14;
    s.jitterHistory.commit(s.jitterSample,s.jitterPlan);++a.frames;++s.frames;return 1;
}
