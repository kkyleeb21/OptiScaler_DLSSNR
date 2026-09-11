#pragma once
#include <array>
#include <sl_reflex.h>
#include <sl_pcl.h>

// Synthetic static scene only. Immutable FG guides stay allocated through all
// presents. Serial GPU waits simplify this diagnostic, not the game integration.
inline void ProbePresent(HMODULE module, PFN_vkGetInstanceProcAddr gipa,
    PFN_vkGetDeviceProcAddr gdpa, VkInstance instance, VkPhysicalDevice physical,
    VkDevice device, VkSwapchainKHR swapchain, uint32_t family, VkExtent2D extent,
    uint32_t maxGenerated,HWND probeWindow=nullptr)
{
    auto require=[](bool ok,const char* what){if(!ok)throw std::runtime_error(what);};
    auto vkcheck=[&](VkResult r,const char* what){if(r!=VK_SUCCESS){printf("present_error step=%s vk=%d\n",what,int(r));require(false,what);}};
    auto slcheck=[&](sl::Result r,const char* what){if(r!=sl::Result::eOk){printf("present_error step=%s sl=%u\n",what,unsigned(r));require(false,what);}};
    auto dp=[&](const char* name){auto p=gdpa(device,name);require(p!=nullptr,name);return p;};
#define PROBE_VK(name) auto name=reinterpret_cast<PFN_##name>(dp(#name))
    PROBE_VK(vkCreateImage);PROBE_VK(vkGetImageMemoryRequirements);PROBE_VK(vkAllocateMemory);
    PROBE_VK(vkBindImageMemory);PROBE_VK(vkCreateImageView);PROBE_VK(vkDestroyImageView);
    PROBE_VK(vkDestroyImage);PROBE_VK(vkFreeMemory);PROBE_VK(vkCreateCommandPool);
    PROBE_VK(vkDestroyCommandPool);PROBE_VK(vkAllocateCommandBuffers);PROBE_VK(vkResetCommandPool);
    PROBE_VK(vkBeginCommandBuffer);PROBE_VK(vkEndCommandBuffer);PROBE_VK(vkCmdPipelineBarrier);
    PROBE_VK(vkCmdClearDepthStencilImage);PROBE_VK(vkCmdClearColorImage);PROBE_VK(vkCreateSemaphore);PROBE_VK(vkDestroySemaphore);
    PROBE_VK(vkQueueSubmit);PROBE_VK(vkDeviceWaitIdle);PROBE_VK(vkGetDeviceQueue);
    PROBE_VK(vkAcquireNextImageKHR);PROBE_VK(vkQueuePresentKHR);PROBE_VK(vkGetSwapchainImagesKHR);
    PROBE_VK(vkCreateFence);PROBE_VK(vkDestroyFence);PROBE_VK(vkWaitForFences);PROBE_VK(vkResetFences);
#undef PROBE_VK
    auto feature=reinterpret_cast<PFun_slGetFeatureFunction*>(GetProcAddress(module,"slGetFeatureFunction"));
    auto function=[&](sl::Feature id,const char* name){void* p=nullptr;slcheck(feature(id,name,p),name);require(p!=nullptr,name);return p;};
    auto setOptions=reinterpret_cast<PFun_slDLSSGSetOptions*>(function(sl::kFeatureDLSS_G,"slDLSSGSetOptions"));
    auto getState=reinterpret_cast<PFun_slDLSSGGetState*>(function(sl::kFeatureDLSS_G,"slDLSSGGetState"));
    auto setReflex=reinterpret_cast<PFun_slReflexSetOptions*>(function(sl::kFeatureReflex,"slReflexSetOptions"));
    auto sleepReflex=reinterpret_cast<PFun_slReflexSleep*>(function(sl::kFeatureReflex,"slReflexSleep"));
    auto marker=reinterpret_cast<PFun_slPCLSetMarker*>(function(sl::kFeaturePCL,"slPCLSetMarker"));
    auto getToken=reinterpret_cast<PFun_slGetNewFrameToken*>(GetProcAddress(module,"slGetNewFrameToken"));
    auto setConstants=reinterpret_cast<PFun_slSetConstants*>(GetProcAddress(module,"slSetConstants"));
    auto setTags=reinterpret_cast<PFun_slSetTagForFrame*>(GetProcAddress(module,"slSetTagForFrame"));
    require(getToken&&setConstants&&setTags,"frame APIs");
    VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);require(queue!=VK_NULL_HANDLE,"graphics queue");
    struct Input {VkImage image{};VkDeviceMemory memory{};VkImageView view{};sl::Resource resource{};};
