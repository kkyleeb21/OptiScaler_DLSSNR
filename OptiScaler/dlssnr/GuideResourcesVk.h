#pragma once
#include "BuildProfile.h"
#include "D24VkTracking.h"
#include "GuideCapture.h"
#include <unordered_map>

namespace DlssNr::GuideResourcesVk {
struct Image {VkDevice device{};VkImageUsageFlags usage{};VkImageCreateFlags flags{};VkExtent3D extent{};VkFormat format{};VkSampleCountFlagBits samples{};uint32_t mips=0,layers=0;VkSharingMode sharing{};uint64_t generation=0;};
struct View {VkDevice device{};VkImage image{};VkFormat format{};VkImageViewType type{};VkImageSubresourceRange range{};VkComponentMapping components{};uint64_t imageGeneration=0,generation=0;};
inline std::mutex mutex;inline std::unordered_map<VkImage,Image> images;inline std::unordered_map<VkImageView,View> views;inline uint64_t generation=0,dropped=0;
inline bool Enabled(){return BuildProfile::PixelCapture&&Config::Instance()->DlssNrDiagnostics.value_or_default()!=0;}
inline std::string CoverageJson(){std::lock_guard lock(mutex);return "{\"tracking_enabled\":"+std::string(Enabled()?"true":"false")+",\"images_tracked\":"+std::to_string(images.size())+",\"views_tracked\":"+std::to_string(views.size())+",\"metadata_dropped\":"+std::to_string(dropped)+",\"coverage\":\"observed_creation_and_explicit_barriers_only\"}";}
inline void Created(VkDevice device,const VkImageCreateInfo& info,VkImage image){
    if(!Enabled())return;std::lock_guard lock(mutex);
    images.erase(image);if(images.size()>=8192){++dropped;return;}
    images[image]={device,info.usage,info.flags,info.extent,info.format,info.samples,info.mipLevels,info.arrayLayers,info.sharingMode,++generation};
}
inline void CreatedView(VkDevice device,const VkImageViewCreateInfo& info,VkImageView view){
    if(!Enabled())return;std::lock_guard lock(mutex);views.erase(view);const auto it=images.find(info.image);
    if(it==images.end()||it->second.device!=device)return;if(views.size()>=16384){++dropped;return;}
    views[view]={device,info.image,info.format,info.viewType,info.subresourceRange,info.components,it->second.generation,++generation};
}
inline void Destroyed(VkDevice device,VkImage image){if(!BuildProfile::PixelCapture)return;std::lock_guard lock(mutex);auto it=images.find(image);if(it!=images.end()&&it->second.device==device)images.erase(it);}
inline void DestroyedView(VkDevice device,VkImageView view){if(!BuildProfile::PixelCapture)return;std::lock_guard lock(mutex);auto it=views.find(view);if(it!=views.end()&&it->second.device==device)views.erase(it);}
inline void DestroyedDevice(VkDevice device){if(!BuildProfile::PixelCapture)return;std::lock_guard lock(mutex);std::erase_if(images,[&](auto& p){return p.second.device==device;});std::erase_if(views,[&](auto& p){return p.second.device==device;});}
inline bool CountFits(uint32_t first,uint32_t count,uint32_t wanted,uint32_t total){return first<=wanted&&wanted<total&&(count==VK_REMAINING_MIP_LEVELS||uint64_t(first)+count>wanted);}
struct Plan {capture::GuideEvidence evidence;VkImage image{};VkImageLayout layout{};VkImageSubresourceRange transition{};VkImageSubresourceLayers copy{};};
inline Plan Inspect(VkDevice device,VkCommandBuffer cmd,const NVSDK_NGX_Resource_VK* resource,const char* role,uint32_t outputW,uint32_t outputH,float sx,float sy){
    Plan p;auto& e=p.evidence;e.role=role;e.api="Vulkan";e.scaleX=sx;e.scaleY=sy;
    e.units=std::string(role).find("motion")==0?"raw_mv_times_scale_to_pixels":std::string(role).find("exposure")==0?"exposure_texture_scalar":"device_depth";
    auto fail=[&](const char* why){e.reason=why;return p;};
    if(!Enabled())return fail("diagnostic_metadata_tracking_disabled");
    if(!resource||resource->Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW)return fail("resource_missing_or_not_view");
    Image image;View view;{std::lock_guard lock(mutex);auto v=views.find(resource->Resource.ImageViewInfo.ImageView);
        if(v==views.end())return fail("view_creation_not_observed");view=v->second;auto i=images.find(view.image);
        if(i==images.end()||i->second.generation!=view.imageGeneration)return fail("image_creation_missing_or_reused");image=i->second;}
    e.sourceFormat=unsigned(image.format);e.resourceGeneration=image.generation;e.viewGeneration=view.generation;
    if(image.device!=device||view.device!=device||view.image!=resource->Resource.ImageViewInfo.Image)return fail("device_or_image_mismatch");
    if(!(image.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT))return fail("transfer_src_usage_absent");
    if(image.flags&(VK_IMAGE_CREATE_SPARSE_BINDING_BIT|VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT|VK_IMAGE_CREATE_PROTECTED_BIT))return fail("sparse_or_protected_resource");
    if(image.samples!=VK_SAMPLE_COUNT_1_BIT||image.extent.depth!=1||view.type!=VK_IMAGE_VIEW_TYPE_2D||image.sharing!=VK_SHARING_MODE_EXCLUSIVE)return fail("unsupported_samples_view_or_sharing");
    if(view.format!=image.format)return fail("view_format_reinterpretation");
    if((view.components.r!=VK_COMPONENT_SWIZZLE_IDENTITY&&view.components.r!=VK_COMPONENT_SWIZZLE_R)||(view.components.g!=VK_COMPONENT_SWIZZLE_IDENTITY&&view.components.g!=VK_COMPONENT_SWIZZLE_G)||(view.components.b!=VK_COMPONENT_SWIZZLE_IDENTITY&&view.components.b!=VK_COMPONENT_SWIZZLE_B)||(view.components.a!=VK_COMPONENT_SWIZZLE_IDENTITY&&view.components.a!=VK_COMPONENT_SWIZZLE_A))return fail("view_component_swizzle");
    if(view.range.baseMipLevel>=image.mips||view.range.baseArrayLayer>=image.layers)return fail("view_range_outside_image");
    const auto levels=view.range.levelCount==VK_REMAINING_MIP_LEVELS?image.mips-view.range.baseMipLevel:view.range.levelCount;
    const auto layers=view.range.layerCount==VK_REMAINING_ARRAY_LAYERS?image.layers-view.range.baseArrayLayer:view.range.layerCount;
    if(levels!=1||layers!=1)return fail("multi_subresource_view");
    e.sourceWidth=std::max(1u,image.extent.width>>view.range.baseMipLevel);e.sourceHeight=std::max(1u,image.extent.height>>view.range.baseMipLevel);
    if(e.sourceWidth!=resource->Resource.ImageViewInfo.Width||e.sourceHeight!=resource->Resource.ImageViewInfo.Height)return fail("wrapper_extent_mismatch");
    VkImageAspectFlags aspect=view.range.aspectMask,transitionAspect=aspect;VkFormat storage=view.format;
    switch(view.format){
    case VK_FORMAT_D16_UNORM:e.texelBytes=2;e.channels=1;break;
    case VK_FORMAT_D32_SFLOAT:e.texelBytes=4;e.channels=1;break;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:storage=VK_FORMAT_D32_SFLOAT;e.texelBytes=4;e.channels=1;transitionAspect=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;break;
    case VK_FORMAT_D24_UNORM_S8_UINT:case VK_FORMAT_X8_D24_UNORM_PACK32:storage=VK_FORMAT_X8_D24_UNORM_PACK32;e.texelBytes=4;e.channels=1;transitionAspect=view.format==VK_FORMAT_D24_UNORM_S8_UINT?VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT:VK_IMAGE_ASPECT_DEPTH_BIT;break;
    case VK_FORMAT_R32_SFLOAT:e.texelBytes=4;e.channels=1;break;
    case VK_FORMAT_R16_SFLOAT:case VK_FORMAT_R16_UNORM:e.texelBytes=2;e.channels=1;break;
    case VK_FORMAT_R16G16_SFLOAT:e.texelBytes=4;e.channels=2;break;
    case VK_FORMAT_R32G32_SFLOAT:e.texelBytes=8;e.channels=2;break;
    default:return fail("unsupported_format");
    }
    const bool depthFormat=storage==VK_FORMAT_D16_UNORM||storage==VK_FORMAT_D32_SFLOAT||storage==VK_FORMAT_X8_D24_UNORM_PACK32;
    if(aspect!=(depthFormat?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT))return fail("view_aspect_mismatch");
    e.storageFormat=unsigned(storage);e.aspect=aspect;e.mip=view.range.baseMipLevel;e.layer=view.range.baseArrayLayer;e.valid={0,0,e.sourceWidth,e.sourceHeight};e.roi=capture::GuideCrop(e.valid,outputW,outputH);
    {std::lock_guard lock(VkAudit::trackingMutex);auto r=VkAudit::recordings.find(cmd);if(r==VkAudit::recordings.end()||r->second.device!=device)return fail("recording_not_observed");bool found=false;
        if(r->second.level!=VK_COMMAND_BUFFER_LEVEL_PRIMARY||!r->second.epoch)return fail("primary_recording_begin_not_observed");
        for(size_t k=0;k<r->second.barriers.size();++k){const auto& b=r->second.barriers.newest(k);if(b.image!=view.image)continue;
            if(b.serial<=r->second.guideBarrierFloor)return fail("layout_observation_invalidated_by_opaque_command");
            if(!CountFits(b.range.baseMipLevel,b.range.levelCount,e.mip,image.mips)||!CountFits(b.range.baseArrayLayer,b.range.layerCount,e.layer,image.layers)||(b.range.aspectMask&transitionAspect)!=transitionAspect)return fail("latest_barrier_does_not_cover_view");
            if(b.sourceFamily!=VK_QUEUE_FAMILY_IGNORED||b.destinationFamily!=VK_QUEUE_FAMILY_IGNORED)return fail("queue_ownership_transfer_not_supported");
            if(b.layout!=VK_IMAGE_LAYOUT_GENERAL&&b.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL&&b.layout!=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)return fail("layout_not_readable");
            p.layout=b.layout;found=true;break;}
        if(!found)return fail("image_barrier_not_observed");}
    p.image=view.image;p.transition={transitionAspect,e.mip,1,e.layer,1};p.copy={aspect,e.mip,e.layer,1};e.reason="";return p;
}
}
