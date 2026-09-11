#include "../../OptiScaler/framegen/VulkanFgResourceState.h"
#include <cassert>
#include <cstdio>
using namespace VulkanFg::Resources;
template<class T> T handle(uintptr_t value){return reinterpret_cast<T>(value);}
int main()
{
    const auto image=handle<VkImage>(1);const auto view=handle<VkImageView>(2);
    const auto cmd=handle<VkCommandBuffer>(3);const auto other=handle<VkCommandBuffer>(4);
    images[image]={VK_IMAGE_USAGE_SAMPLED_BIT,0,{1920,1080,1},VK_FORMAT_D32_SFLOAT,1,1,10};
    views[view]={image,VK_FORMAT_D32_SFLOAT,VK_IMAGE_VIEW_TYPE_2D,{VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1},10,11};
    VkDescriptorImageInfo sampled{};sampled.imageView=view;sampled.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.descriptorCount=1;
    write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.pImageInfo=&sampled;
    Guide captured;
    {
        Scope scope(cmd,7,{image,VK_NULL_HANDLE});
        assert(!scope.observation.guides[0].known);
        ObserveWrites(1,&write);assert(scope.observation.guides[0].known);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image;
        barrier.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,VK_REMAINING_MIP_LEVELS,0,VK_REMAINING_ARRAY_LAYERS};
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        ObserveBarriers(other,1,&barrier);
        assert(scope.observation.guides[0].layout==sampled.imageLayout);
        ObserveBarriers(cmd,1,&barrier);assert(scope.observation.guides[0].layout==VK_IMAGE_LAYOUT_GENERAL);
        captured=scope.observation.guides[0];assert(StillAlive(captured));
    }
    {
        Scope cached(cmd,7,{image,VK_NULL_HANDLE});assert(cached.observation.guides[0].known);
        assert(cached.observation.guides[0].layout==sampled.imageLayout);
        {Scope differentFeature(cmd,8,{image,VK_NULL_HANDLE});assert(!differentFeature.observation.guides[0].known);}
        assert(active==&cached.observation);
    }
    assert(active==nullptr);
    views[view].viewGeneration=12;
    assert(!StillAlive(captured));
    {Scope recreated(cmd,7,{image,VK_NULL_HANDLE});assert(!recreated.observation.guides[0].known);}
    {
        Scope transfer(cmd,9,{image,VK_NULL_HANDLE});ObserveWrites(1,&write);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image;
        barrier.subresourceRange=views[view].range;barrier.srcQueueFamilyIndex=0;barrier.dstQueueFamilyIndex=1;
        barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;ObserveBarriers(cmd,1,&barrier);
        assert(transfer.observation.guides[0].ownershipConflict);
    }
    {Scope rejected(cmd,9,{image,VK_NULL_HANDLE});assert(!rejected.observation.guides[0].known);}
    images[image].generation=20;
    {Scope reusedImage(cmd,7,{image,VK_NULL_HANDLE});ObserveWrites(1,&write);assert(!reusedImage.observation.guides[0].known);}
    Clear();assert(images.empty()&&views.empty());
    puts("PASS: descriptor contracts, same-command restoration, feature isolation, view/image reuse rejection, queue ownership rejection, nested scopes");
}