#ifdef D18_PROBE_GAME_INPUT
    constexpr bool automatic=true;
#else
    constexpr bool automatic=false;
#endif
    std::array<Input,4> inputs{};
    NVSDK_NGX_Parameter* ngxParams=nullptr;NVSDK_NGX_Handle* ngxHandle=nullptr;
    const auto d18=GetModuleHandleW(L"dxgi.dll");
    auto ngxCreate=(NVSDK_NGX_Result (*)(VkDevice,VkCommandBuffer,NVSDK_NGX_Feature,const NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**))GetProcAddress(d18,"NVSDK_NGX_VULKAN_CreateFeature1");
    auto ngxEvaluate=(NVSDK_NGX_Result (*)(VkCommandBuffer,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback))GetProcAddress(d18,"NVSDK_NGX_VULKAN_EvaluateFeature");
    auto ngxRelease=(NVSDK_NGX_Result (*)(NVSDK_NGX_Handle*))GetProcAddress(d18,"NVSDK_NGX_VULKAN_ReleaseFeature");
    auto ngxDestroy=(NVSDK_NGX_Result (*)(NVSDK_NGX_Parameter*))GetProcAddress(d18,"NVSDK_NGX_VULKAN_DestroyParameters");
    VkCommandPool pool{};VkCommandBuffer cmd{},depthCmd{};VkSemaphore acquire{};VkFence submitted{};
    std::vector<VkSemaphore> ready;
    sl::DLSSGOptions options{};sl::ViewportHandle viewport(0);
    auto cleanup=[&]{
        options.mode=sl::DLSSGMode::eOff;
        const auto off=setOptions(viewport,options);printf("present_cleanup_off=%u\n",unsigned(off));
        const auto idle=vkDeviceWaitIdle(device);printf("present_cleanup_idle=%d\n",int(idle));
        if(idle!=VK_SUCCESS)return; // Do not release possibly in-flight storage on an unknown failure.
        if(ngxHandle&&ngxRelease){ngxRelease(ngxHandle);ngxHandle=nullptr;}if(ngxParams&&ngxDestroy){ngxDestroy(ngxParams);ngxParams=nullptr;}
        for(auto& in:inputs){if(in.view)vkDestroyImageView(device,in.view,nullptr);if(in.image)vkDestroyImage(device,in.image,nullptr);if(in.memory)vkFreeMemory(device,in.memory,nullptr);}
        if(acquire)vkDestroySemaphore(device,acquire,nullptr);for(auto s:ready)if(s)vkDestroySemaphore(device,s,nullptr);
        if(submitted)vkDestroyFence(device,submitted,nullptr);
        if(pool)vkDestroyCommandPool(device,pool,nullptr);
    };
    try {
        VkPhysicalDeviceMemoryProperties memory{};
        auto getMemory=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance,"vkGetPhysicalDeviceMemoryProperties"));
        require(getMemory!=nullptr,"memory properties");getMemory(physical,&memory);
#ifdef D18_PROBE_VOLATILE
        constexpr bool volatileInputs=true;
#else
        constexpr bool volatileInputs=false;
