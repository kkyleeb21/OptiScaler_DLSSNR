#define NOMINMAX
#include "NativeTimingVk.h"
#include <cstdio>
static unsigned reads=0;
static VkResult marker=VK_EVENT_RESET;
static uint64_t startTick=100,endTick=200,available=1;
extern "C" VkResult VKAPI_CALL vkGetEventStatus(VkDevice,VkEvent){return marker;}
extern "C" void VKAPI_CALL vkDestroyEvent(VkDevice,VkEvent,const VkAllocationCallbacks*){}
extern "C" void VKAPI_CALL vkDestroyQueryPool(VkDevice,VkQueryPool,const VkAllocationCallbacks*){}
extern "C" VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice,VkQueryPool,uint32_t,uint32_t,size_t,void* data,VkDeviceSize,VkQueryResultFlags flags){
    if(flags&VK_QUERY_RESULT_WAIT_BIT)return VK_ERROR_DEVICE_LOST;
    ++reads;auto p=static_cast<uint64_t*>(data);p[0]=startTick;p[1]=available;p[2]=endTick;p[3]=available;return VK_SUCCESS;
}
#define CHECK(x) if(!(x)){printf("FAIL line=%d\n",__LINE__);return 1;}
int main(){
    namespace A=DlssNr::VkAudit;
    A::completionSamples.push_back({(VkCommandBuffer)1,(VkDevice)1,7});
    DlssNr::NativeTimingVk meter;meter.device=(VkDevice)1;meter.pool=(VkQueryPool)1;meter.period=1000;
    meter.executed[0]=(VkEvent)1;
    auto arm=[&]{meter.slots[0]={{0,7},32,true,true};meter.lastMs.reset();};
    arm();meter.Poll(GetTickCount64());CHECK(reads==0&&meter.slots[0].pending); // still executable
    A::completionSamples[0].reported=true;
    arm();meter.Poll(GetTickCount64());CHECK(reads==0&&!meter.Value()); // abandoned, old queries available
    marker=VK_EVENT_SET;arm();meter.slots[0].sealed=false;meter.Poll(GetTickCount64());CHECK(reads==0);
    arm();meter.Poll(GetTickCount64());CHECK(reads==1&&meter.Value()&&std::abs(*meter.Value()-0.1)<1e-9);
    available=0;arm();meter.Poll(GetTickCount64());CHECK(reads==2&&!meter.Value());
    available=1;startTick=0xfffffff0;endTick=0x10;arm();meter.Poll(GetTickCount64());CHECK(std::abs(*meter.Value()-0.032)<1e-9);
    A::completionSamples[0].epoch=8;marker=VK_EVENT_RESET;arm();meter.Poll(GetTickCount64());CHECK(reads==3&&!meter.Value());
    marker=VK_EVENT_SET;arm();meter.Poll(GetTickCount64());CHECK(reads==4&&meter.Value()); // recycled lease, this GPU marker still proves execution
    meter.lastRead=GetTickCount64()-6000;CHECK(!meter.Value());
    puts("PASS pending/discarded/unsealed/unavailable/wrap/recycled/stale; query reads nonblocking");
}
