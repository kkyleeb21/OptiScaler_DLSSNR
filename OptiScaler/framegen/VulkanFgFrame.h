#pragma once
#include "VulkanFgResourceState.h"
#include "VulkanFgInputPolicy.h"
#include "VulkanFgSubmissionOrder.h"
#include "VulkanFgReadiness.h"
#include <proxies/Streamline_Proxy.h>
#include <dlssnr/D24VkTracking.h>
#include <DirectXMath.h>
#include <atomic>

// Experimental, one rendering device/viewport. Volatile tags let SL retain its
// own snapshots. Game resources are never kept as immutable Present-time tags.
namespace VulkanFg::Frame
{
inline std::mutex mutex;
inline std::atomic<unsigned> maximum{0},actual{0},status{0};
inline std::atomic<const char*> reason{"Waiting for SR inputs"};
inline unsigned frameId=0,mode=0;
inline bool faulted=false;
inline bool reflexOwned=false;
inline Readiness readiness{};
struct ViewLease
{
    VkDevice device{}; VkImage image{}; VkImageView view{}; uint64_t generation{},feature{};
    VkImageLayout readLayout=VK_IMAGE_LAYOUT_UNDEFINED;
};
inline std::array<ViewLease,32> depthViews{};
struct Pending
{
    VkDevice device{}; VkCommandBuffer command{}; VkCommandPool pool{}; VkQueue queue{};
    VkCommandBuffer depthCommand{}; VkCommandPool depthPool{};
    SubmissionOrder order{};
    sl::FrameToken* token{}; sl::Resource depth{}; sl::Extent extent{};
    uint64_t generation{},feature{}; unsigned submissions=0; VkImageAspectFlags depthAspect=VK_IMAGE_ASPECT_DEPTH_BIT;
    bool tagged=false,depthTagged=false,invalid=false,srRecorded=false;
};
inline Pending pending{};
struct ExpectedDepth
{
    VkDevice device{};NVSDK_NGX_Resource_VK input{};uint64_t feature{},generation{};
};
inline ExpectedDepth expectedDepth{};
inline bool Requested(){static const bool value=Config::Instance()->FGVulkanExperimental.value_or_default();return value;}
inline bool Enabled(){return Requested() && Config::Instance()->FGEnabled.value_or_default();}
inline bool Check(sl::Result result,const char* step)
{
    if(result==sl::Result::eOk)return true;
    reason.store(step);status.store(unsigned(result));
    return false;
}
inline bool PrepareFrameToken(unsigned id)
{
    // CompleteVulkanDevice establishes these exact Off defaults for our own
    // interposer; PrepareVulkan rejects an already loaded game interposer.
    // Do not apply this policy to native game FG or other API routes.
    if(!reflexOwned)
    {
        sl::ReflexOptions options{};options.mode=sl::ReflexMode::eLowLatency;
        if(!Check(StreamlineProxy::ReflexSetOptions()(options),"FG Reflex enable failed"))return false;
        reflexOwned=true;
        DlssNr::VkAudit::Write("event=fg_vulkan_reflex enabled=1 result=0");
    }
    if(!Check(StreamlineProxy::GetNewFrameToken()(pending.token,&id),"FG frame token failed") || !pending.token)return false;
    // First observed render-input boundary, once per token. This is not a
    // measured game simulation boundary and must not feed a PC latency claim.
    return Check(StreamlineProxy::ReflexSleep()(*pending.token),"FG Reflex sleep failed");
}
inline void Off(const char* why)
{
    readiness.Reset();
    reason.store(why);actual.store(0);
    static ULONGLONG logged=0;const auto now=GetTickCount64();
    if(Enabled() && now-logged>=1000){logged=now;DlssNr::VkAudit::Write("event=fg_vulkan_gate reason=%s",why);}
    if(mode && StreamlineProxy::IsVulkanInited())
    {
        sl::DLSSGOptions options{};options.mode=sl::DLSSGMode::eOff;
        if(Check(StreamlineProxy::DLSSGSetOptions()(sl::ViewportHandle(0),options),"FG Off failed"))mode=0;
    }
    if(reflexOwned && !mode && (!Enabled() || faulted) && StreamlineProxy::IsVulkanInited())
    {
        sl::ReflexOptions options{};
        const auto result=StreamlineProxy::ReflexSetOptions()(options);
        if(Check(result,"FG Reflex restore failed"))reflexOwned=false;
        DlssNr::VkAudit::Write("event=fg_vulkan_reflex enabled=0 result=%u",unsigned(result));
    }
}
inline void Marker(sl::PCLMarker marker)
{
    if(pending.token && StreamlineProxy::PCLSetMarker())
        if(!Check(StreamlineProxy::PCLSetMarker()(marker,*pending.token),"FG marker failed"))pending.invalid=true;
}
inline bool Describe(VkDevice device,const NVSDK_NGX_Resource_VK* value,sl::Resource& result,bool snapshot)
{
    if(!value || value->Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW)return false;
    const auto& v=value->Resource.ImageViewInfo;
    if(!v.Image || !v.ImageView || v.SubresourceRange.baseMipLevel || v.SubresourceRange.baseArrayLayer ||
        v.SubresourceRange.levelCount!=1 || v.SubresourceRange.layerCount!=1)return false;
    std::lock_guard lock(Resources::mutex);
    const auto found=Resources::images.find(v.Image);
    if(found==Resources::images.end())return false;
    const auto& image=found->second;
    // SL volatile copies use the complete first mip/layer. Reject subrects,
    // aliases, and unsupported usages instead of copying beyond a valid input.
    if(image.extent.width!=v.Width || image.extent.height!=v.Height || image.extent.depth!=1 || image.mips!=1 ||
        image.layers!=1 || image.format!=v.Format || !v.Width || !v.Height)return false;
    // Output is metadata only: FG gets final colour from its swapchain. Do not
    // impose sample/copy usage requirements on a texture we never read.
    if(snapshot && ((image.flags&VK_IMAGE_CREATE_ALIAS_BIT) ||
        !(image.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT) || !(image.usage&VK_IMAGE_USAGE_SAMPLED_BIT)))return false;
    result=sl::Resource(sl::ResourceType::eTex2d,(void*)v.Image,nullptr,(void*)v.ImageView,UINT_MAX);
    result.width=v.Width;result.height=v.Height;result.nativeFormat=v.Format;
    result.mipLevels=result.arrayLayers=1;result.flags=image.flags;result.usage=image.usage;
    return true;
}
inline ViewLease* DepthView(VkDevice device,const NVSDK_NGX_Resource_VK& input,uint64_t feature)
{
    const auto& v=input.Resource.ImageViewInfo;
    uint64_t generation=0;
    {std::lock_guard lock(Resources::mutex);auto it=Resources::images.find(v.Image);if(it==Resources::images.end())return nullptr;generation=it->second.generation;}
    for(auto& view:depthViews)if(view.image==v.Image && view.generation==generation && view.feature==feature)return &view;
    auto empty=std::find_if(depthViews.begin(),depthViews.end(),[](auto& view){return !view.image;});
    if(empty==depthViews.end())return nullptr;
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image=v.Image;info.format=v.Format;info.viewType=VK_IMAGE_VIEW_TYPE_2D;info.subresourceRange=v.SubresourceRange;
    // Linear depth may be an R16/R32 colour texture. Only strip stencil when
    // the source actually has a depth aspect; never create a depth view of R32.
    if(v.SubresourceRange.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT)info.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    else if(v.SubresourceRange.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT)return nullptr;
    VkImageView view{};
    auto create=(PFN_vkCreateImageView)VulkanHooks::NativeDeviceProc(device,"vkCreateImageView");
    if(!create || create(device,&info,nullptr,&view)!=VK_SUCCESS)return nullptr;
    *empty={device,v.Image,view,generation,feature,VK_IMAGE_LAYOUT_UNDEFINED};return &*empty;
}
inline bool TagDepth(VkImageLayout layout,VkCommandBuffer command)
{
    if(!Resources::SampledLayout(layout) || !pending.token)return false;
    VkCommandPool pool{};
    {std::lock_guard tracking(DlssNr::VkAudit::trackingMutex);
        const auto it=DlssNr::VkAudit::recordings.find(command);
        if(it==DlssNr::VkAudit::recordings.end() || it->second.device!=pending.device || !it->second.epoch ||
            it->second.level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY)return false;
        pool=it->second.pool;}
    pending.depth.state=layout;
    sl::ResourceTag tag{&pending.depth,sl::kBufferTypeDepth,sl::eOnlyValidNow,&pending.extent};
    pending.depthTagged=Check(StreamlineProxy::SetTagForFrame()(*pending.token,sl::ViewportHandle(0),&tag,1,
        (sl::CommandBuffer*)command),"Depth snapshot failed");
    if(pending.depthTagged){pending.depthCommand=command;pending.depthPool=pool;}
    return pending.depthTagged;
}
inline VkImageLayout RecordedReadLayout(VkCommandBuffer command,const NVSDK_NGX_Resource_VK& input)
{
    std::lock_guard lock(DlssNr::VkAudit::trackingMutex);
    const auto it=DlssNr::VkAudit::recordings.find(command);if(it==DlssNr::VkAudit::recordings.end())return VK_IMAGE_LAYOUT_UNDEFINED;
    const auto& v=input.Resource.ImageViewInfo;
    for(size_t n=0;n<it->second.barriers.size();++n)
    {
        const auto& b=it->second.barriers.newest(n);if(b.image!=v.Image)continue;
        if(b.range.baseMipLevel || b.range.baseArrayLayer || (b.range.aspectMask&v.SubresourceRange.aspectMask)!=v.SubresourceRange.aspectMask ||
            b.sourceFamily!=VK_QUEUE_FAMILY_IGNORED || b.destinationFamily!=VK_QUEUE_FAMILY_IGNORED)return VK_IMAGE_LAYOUT_UNDEFINED;
        return Resources::SampledLayout(b.layout)?b.layout:VK_IMAGE_LAYOUT_UNDEFINED;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}
inline void Record(VkDevice device,VkCommandBuffer command,NVSDK_NGX_Parameter* params,uint64_t feature,
    int flags,const Resources::Observation& observation)
{
    std::lock_guard lock(mutex);
    if(!Enabled() || !StreamlineProxy::IsVulkanInited()){pending={};Off("FG Off");return;}
    if(faulted){Off("Runtime fault; switch FG Off before retry");return;}
    if(pending.srRecorded){pending.invalid=true;Off("Multiple SR evaluations before Present");return;}
    auto reject=[](const char* text){pending={};Off(text);};
    if(flags<0){reject("SR feature flags missing");return;}
    NVSDK_NGX_Resource_VK* input[3]{};
    const char* keys[]={NVSDK_NGX_Parameter_Depth,NVSDK_NGX_Parameter_MotionVectors,NVSDK_NGX_Parameter_Output};
    sl::Resource resources[3];
    for(unsigned n=0;n<3;++n)
        if(params->Get(keys[n],(void**)&input[n])!=NVSDK_NGX_Result_Success || !Describe(device,input[n],resources[n],n<2))
        {reject("FG input extent, usage or metadata unavailable");return;}
    for(const char* key:{"DLSS.Input.Depth.Subrect.Base.X","DLSS.Input.Depth.Subrect.Base.Y",
        "DLSS.Input.MV.Subrect.Base.X","DLSS.Input.MV.Subrect.Base.Y","DLSS.Output.Subrect.Base.X","DLSS.Output.Subrect.Base.Y"})
    {unsigned base=0;if(params->Get(key,&base)==NVSDK_NGX_Result_Success && base){reject("FG subrect not supported");return;}}
    auto query=[&](const char* key){QueriedFloat v;v.supplied=params->Get(key,&v.value)==NVSDK_NGX_Result_Success;return v;};
    const auto motion=NormalizeMotion(query(NVSDK_NGX_Parameter_MV_Scale_X),query(NVSDK_NGX_Parameter_MV_Scale_Y),resources[1].width,resources[1].height);
    const auto motionLayout=observation.guides[1].known && Resources::StillAlive(observation.guides[1])?
        observation.guides[1].layout:RecordedReadLayout(command,*input[1]);
    if(!motion.valid || !Resources::SampledLayout(motionLayout))
    {reject("Motion-vector read contract unavailable");return;}
    auto& config=*Config::Instance();CameraInput cameraInput;
    cameraInput.nearPlane=query("FSR.cameraNear");cameraInput.farPlane=query("FSR.cameraFar");cameraInput.verticalFovRadians=query("FSR.cameraFovAngleVertical");
    cameraInput.useGameValues=config.FsrUseFsrInputValues.value_or_default();cameraInput.configuredNear=config.FsrCameraNear.value_or_default();cameraInput.configuredFar=config.FsrCameraFar.value_or_default();
    cameraInput.configuredVerticalDegrees=config.FsrVerticalFov.value_or_default();cameraInput.configuredHorizontalDegrees=config.FsrHorizontalFov.value_or_default();cameraInput.verticalOverride=config.FsrVerticalFov.has_value();
    cameraInput.depthInverted=(flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)!=0;
    cameraInput.outputWidth=resources[2].width;cameraInput.outputHeight=resources[2].height;
    const auto camera=ResolveCamera(cameraInput);if(!camera.valid){reject("Camera constants unavailable");return;}
    auto view=DepthView(device,*input[0],feature);if(!view){reject("Depth view unavailable");return;}
    expectedDepth={device,*input[0],feature,view->generation};
    // A depth command may be recorded before the SR call on the CPU. Keep its
    // frame token only when this call confirms the exact feature/image identity.
    const bool earlyDepth=pending.token && pending.depthTagged && !pending.invalid &&
        pending.device==device && pending.feature==feature && pending.generation==view->generation &&
        pending.depth.native==(void*)input[0]->Resource.ImageViewInfo.Image;
    if(!earlyDepth)pending={};
    pending.srRecorded=true;pending.device=device;pending.command=command;pending.feature=feature;pending.generation=view->generation;
    {std::lock_guard tracking(DlssNr::VkAudit::trackingMutex);auto recording=DlssNr::VkAudit::recordings.find(command);
        if(recording==DlssNr::VkAudit::recordings.end() || recording->second.device!=device || recording->second.level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY)
        {reject("Primary command recording unavailable");return;}pending.pool=recording->second.pool;}
    pending.depth=resources[0];pending.depth.view=(void*)view->view;pending.extent={0,0,resources[0].width,resources[0].height};
    pending.depthAspect=(input[0]->Resource.ImageViewInfo.SubresourceRange.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT)?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
    unsigned id=pending.token?frameId-1:frameId++;
    if(!pending.token && !PrepareFrameToken(id)){faulted=true;reject("FG frame/Reflex setup failed");return;}
    sl::Constants constants{};
    using namespace DirectX;
    auto matrix=[](sl::float4x4& target,FXMMATRIX value){XMFLOAT4X4 temp;XMStoreFloat4x4(&temp,value);memcpy(&target,&temp,sizeof(target));};
    const auto projection=XMMatrixPerspectiveFovRH(camera.verticalFovRadians,camera.aspect,camera.nearPlane,camera.farPlane);
    matrix(constants.cameraViewToClip,projection);matrix(constants.clipToCameraView,XMMatrixInverse(nullptr,projection));
    matrix(constants.clipToLensClip,XMMatrixIdentity());matrix(constants.clipToPrevClip,XMMatrixIdentity());matrix(constants.prevClipToClip,XMMatrixIdentity());
    constants.cameraNear=camera.nearPlane;constants.cameraFar=camera.farPlane;constants.cameraFOV=camera.verticalFovRadians;constants.cameraAspectRatio=camera.aspect;
    constants.cameraPos={0,0,0};constants.cameraUp={0,1,0};constants.cameraRight={1,0,0};constants.cameraFwd={0,0,-1};constants.cameraPinholeOffset={0,0};
    const auto jx=query(NVSDK_NGX_Parameter_Jitter_Offset_X),jy=query(NVSDK_NGX_Parameter_Jitter_Offset_Y);
    if(!jx.supplied || !jy.supplied || !std::isfinite(jx.value) || !std::isfinite(jy.value)){reject("Jitter unavailable");return;}
    constants.jitterOffset={jx.value,jy.value};constants.mvecScale={motion.x,motion.y};
    unsigned reset=0;params->Get(NVSDK_NGX_Parameter_Reset,&reset);
    constants.reset=reset || !mode || mode!=unsigned(config.FGDLSSGInterpolationCount.value_or_default())?sl::eTrue:sl::eFalse;constants.depthInverted=cameraInput.depthInverted?sl::eTrue:sl::eFalse;
    constants.cameraMotionIncluded=sl::eTrue;constants.motionVectors3D=sl::eFalse;constants.orthographicProjection=sl::eFalse;
    constants.motionVectorsDilated=(flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)?sl::eFalse:sl::eTrue;
    constants.motionVectorsJittered=(flags&NVSDK_NGX_DLSS_Feature_Flags_MVJittered)?sl::eTrue:sl::eFalse;
    Marker(sl::PCLMarker::eSimulationStart);Marker(sl::PCLMarker::eSimulationEnd);
    if(!Check(StreamlineProxy::SetConstants()(constants,*pending.token,sl::ViewportHandle(0)),"FG constants failed")){pending.invalid=true;return;}
    resources[1].state=motionLayout;
    // SR output need not match the final backbuffer's tone mapping or colour
    // space. Leave optional HUDless/UI inputs absent until their semantics can
    // be established; Streamline owns the final-colour presentation path.
    sl::Extent mvExtent{0,0,resources[1].width,resources[1].height},colourExtent{0,0,resources[2].width,resources[2].height};
    sl::ResourceTag tags[]={{&resources[1],sl::kBufferTypeMotionVectors,sl::eOnlyValidNow,&mvExtent},
        {nullptr,sl::kBufferTypeHUDLessColor,sl::eValidUntilPresent,&colourExtent},
        {nullptr,sl::kBufferTypeUIColorAndAlpha,sl::eValidUntilPresent,&colourExtent}};
    pending.tagged=Check(StreamlineProxy::SetTagForFrame()(*pending.token,sl::ViewportHandle(0),tags,3,(sl::CommandBuffer*)command),"FG motion snapshot failed");
    // Use this evaluation's observed contract only. Another command's layout
    // and previous frames' read boundaries do not establish the SR layout.
    const auto recordedDepth=RecordedReadLayout(command,*input[0]);
    static ULONGLONG inputLogged=0;const auto inputNow=GetTickCount64();
    if(inputNow-inputLogged>=1000){inputLogged=inputNow;
        DlssNr::VkAudit::Write("event=fg_vulkan_depth_input frame=%u cmd=%p image=%p generation=%llu recorded_layout=%d cached_layout=%d observed_layout=%d observed_known=%d descriptor_layout=%d aspect=%u early_depth=%d",
            id,(void*)command,pending.depth.native,(unsigned long long)pending.generation,int(recordedDepth),int(view->readLayout),
            int(observation.guides[0].layout),observation.guides[0].known,int(observation.guides[0].descriptorLayout),unsigned(pending.depthAspect),earlyDepth);}
    if(!pending.depthTagged && Resources::SampledLayout(recordedDepth))TagDepth(recordedDepth,command);
    else if(!pending.depthTagged && observation.guides[0].known && !observation.guides[0].ownershipConflict && Resources::StillAlive(observation.guides[0]))
        TagDepth(observation.guides[0].layout,command);
    reason.store(pending.depthTagged?"Inputs recorded; awaiting Present":"Waiting for depth read contract");
}
template<class Barrier> inline void BeforeBarriers(VkCommandBuffer command,uint32_t count,const Barrier* barriers)
{
    if(!Enabled() || !barriers)return;
    std::lock_guard lock(mutex);
    if(faulted)return;
    if(!pending.token && expectedDepth.device)
    {
        const auto& v=expectedDepth.input.Resource.ImageViewInfo;
        bool alive=false;
        {std::lock_guard resourceLock(Resources::mutex);auto it=Resources::images.find(v.Image);
            alive=it!=Resources::images.end() && it->second.generation==expectedDepth.generation;}
        if(alive)for(uint32_t i=0;i<count;++i)
        {
            const auto& b=barriers[i];
            if(b.image!=v.Image || !Resources::SampledLayout(b.oldLayout) || b.newLayout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)continue;
            {std::lock_guard tracking(DlssNr::VkAudit::trackingMutex);auto it=DlssNr::VkAudit::recordings.find(command);
                if(it==DlssNr::VkAudit::recordings.end() || it->second.device!=expectedDepth.device ||
                    !it->second.epoch || it->second.level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY)break;}
            sl::Resource depth;
            if(!Describe(expectedDepth.device,&expectedDepth.input,depth,true))break;
            auto view=DepthView(expectedDepth.device,expectedDepth.input,expectedDepth.feature);if(!view)break;
            pending={};pending.device=expectedDepth.device;pending.feature=expectedDepth.feature;pending.generation=expectedDepth.generation;
            pending.depth=depth;pending.depth.view=(void*)view->view;pending.extent={0,0,depth.width,depth.height};
            pending.depthAspect=(v.SubresourceRange.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT)?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
            unsigned id=frameId++;
            if(!PrepareFrameToken(id)){pending={};faulted=true;Off("Early depth/Reflex setup failed");}
            break;
        }
    }
    if(!pending.token || pending.depthTagged)return;
    // Observe the resource's real read boundary. Cross-command snapshots are
    // accepted for FG only after actual queue submission proves depth -> SR.
    static unsigned barrierRecords=0;
    for(uint32_t i=0;i<count && barrierRecords<96;++i)
    {
        const auto& b=barriers[i];if((void*)b.image!=pending.depth.native)continue;
        if(Config::Instance()->DlssNrDiagnostics.value_or_default()!=0){++barrierRecords;
            DlssNr::VkAudit::Write("event=fg_vulkan_depth_barrier frame=%u cmd=%p sr_cmd=%p same_cmd=%d old_layout=%d new_layout=%d aspect=%u source_family=%u destination_family=%u invalid=%d submissions=%u",
                frameId-1,(void*)command,(void*)pending.command,command==pending.command,int(b.oldLayout),int(b.newLayout),
                unsigned(b.subresourceRange.aspectMask),b.srcQueueFamilyIndex,b.dstQueueFamilyIndex,pending.invalid,pending.submissions);}
    }
    if(pending.invalid)return;
    for(uint32_t i=0;i<count;++i)
    {
        const auto& b=barriers[i];if((void*)b.image!=pending.depth.native)continue;
        // Do not insert a cross-command copy at a same-layout barrier which
        // could be inside a render pass. This route captures read -> attachment.
        if(command!=pending.command && b.newLayout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)continue;
        // SL's image copy covers both aspects of packed depth/stencil formats.
        const auto requiredAspect=(pending.depth.nativeFormat==VK_FORMAT_D24_UNORM_S8_UINT ||
            pending.depth.nativeFormat==VK_FORMAT_D32_SFLOAT_S8_UINT || pending.depth.nativeFormat==VK_FORMAT_D16_UNORM_S8_UINT)?
            VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT):pending.depthAspect;
        if(!Resources::SampledLayout(b.oldLayout) || b.srcQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED ||
            b.dstQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED || (b.subresourceRange.aspectMask&requiredAspect)!=requiredAspect ||
            b.subresourceRange.baseMipLevel || b.subresourceRange.baseArrayLayer || !b.subresourceRange.levelCount || !b.subresourceRange.layerCount)
        {if(pending.command==command)pending.invalid=true;continue;}
        // Do not reuse this boundary as a future frame's SR layout contract.
        // The application can return depth to attachment layout afterwards.
        TagDepth(b.oldLayout,command);return;
    }
}
inline VkCommandBuffer Command(const VkSubmitInfo& info,uint32_t n){return info.pCommandBuffers[n];}
inline VkCommandBuffer Command(const VkSubmitInfo2& info,uint32_t n){return info.pCommandBufferInfos[n].commandBuffer;}
inline uint32_t Count(const VkSubmitInfo& info){return info.commandBufferCount;}
inline uint32_t Count(const VkSubmitInfo2& info){return info.commandBufferInfoCount;}
template<class Submit> inline void Submitted(VkQueue queue,uint32_t count,const Submit* infos,VkResult result)
{
    if(!Requested())return;
    std::lock_guard lock(mutex);if(!pending.token || !infos)return;
    for(uint32_t i=0;i<count;++i)for(uint32_t n=0;n<Count(infos[i]);++n)
    {
        const auto command=Command(infos[i],n);
        pending.order.Submit((uintptr_t)queue,command==pending.depthCommand,command==pending.command,result==VK_SUCCESS);
        if(command==pending.command)
        {++pending.submissions;pending.queue=queue;pending.invalid|=result!=VK_SUCCESS;Marker(sl::PCLMarker::eRenderSubmitEnd);}
    }
}
template<class Submit> inline void Submitting(uint32_t count,const Submit* infos)
{
    if(!Requested())return;
    std::lock_guard lock(mutex);if(!pending.token || !infos)return;
    for(uint32_t i=0;i<count;++i)for(uint32_t n=0;n<Count(infos[i]);++n)if(Command(infos[i],n)==pending.command)
        Marker(sl::PCLMarker::eRenderSubmitStart);
}
inline void Invalidate(VkCommandBuffer command)
{if(!Requested())return;std::lock_guard lock(mutex);if(pending.command==command || pending.depthCommand==command)pending.invalid=true;}
inline void InvalidatePool(VkCommandPool pool)
{if(!Requested())return;std::lock_guard lock(mutex);if(pending.pool==pool || pending.depthPool==pool)pending.invalid=true;}
inline void InvalidateFrame(){std::lock_guard lock(mutex);pending.invalid=true;}
inline void BeforePresent(VkQueue queue)
{
    std::lock_guard lock(mutex);
    if(!StreamlineProxy::IsVulkanInited())return;
    if(!Enabled()){Off("FG Off");faulted=false;status.store(0);return;}
    if(faulted){Off("Runtime fault; switch FG Off before retry");return;}
    static ULONGLONG presentLogged=0;const auto presentNow=GetTickCount64();
    if(presentNow-presentLogged>=1000){presentLogged=presentNow;
        DlssNr::VkAudit::Write("event=fg_vulkan_input_gate token=%d tagged=%d depth=%d invalid=%d submissions=%u same_queue=%d depth_submissions=%u depth_before_sr=%d depth_same_cmd=%d order_invalid=%d",
            pending.token!=nullptr,pending.tagged,pending.depthTagged,pending.invalid,pending.submissions,pending.queue==queue,
            pending.order.depthCount,pending.order.Ready((uintptr_t)queue),pending.depthCommand==pending.command,pending.order.invalid);}
    if(pending.token && pending.tagged && !pending.depthTagged && !pending.invalid && pending.submissions==1 && pending.queue==queue)
    {Off("Depth snapshot unavailable; see diagnostics");return;}
    if(!pending.token || !pending.tagged || !pending.depthTagged || pending.invalid || pending.submissions!=1 || pending.queue!=queue)
    {Off("Incomplete or ambiguous frame inputs");return;}
    if(!pending.order.Ready((uintptr_t)queue)){Off("Depth submission must precede SR on the Present queue");return;}
    if(!readiness.Observe(true)){reason.store("Waiting for 32 consecutive valid frames");return;}
    sl::DLSSGState state{};
    if(!Check(StreamlineProxy::DLSSGGetState()(sl::ViewportHandle(0),state,nullptr),"FG capability query failed")){Off("FG query failed");return;}
    maximum.store(std::min(state.numFramesToGenerateMax,5u));
    if(!maximum.load()){Off("FG unsupported by runtime");return;}
    const unsigned requested=std::clamp(unsigned(Config::Instance()->FGDLSSGInterpolationCount.value_or_default()),1u,maximum.load());
    if(mode!=requested)
    {
        sl::DLSSGOptions options{};options.mode=sl::DLSSGMode::eOn;options.numFramesToGenerate=requested;
        if(!Check(StreamlineProxy::DLSSGSetOptions()(sl::ViewportHandle(0),options),"FG options failed")){Off("FG options failed");return;}
        // The owned interposer's Reflex policy is established at token start.
        mode=requested;
    }
    Marker(sl::PCLMarker::ePresentStart);
    if(pending.invalid)Off("FG marker failed");
}
inline void AfterPresent(VkResult result)
{
    std::lock_guard lock(mutex);
    if(pending.token && mode)
    {
        Marker(sl::PCLMarker::ePresentEnd);sl::DLSSGState state{};
        const auto query=StreamlineProxy::DLSSGGetState()(sl::ViewportHandle(0),state,nullptr);
        if(Check(query,"FG state readback failed"))
        {actual.store(state.numFramesActuallyPresented);status.store(unsigned(state.status));reason.store(unsigned(state.status)?"FG runtime error":state.numFramesActuallyPresented>1?"Generated frames observed":"Waiting for generated frames");}
        static ULONGLONG logged=0;
        const auto now=GetTickCount64();
        if(now-logged>=1000){logged=now;DlssNr::VkAudit::Write("event=fg_vulkan_frame requested=%u actual=%u status=%u query=%u present=%d depth_snapshot=%d camera=approximate colour_source=swapchain hudless=0 volatile_inputs=2",mode+1,actual.load(),status.load(),unsigned(query),int(result),pending.depthTagged);}
        if(query!=sl::Result::eOk || unsigned(state.status)!=0 || (result!=VK_SUCCESS && result!=VK_SUBOPTIMAL_KHR))
        {
            // A first fault may occur between the periodic readback samples.
            // Emit it before latching Off so its status cannot disappear.
            DlssNr::VkAudit::Write("event=fg_vulkan_fault requested=%u actual=%u status=%u query=%u present=%d depth_snapshot=%d",
                mode+1,state.numFramesActuallyPresented,unsigned(state.status),unsigned(query),int(result),pending.depthTagged);
            faulted=true;Off("Runtime fault; switch FG Off before retry");
        }
    }
    pending={};
}
inline void DestroyImage(VkDevice device,VkImage image)
{
    std::lock_guard lock(mutex);
    if((void*)image==pending.depth.native)pending.invalid=true;
    if(expectedDepth.input.Resource.ImageViewInfo.Image==image)expectedDepth={};
    for(auto& view:depthViews)if(view.device==device && view.image==image)
    {((PFN_vkDestroyImageView)VulkanHooks::NativeDeviceProc(device,"vkDestroyImageView"))(device,view.view,nullptr);view={};}
}
inline void LogPacing(double controlUs,double presentUs,double readbackUs,VkResult result)
{
    if(Config::Instance()->DlssNrDiagnostics.value_or_default()==0)return;
    std::lock_guard lock(mutex);
    static ULONGLONG start=GetTickCount64();static unsigned frames=0,foregroundFrames=0;
    static double control=0,present=0,readback=0,maxPresent=0;
    DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
    ++frames;foregroundFrames+=foregroundPid==GetCurrentProcessId();
    control+=controlUs;present+=presentUs;readback+=readbackUs;maxPresent=std::max(maxPresent,presentUs);
    const auto now=GetTickCount64();if(now-start<1000)return;
    DlssNr::VkAudit::Write("event=fg_vulkan_pacing frames=%u foreground_frames=%u window_ms=%llu control_us=%.0f present_us=%.0f readback_us=%.0f max_present_us=%.0f enabled=%d active=%u ready=%u result=%d reflex_policy=owned",
        frames,foregroundFrames,(unsigned long long)(now-start),control,present,readback,maxPresent,Enabled(),mode,readiness.consecutive,int(result));
    start=now;frames=foregroundFrames=0;control=present=readback=maxPresent=0;
}
inline void Stop()
{
    std::lock_guard lock(mutex);faulted=true;Off("FG shutdown");pending={};
}
inline void DestroyDevice(VkDevice device)
{
    std::lock_guard lock(mutex);pending={};expectedDepth={};mode=0;faulted=false;reflexOwned=false;readiness.Reset();maximum.store(0);actual.store(0);
    for(auto& view:depthViews)if(view.device==device)
    {((PFN_vkDestroyImageView)VulkanHooks::NativeDeviceProc(device,"vkDestroyImageView"))(device,view.view,nullptr);view={};}
}
}
