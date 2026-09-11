#include "D24VkTracking.h"
#include <cstring>
#define CHECK(x) if(!(x)){printf("FAIL line=%d\n",__LINE__);return 1;}
namespace A=DlssNr::VkAudit;
static bool ready=false;
static unsigned destroyed=0;
static VkResult VKAPI_CALL Create(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* out){*out=(VkFence)1;return VK_SUCCESS;}
static VkResult VKAPI_CALL Status(VkDevice,VkFence){return ready?VK_SUCCESS:VK_NOT_READY;}
static void VKAPI_CALL Destroy(VkDevice,VkFence,const VkAllocationCallbacks*){++destroyed;}
static VkResult VKAPI_CALL Idle(VkDevice){return VK_SUCCESS;}
static PFN_vkVoidFunction VKAPI_CALL Get(VkDevice,const char* name){
 if(!strcmp(name,"vkCreateFence"))return (PFN_vkVoidFunction)Create;
 if(!strcmp(name,"vkGetFenceStatus"))return (PFN_vkVoidFunction)Status;
 if(!strcmp(name,"vkDestroyFence"))return (PFN_vkVoidFunction)Destroy;
 return (PFN_vkVoidFunction)Idle;
}
int main(){
 CHECK(A::Enabled());
 auto device=(VkDevice)1;auto pool=(VkCommandPool)2;auto cmd=(VkCommandBuffer)3;auto queue=(VkQueue)4;
 A::RegisterDevice(device,Get);A::Allocate(device,pool,7,1,&cmd);A::Invalidate(cmd);
 A::Handoff(cmd,nullptr,nullptr,0);
 auto batch=A::Prepare({cmd});CHECK(batch.size()==1);
 // Reset racing the return from queue submission must not release the lease.
 A::Invalidate(cmd);CHECK(!A::completionSamples[0].reported);
 A::Submitted(queue,batch,VK_SUCCESS,[](VkFence){return VK_SUCCESS;});
 A::Invalidate(cmd);CHECK(!A::completionSamples[0].reported && !destroyed);
 ready=true;A::Invalidate(cmd);CHECK(A::completionSamples[0].reported && destroyed==1);
 // GPU completion alone does not make an executable recording safe to reuse.
 A::Handoff(cmd,nullptr,nullptr,0);batch=A::Prepare({cmd});
 A::Submitted(queue,batch,VK_SUCCESS,[](VkFence){return VK_SUCCESS;});
 {std::lock_guard<std::mutex> lock(A::trackingMutex);A::PollLocked();}
 CHECK(!A::completionSamples[1].reported && destroyed==2);
 A::InvalidatePool(pool,false);CHECK(A::completionSamples[1].reported);
 // Unsubmitted recordings become releasable only after invalidation.
 A::Handoff(cmd,nullptr,nullptr,0);CHECK(!A::completionSamples[2].reported);
 A::Invalidate(cmd);CHECK(A::completionSamples[2].reported && !A::completionSamples[2].submitted);
 // Submission errors remain unresolved, never a completion proof.
 A::Handoff(cmd,nullptr,nullptr,0);batch=A::Prepare({cmd});
 A::Submitted(queue,batch,VK_ERROR_DEVICE_LOST,[](VkFence){return VK_SUCCESS;});
 A::Invalidate(cmd);CHECK(A::completionSamples[3].failed && !A::completionSamples[3].reported);
 A::Free(1,&cmd);A::DestroyDevice(device);CHECK(A::recordings.empty());
 // Queue indices are opaque: accept compute family2, reject transfer-only0.
 A::RegisterDevice(device,Get);
 std::vector<VkQueueFamilyProperties> families(3);
 families[0].queueCount=1;families[0].queueFlags=VK_QUEUE_TRANSFER_BIT;
 families[2].queueCount=1;families[2].queueFlags=VK_QUEUE_COMPUTE_BIT;
 A::RegisterQueueFamilies(device,families);
 const char* reason=nullptr;
 A::Allocate(device,pool,2,1,&cmd);A::Invalidate(cmd);
 CHECK(A::Acquire(cmd,device,&reason).Valid() && !reason);
 CHECK(!A::Acquire(cmd,(VkDevice)99,&reason).Valid() && strstr(reason,"device mismatch"));
 A::Free(1,&cmd);A::Allocate(device,pool,0,1,&cmd);A::Invalidate(cmd);
 CHECK(!A::Acquire(cmd,device,&reason).Valid() && strstr(reason,"lacks compute"));
 A::Free(1,&cmd);A::Allocate(device,pool,8,1,&cmd);A::Invalidate(cmd);
 CHECK(!A::Acquire(cmd,device,&reason).Valid() && strstr(reason,"capabilities unavailable"));
 A::Free(1,&cmd);A::Allocate(device,pool,2,1,&cmd,VK_COMMAND_BUFFER_LEVEL_SECONDARY);A::Invalidate(cmd);
 CHECK(!A::Acquire(cmd,device,&reason).Valid() && strstr(reason,"secondary"));
 A::Free(1,&cmd);A::DestroyDevice(device);
 puts("PASS submit-reset-race/pending-fence/completed-but-recorded/discarded-recording/failed-submit/free-device");
}
