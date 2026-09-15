// Included after the common frame/guide checks and exposure handling. Every
// allocation and feature below belongs to g_vk and retires with its leases.
#ifdef D18_VULKAN_ADVANCED_TEST
static int failAdvancedAllocation=-1;
#endif
bool RecordAdvancedVk(VkCommandBuffer cmd,const DlssNrNative::AdvancedSettings& settings,
    const DlssNrNative::Settings& common,NVSDK_NGX_Resource_VK* colour,NVSDK_NGX_Resource_VK* depth,
    NVSDK_NGX_Resource_VK* motion,unsigned gw,unsigned gh,bool inverted,bool reset,float sx,float sy,
    const DlssNrConstants& originalEncode,const VkAudit::Lease& lease){
    auto encode=originalEncode;
    auto& status=g_vk.advancedStatus;status={};status.tick=GetTickCount64();
    if(settings.count<1||settings.count>4||!std::isfinite(settings.scale)||
       (settings.scale!=1.25f&&settings.scale!=1.5f)){status.result=-101;return false;}
    for(const auto& t:settings.passes){
        if(!std::isfinite(t.ratio)||t.ratio<.5f||t.ratio>1||
           !std::isfinite(t.intensity)||t.intensity<0||t.intensity>2||
           !std::isfinite(t.structure)||t.structure<0||t.structure>2||
           !std::isfinite(t.tone)||t.tone<0||t.tone>2||
           !std::isfinite(t.skin)||t.skin< -1||t.skin>2||t.preset>3||t.style>2||t.autoMask>1){status.result=-101;return false;}
    }
    const bool high=settings.highResolution!=0,shared=settings.shared!=0&&!high;
    const unsigned count=high?1:settings.count;status.requested=count;status.highResolution=high;
    if(shared)for(unsigned i=1;i<count;++i)if(!(settings.passes[i]==settings.passes[0])){status.result=-30;return false;}
    unsigned ww=encode.Width,wh=encode.Height;
    if(high){ww=(unsigned(ww*settings.scale+.5f)+15)&~15u;wh=(unsigned(wh*settings.scale+.5f)+7)&~7u;}
    VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(g_vk.physicalDevice,&props);
    if(!ww||!wh||ww>std::min(6144u,props.limits.maxImageDimension2D)||wh>std::min(6144u,props.limits.maxImageDimension2D)){status.result=-5;return false;}
    status.width=ww;status.height=wh;
    auto allocate=[&](OwnedImage& image,unsigned w,unsigned h,VkFormat format,const char* name,bool writable=false){
#ifdef D18_VULKAN_ADVANCED_TEST
        if(!image.Valid()){
            if(failAdvancedAllocation==0)return false;
            if(failAdvancedAllocation>0)--failAdvancedAllocation;
        }
#endif
        return image.Valid()||CreateImage(image,w,h,format,writable,name,true);
    };
    if(!allocate(g_vk.delta[0],encode.Width,encode.Height,VK_FORMAT_R16G16B16A16_SFLOAT,"delta0")||
       !allocate(g_vk.delta[1],encode.Width,encode.Height,VK_FORMAT_R16G16B16A16_SFLOAT,"delta1")||
       (high&&!allocate(g_vk.largeInput,ww,wh,VK_FORMAT_R16G16B16A16_SFLOAT,"high_input"))||
       (shared&&!allocate(g_vk.zeroMotion,gw,gh,VK_FORMAT_R32G32_SFLOAT,"shared_zero_motion"))){status.result=-26;return false;}
    const unsigned instances=shared?1:count;
    for(unsigned i=0;i<instances;++i){auto& p=g_vk.models[i];auto t=settings.passes[i];const float ratio=high?1:t.ratio;
        if(!allocate(p.output,ww,wh,VK_FORMAT_R16G16B16A16_SFLOAT,"pass_output",true)||
            (common.customFilter&&ratio<1&&!allocate(p.filtered,ww,wh,VK_FORMAT_R16G16B16A16_SFLOAT,"pass_filtered"))){status.result=-26;return false;}
        if(!p.params&&(NVSDK_NGX_VULKAN_AllocateParameters(&p.params)!=NVSDK_NGX_Result_Success||!p.params)){status.result=-8;return false;}
        if(!p.feature){
            if(!g_vk.options||!g_vk.options(ratio,common.linearResolve,common.linearColorInput)){status.result=-101;return false;}
            p.feature=g_vk.create((void*)cmd,p.params,ww,wh,t.preset,t.intensity,t.style,t.structure,t.tone,t.skin,t.autoMask,1);
            if(!p.feature){status.result=-8;return false;}
            float got=0;if(p.params->Get("DLSSNR.ScalingRatio",&got)!=NVSDK_NGX_Result_Success||got!=ratio){status.result=-101;return false;}
        }++status.ready;
    }
    if(shared)status.ready=count;
    auto dispatch=[&](const DlssNrConstants& c,OwnedImage* src,OwnedImage* model,OwnedImage* original,OwnedImage* residual,OwnedImage& dst,OwnedImage* keep=nullptr){
        for(auto* p:{src,model,original,residual})if(p)Transition(cmd,*p,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Transition(cmd,dst,VK_IMAGE_LAYOUT_GENERAL);if(keep)Transition(cmd,*keep,VK_IMAGE_LAYOUT_GENERAL);
        return g_vk.pass->Dispatch(cmd,c,c.Width,c.Height,src?src->view:VK_NULL_HANDLE,model?model->view:VK_NULL_HANDLE,
            original?original->view:VK_NULL_HANDLE,VK_NULL_HANDLE,dst.view,keep?keep->view:VK_NULL_HANDLE,dst.format,
            keep?keep->format:VK_FORMAT_R16G16B16A16_SFLOAT,residual?residual->view:VK_NULL_HANDLE);
    };
    const auto range=colour->Resource.ImageViewInfo.SubresourceRange;
    // Guard every path after borrowing the game's colour layout.
    struct RestoreColour {VkCommandBuffer cmd;NVSDK_NGX_Resource_VK* c;bool read=false;
        ~RestoreColour(){if(read)TransitionForeign(cmd,c->Resource.ImageViewInfo.Image,c->Resource.ImageViewInfo.SubresourceRange,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);}
    } restore{cmd,colour};
    Transition(cmd,g_vk.proxy,VK_IMAGE_LAYOUT_GENERAL);Transition(cmd,g_vk.keep,VK_IMAGE_LAYOUT_GENERAL);
    TransitionForeign(cmd,colour->Resource.ImageViewInfo.Image,range,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);restore.read=true;
    encode.RelativeColour=2;encode.ValidWidth=encode.Width;encode.ValidHeight=encode.Height;
    if(!g_vk.pass->Dispatch(cmd,encode,encode.Width,encode.Height,colour->Resource.ImageViewInfo.ImageView,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,
        g_vk.proxy.view,g_vk.keep.view,g_vk.proxy.format,g_vk.keep.format)){status.result=-15;return false;}
    OwnedImage* previous=&g_vk.proxy;
    if(high){auto up=encode;up.Mode=5;up.Width=ww;up.Height=wh;if(!dispatch(up,previous,nullptr,nullptr,nullptr,g_vk.largeInput)){status.result=-15;return false;}previous=&g_vk.largeInput;}
    if(shared){auto zero=encode;zero.Mode=9;zero.Width=gw;zero.Height=gh;if(!dispatch(zero,nullptr,nullptr,nullptr,nullptr,g_vk.zeroMotion)){status.result=-15;return false;}}
    g_vk.nrDepth=*depth;g_vk.nrMotion=*motion;g_vk.nrDepth.ReadWrite=g_vk.nrMotion.ReadWrite=false;
    unsigned last=0;float minRatio=1;
    for(unsigned i=0;i<count;++i){auto& p=g_vk.models[shared?0:i];auto t=settings.passes[shared?0:i];float ratio=high?1:t.ratio;minRatio=std::min(minRatio,ratio);
        auto* input=previous;
        if(shared&&previous==&p.output){
            // The single shared feature cannot read and write its answer image.
            if(!allocate(g_vk.sharedInput,ww,wh,VK_FORMAT_R16G16B16A16_SFLOAT,"shared_input")){status.result=-26;return false;}
            auto copy=encode;copy.Mode=2;copy.Width=ww;copy.Height=wh;copy.SourceWidth=ww;copy.SourceHeight=wh;
            if(!dispatch(copy,previous,nullptr,nullptr,nullptr,g_vk.sharedInput)){status.result=-15;return false;}input=&g_vk.sharedInput;
        }
        if(common.customFilter&&ratio<1){auto filter=encode;filter.Mode=4;filter.Width=ww;filter.Height=wh;filter.SourceWidth=ww;filter.SourceHeight=wh;
            filter.NetworkRatioX=float(std::max(16u,unsigned(ww*ratio+.5f)&~15u))/ww;filter.NetworkRatioY=float(std::max(8u,unsigned(wh*ratio+.5f)&~7u))/wh;filter.CatmullRomInput=common.catmullRom;
            if(!dispatch(filter,input,nullptr,nullptr,nullptr,p.filtered)){status.result=-15;return false;}input=&p.filtered;
        }
        // The inspected forwarder reads a global ratio in Evaluate too. Set it
        // for this call under g_vkMutex; each feature still has separate params.
        if(!g_vk.options(ratio,common.linearResolve,common.linearColorInput)){status.result=-101;return false;}
        Transition(cmd,*input,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);Transition(cmd,p.output,VK_IMAGE_LAYOUT_GENERAL);
        if(shared&&i)Transition(cmd,g_vk.zeroMotion,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        auto* mv=shared&&i?&g_vk.zeroMotion.ngx:&g_vk.nrMotion;
        const int result=g_vk.evaluate((void*)cmd,p.feature,p.params,&input->ngx,&g_vk.nrDepth,mv,&p.output.ngx,ww,wh,gw,gh,inverted,
            (!p.frames||reset)&&(!shared||!i),t.intensity,t.style,t.structure,t.tone,t.skin,t.autoMask,sx,sy);
        if(result!=1){status.result=-13;return false;}++p.frames;++status.recorded;
        auto delta=encode;delta.Mode=high?6:(i?8:7);unsigned next=i?1-last:0;
        if(!dispatch(delta,input,&p.output,nullptr,i?&g_vk.delta[last]:nullptr,g_vk.delta[next])){status.result=-15;return false;}
        last=next;previous=&p.output;
    }
    auto resolve=encode;resolve.Mode=1;resolve.Transfer=0;resolve.NetworkRatioX=resolve.NetworkRatioY=minRatio;
    resolve.PreserveHighFrequency=settings.preserveHighFrequency;resolve.DebugView=common.debugView;resolve.CompareMode=common.compare;
    resolve.CompareSplit=common.compareSplit;resolve.CompareSwap=common.compareSwap;resolve.CompareZoom=common.compareZoom;
    for(auto* p:{&g_vk.proxy,&g_vk.keep,&g_vk.delta[last]})Transition(cmd,*p,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    TransitionForeign(cmd,colour->Resource.ImageViewInfo.Image,range,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);restore.read=false;
    if(!g_vk.pass->Dispatch(cmd,resolve,resolve.Width,resolve.Height,g_vk.proxy.view,g_vk.proxy.view,g_vk.keep.view,VK_NULL_HANDLE,
        colour->Resource.ImageViewInfo.ImageView,VK_NULL_HANDLE,colour->Resource.ImageViewInfo.Format,VK_FORMAT_R16G16B16A16_SFLOAT,g_vk.delta[last].view)){status.result=-16;return false;}
    status.result=1;g_vk.reset=false;++g_vk.frames;return true;
}