#endif
        printf("volatile_inputs=%d depth_format=%s\n",volatileInputs,volatileInputs?"D24S8":"R32");
        const VkFormat formats[]={volatileInputs?VK_FORMAT_D24_UNORM_S8_UINT:VK_FORMAT_R32_SFLOAT,VK_FORMAT_R16G16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT};
        for(size_t n=0;n<inputs.size();++n){
            auto& in=inputs[n];VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            ci.imageType=VK_IMAGE_TYPE_2D;ci.format=formats[n];ci.extent={extent.width,extent.height,1};
            ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;
            ci.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            if(n==3)ci.usage|=VK_IMAGE_USAGE_STORAGE_BIT;
#ifdef D18_PROBE_OUTPUT_METADATA_ONLY
            if(n==3)ci.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
#endif
#ifdef D18_PROBE_CROSS_DEPTH
            if(n==0)ci.usage|=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
#endif
            vkcheck(vkCreateImage(device,&ci,nullptr,&in.image),"guide image");
            VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,in.image,&req);
            uint32_t type=UINT32_MAX;for(uint32_t t=0;t<memory.memoryTypeCount;++t)
                if((req.memoryTypeBits&(1u<<t))&&(memory.memoryTypes[t].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=t;break;}
            require(type!=UINT32_MAX,"device-local memory");VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;
            vkcheck(vkAllocateMemory(device,&alloc,nullptr,&in.memory),"guide allocation");
            vkcheck(vkBindImageMemory(device,in.image,in.memory,0),"guide bind");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=in.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=ci.format;view.subresourceRange={VkImageAspectFlags(volatileInputs&&n==0?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1};
            vkcheck(vkCreateImageView(device,&view,nullptr,&in.view),"guide view");
            in.resource=sl::Resource(sl::ResourceType::eTex2d,(void*)in.image,(void*)in.memory,(void*)in.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            in.resource.width=extent.width;in.resource.height=extent.height;in.resource.nativeFormat=ci.format;
            in.resource.mipLevels=in.resource.arrayLayers=1;in.resource.flags=0;in.resource.usage=ci.usage;
        }
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pci.queueFamilyIndex=family;
        vkcheck(vkCreateCommandPool(device,&pci,nullptr,&pool),"command pool");
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=pool;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;
        vkcheck(vkAllocateCommandBuffers(device,&cai,&cmd),"command buffer");
#ifdef D18_PROBE_CROSS_DEPTH
        vkcheck(vkAllocateCommandBuffers(device,&cai,&depthCmd),"depth command buffer");
#endif
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkcheck(vkCreateSemaphore(device,&sci,nullptr,&acquire),"acquire semaphore");
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fci.flags=VK_FENCE_CREATE_SIGNALED_BIT;
        vkcheck(vkCreateFence(device,&fci,nullptr,&submitted),"submit fence");
        auto barrier=[&](VkImage image,VkImageLayout oldLayout,VkImageLayout newLayout,VkAccessFlags src,VkAccessFlags dst){
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=oldLayout;b.newLayout=newLayout;
            b.srcAccessMask=src;b.dstAccessMask=dst;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={volatileInputs&&image==inputs[0].image?VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT):VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1};
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
        };
        auto begin=[&]{vkcheck(vkResetCommandPool(device,pool,0),"reset pool");VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;vkcheck(vkBeginCommandBuffer(cmd,&bi),"begin command");};
        begin();VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        const VkClearColorValue colours[]={{{0.5f,0,0,0}},{{0,0,0,0}},{{0.25f,0.25f,0.25f,1}},{{0,0,0,1}}};
        for(size_t n=0;n<inputs.size();++n){barrier(inputs[n].image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
            if(volatileInputs&&n==0){VkImageSubresourceRange depthRange{VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0,1,0,1};VkClearDepthStencilValue clear{0.5f,0};vkCmdClearDepthStencilImage(cmd,inputs[n].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&depthRange);}
            else vkCmdClearColorImage(cmd,inputs[n].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&colours[n],1,&range);
            barrier(inputs[n].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,n==3?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,n==3?VK_ACCESS_SHADER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT);}
        if(automatic){
            auto getParams=(NVSDK_NGX_Result (*)(NVSDK_NGX_Parameter**))GetProcAddress(d18,"NVSDK_NGX_VULKAN_GetCapabilityParameters");
            require(getParams&&ngxCreate&&ngxEvaluate&&ngxRelease&&ngxDestroy,"D18 NGX APIs");
            require(getParams(&ngxParams)==NVSDK_NGX_Result_Success&&ngxParams,"game params");
            ngxParams->Set(NVSDK_NGX_Parameter_Width,extent.width);ngxParams->Set(NVSDK_NGX_Parameter_Height,extent.height);
            ngxParams->Set(NVSDK_NGX_Parameter_OutWidth,extent.width);ngxParams->Set(NVSDK_NGX_Parameter_OutHeight,extent.height);
            ngxParams->Set(NVSDK_NGX_Parameter_PerfQualityValue,NVSDK_NGX_PerfQuality_Value_DLAA);
            ngxParams->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,int(NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_IsHDR));
            const auto result=ngxCreate(device,cmd,NVSDK_NGX_Feature_SuperSampling,ngxParams,&ngxHandle);
            printf("game_frame_sr_create=%x handle=%p automatic=%d\n",unsigned(result),ngxHandle,automatic);require(result==NVSDK_NGX_Result_Success&&ngxHandle,"game SR creation");
        }
        vkcheck(vkEndCommandBuffer(cmd),"end initialization");VkSubmitInfo initial{VK_STRUCTURE_TYPE_SUBMIT_INFO};initial.commandBufferCount=1;initial.pCommandBuffers=&cmd;
        vkcheck(vkQueueSubmit(queue,1,&initial,VK_NULL_HANDLE),"initialize guides");vkcheck(vkDeviceWaitIdle(device),"initialize idle");
        uint32_t imageCount=0;vkcheck(vkGetSwapchainImagesKHR(device,swapchain,&imageCount,nullptr),"present images count");
        std::vector<VkImage> images(imageCount);vkcheck(vkGetSwapchainImagesKHR(device,swapchain,&imageCount,images.data()),"present images");
        ready.resize(imageCount);for(auto& s:ready)vkcheck(vkCreateSemaphore(device,&sci,nullptr,&s),"image present semaphore");
        if(!automatic){sl::ReflexOptions reflex{};reflex.mode=sl::ReflexMode::eLowLatency;slcheck(setReflex(reflex),"Reflex options");}
        sl::Constants constants{};
        auto identity=[](sl::float4x4& m){for(int r=0;r<4;++r)m[r]=sl::float4(r==0?1.0f:0.0f,r==1?1.0f:0.0f,r==2?1.0f:0.0f,r==3?1.0f:0.0f);};
        identity(constants.cameraViewToClip);identity(constants.clipToCameraView);identity(constants.clipToLensClip);identity(constants.clipToPrevClip);identity(constants.prevClipToClip);
        constants.jitterOffset={0,0};constants.mvecScale={1,1};constants.cameraPinholeOffset={0,0};constants.cameraPos={0,0,0};
        constants.cameraUp={0,1,0};constants.cameraRight={1,0,0};constants.cameraFwd={0,0,1};
        constants.cameraNear=0.1f;constants.cameraFar=100.0f;constants.cameraFOV=1.0f;constants.cameraAspectRatio=float(extent.width)/extent.height;
        constants.depthInverted=sl::eFalse;constants.cameraMotionIncluded=sl::eTrue;constants.motionVectors3D=sl::eFalse;constants.orthographicProjection=sl::eTrue;
        sl::Extent full{0,0,extent.width,extent.height};
        const auto lifetime=volatileInputs?sl::eOnlyValidNow:sl::eValidUntilPresent;
        sl::ResourceTag tags[]={ {&inputs[0].resource,sl::kBufferTypeDepth,lifetime,&full},
            {&inputs[1].resource,sl::kBufferTypeMotionVectors,lifetime,&full},
            {&inputs[2].resource,sl::kBufferTypeHUDLessColor,lifetime,&full},
            {nullptr,sl::kBufferTypeUIColorAndAlpha,sl::eValidUntilPresent,&full} };
        uint32_t frame=0;bool allMultipliersObserved=true;
        const auto iniPath=std::filesystem::current_path()/L"OptiScaler.ini";
        wchar_t enabledText[16]{};GetPrivateProfileStringW(L"FrameGen",L"Enabled",L"false",enabledText,16,iniPath.c_str());
        const bool enabled=_wcsicmp(enabledText,L"true")==0 || wcscmp(enabledText,L"1")==0;
        const auto requested=enabled?GetPrivateProfileIntW(L"DLSSG",L"InterpolationCount",1,iniPath.c_str()):0;
        for(uint32_t generated=automatic?requested:0;generated<=(automatic?requested:std::min(maxGenerated,5u));++generated){
            vkcheck(vkDeviceWaitIdle(device),"mode idle");options.mode=generated?sl::DLSSGMode::eOn:sl::DLSSGMode::eOff;options.numFramesToGenerate=std::max(1u,generated);
            if(!automatic)slcheck(setOptions(viewport,options),"FG mode");
            uint64_t actual=0;uint32_t status=0;sl::DLSSGState state{};slcheck(getState(viewport,state,nullptr),"state baseline");
#ifdef D18_PROBE_LONG_RUN
            constexpr uint32_t runFrames=720;
#else
            constexpr uint32_t runFrames=120;
#endif
            for(uint32_t local=0;local<runFrames;++local,++frame){
                // One foreground refresh after initialization/warmup, confined
                // to this explicitly visible synthetic window. Never a game.
                if(local==64 && probeWindow && IsWindowVisible(probeWindow)){
                    const DWORD current=GetCurrentThreadId(),foreground=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
                    const bool attached=foreground&&foreground!=current&&AttachThreadInput(current,foreground,TRUE);
                    BringWindowToTop(probeWindow);SetForegroundWindow(probeWindow);SetFocus(probeWindow);
                    if(attached)AttachThreadInput(current,foreground,FALSE);
                    printf("measurement_foreground=%d\n",GetForegroundWindow()==probeWindow);
                }
                Sleep(16); // Bounded synthetic pacing; allow asynchronous first-use compilation.
                vkcheck(vkWaitForFences(device,1,&submitted,VK_TRUE,1000000000),"previous submission completion");
                MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                sl::FrameToken* token=nullptr;
                if(!automatic){slcheck(getToken(token,&frame),"frame token");require(token!=nullptr,"token null");slcheck(sleepReflex(*token),"Reflex sleep");slcheck(marker(sl::PCLMarker::eSimulationStart,*token),"sim start");slcheck(marker(sl::PCLMarker::eSimulationEnd,*token),"sim end");}
                uint32_t index=0;vkcheck(vkAcquireNextImageKHR(device,swapchain,1000000000,acquire,VK_NULL_HANDLE,&index),"acquire");require(index<images.size(),"image index");
                begin();barrier(images[index],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
                vkCmdClearColorImage(cmd,images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&colours[2],1,&range);
                barrier(images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_ACCESS_TRANSFER_WRITE_BIT,0);
                constants.reset=local==0?sl::eTrue:sl::eFalse;
#ifdef D18_PROBE_EARLY_DEPTH
                VkCommandBufferBeginInfo earlyInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};earlyInfo.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkcheck(vkBeginCommandBuffer(depthCmd,&earlyInfo),"begin early depth");
                const auto earlySrCmd=cmd;cmd=depthCmd;
                barrier(inputs[0].image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
                barrier(inputs[0].image,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
                cmd=earlySrCmd;vkcheck(vkEndCommandBuffer(depthCmd),"end early depth");
#endif
                if(!automatic){slcheck(setConstants(constants,*token,viewport),"constants");slcheck(setTags(*token,viewport,tags,4,(sl::CommandBuffer*)cmd),"resource tags");}
                else if((local<32 || local>=44)
#ifdef D18_PROBE_INTERMITTENT
                    && local%2==0
#endif
                ){
                    NVSDK_NGX_Resource_VK resources[4]{};
                    for(unsigned n=0;n<4;++n){auto& r=resources[n];r.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;r.ReadWrite=n==3;
                        r.Resource.ImageViewInfo={inputs[n].view,inputs[n].image,{VkImageAspectFlags(n==0&&volatileInputs?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1},formats[n],extent.width,extent.height};}
                    ngxParams->Set(NVSDK_NGX_Parameter_Color,(void*)&resources[2]);ngxParams->Set(NVSDK_NGX_Parameter_Output,(void*)&resources[3]);
                    ngxParams->Set(NVSDK_NGX_Parameter_Depth,(void*)&resources[0]);ngxParams->Set(NVSDK_NGX_Parameter_MotionVectors,(void*)&resources[1]);
                    ngxParams->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,0.0f);ngxParams->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,0.0f);
                    ngxParams->Set(NVSDK_NGX_Parameter_MV_Scale_X,1.0f);ngxParams->Set(NVSDK_NGX_Parameter_MV_Scale_Y,1.0f);
                    ngxParams->Set(NVSDK_NGX_Parameter_Reset,local==0?1:0);
                    ngxParams->Set("DLSS.Render.Subrect.Dimensions.Width",extent.width);ngxParams->Set("DLSS.Render.Subrect.Dimensions.Height",extent.height);
                    barrier(inputs[1].image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
                    const auto evaluated=ngxEvaluate(cmd,ngxHandle,ngxParams,nullptr);require(evaluated==NVSDK_NGX_Result_Success,"automatic SR evaluate");
                    // The application explicitly declares the read layout before reusing its depth.
#ifndef D18_PROBE_CROSS_DEPTH
                    barrier(inputs[0].image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
#endif
                }
                vkcheck(vkEndCommandBuffer(cmd),"end frame");
                std::array<VkCommandBuffer,2> ordered{depthCmd,cmd};
#ifdef D18_PROBE_DEPTH_REVERSE
                std::swap(ordered[0],ordered[1]);
#endif
#if defined(D18_PROBE_CROSS_DEPTH) && !defined(D18_PROBE_EARLY_DEPTH)
                // Record after SR on the CPU, submit before SR on the GPU.
                // This is the relationship missed by the former same-command gate.
                VkCommandBufferBeginInfo dbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};dbi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkcheck(vkBeginCommandBuffer(depthCmd,&dbi),"begin depth");
                const auto srCmd=cmd;cmd=depthCmd;
                barrier(inputs[0].image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
                barrier(inputs[0].image,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
                cmd=srCmd;vkcheck(vkEndCommandBuffer(depthCmd),"end depth");
#endif
                VkPipelineStageFlags waitStage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&acquire;submit.pWaitDstStageMask=&waitStage;submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&ready[index];
#ifdef D18_PROBE_CROSS_DEPTH
                submit.commandBufferCount=2;submit.pCommandBuffers=ordered.data();
#endif
                vkcheck(vkResetFences(device,1,&submitted),"reset submit fence");
                if(!automatic)slcheck(marker(sl::PCLMarker::eRenderSubmitStart,*token),"submit start");vkcheck(vkQueueSubmit(queue,1,&submit,submitted),"frame submit");if(!automatic)slcheck(marker(sl::PCLMarker::eRenderSubmitEnd,*token),"submit end");
                VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&ready[index];present.swapchainCount=1;present.pSwapchains=&swapchain;present.pImageIndices=&index;
                if(!automatic)slcheck(marker(sl::PCLMarker::ePresentStart,*token),"present start");vkcheck(vkQueuePresentKHR(queue,&present),"present");if(!automatic)slcheck(marker(sl::PCLMarker::ePresentEnd,*token),"present end");
                slcheck(getState(viewport,state,nullptr),"frame state");if(local>=runFrames-24)actual+=state.numFramesActuallyPresented;status|=unsigned(state.status);
                if(automatic && local>=32 && local<44){printf("game_input_gap local=%u actual=%u status=%u\n",local,state.numFramesActuallyPresented,unsigned(state.status));require(state.numFramesActuallyPresented<=1,"FG used stale inputs during SR gap");}
            }
            printf("present_measurement requested_multiplier=%u app_frames=24 actual_presented=%llu status_or=%u synthetic=1\n",generated+1,(unsigned long long)actual,status);
            require(status==0,"FG runtime status nonzero");
#if defined(D18_PROBE_DEPTH_REVERSE) || defined(D18_PROBE_INTERMITTENT)
            require(actual==24,"invalid or intermittent inputs must not generate");
#else
            if(generated&&actual<=24)allMultipliersObserved=false;
#endif
        }
        require(allMultipliersObserved,"generated presentation not observed for all requested multipliers");
    } catch(...) {cleanup();throw;}
    cleanup();
}
