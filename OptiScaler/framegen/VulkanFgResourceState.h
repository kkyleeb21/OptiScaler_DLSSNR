#pragma once
#include <array>
#include <mutex>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <algorithm>

namespace VulkanFg::Resources
{
struct Image
{
    VkImageUsageFlags usage{};
    VkImageCreateFlags flags{};
    VkExtent3D extent{};
    VkFormat format{};
    uint32_t mips{},layers{};
    uint64_t generation{};
};
struct View { VkImage image{}; VkFormat format{}; VkImageViewType type{}; VkImageSubresourceRange range{}; uint64_t generation{},viewGeneration{}; };
struct Guide
{
    VkImage image{}; VkImageView view{};
    Image description{}; View sampled{};
    VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout descriptorLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    bool known=false,ownershipConflict=false;
};
inline std::mutex mutex;
inline std::unordered_map<VkImage,Image> images;
inline std::unordered_map<VkImageView,View> views;
inline uint64_t generation=0;
struct Contract { uint64_t feature{}; Guide guide{}; };
inline std::array<Contract,16> contracts{};
inline size_t nextContract=0;

inline bool SampledLayout(VkImageLayout layout)
{
    return layout==VK_IMAGE_LAYOUT_GENERAL || layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL || layout==VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL ||
        layout==VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
}
inline bool ContainsRange(const VkImageSubresourceRange& outer,const VkImageSubresourceRange& inner,const Image& image)
{
    auto end=[](uint32_t base,uint32_t count,uint32_t total)->uint64_t {
        return count==UINT32_MAX ? total : uint64_t(base)+count;
    };
    return (outer.aspectMask&inner.aspectMask)==inner.aspectMask && outer.baseMipLevel<=inner.baseMipLevel &&
        outer.baseArrayLayer<=inner.baseArrayLayer &&
        end(outer.baseMipLevel,outer.levelCount,image.mips)>=end(inner.baseMipLevel,inner.levelCount,image.mips) &&
        end(outer.baseArrayLayer,outer.layerCount,image.layers)>=end(inner.baseArrayLayer,inner.layerCount,image.layers);
}
struct Observation
{
    VkCommandBuffer command{};
    uint64_t feature{};
    std::array<Guide,2> guides{};
    uint32_t descriptorWrites=0,barriers=0;
};
inline thread_local Observation* active=nullptr;
class Scope
{
    Observation* previous{};
public:
    Observation observation{};
    Scope(VkCommandBuffer command,uint64_t feature,const std::array<VkImage,2>& wanted)
    {
        observation.command=command;observation.feature=feature;
        std::lock_guard<std::mutex> lock(mutex);
        for(size_t n=0;n<wanted.size();++n)
        {
            auto& guide=observation.guides[n];guide.image=wanted[n];
            const auto image=images.find(wanted[n]);
            if(image==images.end())continue;
            guide.description=image->second;
            // A retained shader descriptor is an API usage contract, not a
            // globally inferred layout based on CPU command-recording order.
            for(const auto& cached:contracts)
                if(cached.feature==feature && cached.guide.image==wanted[n] &&
                   cached.guide.description.generation==image->second.generation && views.contains(cached.guide.view) &&
                   views.at(cached.guide.view).viewGeneration==cached.guide.sampled.viewGeneration)
                {
                    guide=cached.guide;guide.layout=guide.descriptorLayout;
                    guide.known=SampledLayout(guide.layout);guide.ownershipConflict=false;break;
                }
        }
        previous=active;active=&observation;
    }
    ~Scope()
    {
        active=previous;
        std::lock_guard<std::mutex> lock(mutex);
        for(const auto& guide:observation.guides)
            if(guide.view && SampledLayout(guide.descriptorLayout) && !guide.ownershipConflict)
            {
                auto slot=std::find_if(contracts.begin(),contracts.end(),[&](const auto& c){
                    return c.feature==observation.feature && c.guide.image==guide.image;
                });
                if(slot==contracts.end())slot=contracts.begin()+(nextContract++%contracts.size());
                *slot={observation.feature,guide};
            }
    }
    Scope(const Scope&)=delete;
};

inline void ObserveWrites(uint32_t count,const VkWriteDescriptorSet* writes)
{
    if(!active || !writes)return;
    std::lock_guard<std::mutex> lock(mutex);
    for(uint32_t i=0;i<count;++i)
    {
        const auto& write=writes[i];
        if(!write.pImageInfo || (write.descriptorType!=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
            write.descriptorType!=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
            write.descriptorType!=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE && write.descriptorType!=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT))continue;
        for(uint32_t j=0;j<write.descriptorCount;++j)
        {
            const auto& info=write.pImageInfo[j];const auto view=views.find(info.imageView);
            if(view==views.end())continue;
            for(auto& guide:active->guides)
                if(guide.image && guide.image==view->second.image && guide.description.generation==view->second.generation)
                {
                    guide.view=info.imageView;guide.sampled=view->second;
                    guide.descriptorLayout=info.imageLayout;guide.layout=info.imageLayout;
                    guide.known=SampledLayout(info.imageLayout);++active->descriptorWrites;
                }
        }
    }
}
template<class Barrier> inline void ObserveBarriers(VkCommandBuffer command,uint32_t count,const Barrier* values)
{
    if(!active || active->command!=command || !values)return;
    for(uint32_t i=0;i<count;++i)for(auto& guide:active->guides)
        if(guide.image && guide.image==values[i].image)
        {
            const auto& b=values[i];
            if(b.srcQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED && b.dstQueueFamilyIndex!=VK_QUEUE_FAMILY_IGNORED &&
               b.srcQueueFamilyIndex!=b.dstQueueFamilyIndex)guide.ownershipConflict=true;
            if(guide.view && ContainsRange(b.subresourceRange,guide.sampled.range,guide.description))
            {guide.layout=b.newLayout;guide.known=SampledLayout(b.newLayout);++active->barriers;}
        }
}
inline bool StillAlive(const Guide& guide)
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto image=images.find(guide.image);const auto view=views.find(guide.view);
    return image!=images.end() && view!=views.end() && image->second.generation==guide.description.generation &&
        view->second.generation==guide.description.generation && view->second.viewGeneration==guide.sampled.viewGeneration;
}
inline void Clear()
{
    std::lock_guard<std::mutex> lock(mutex);images.clear();views.clear();contracts={};
}

}
