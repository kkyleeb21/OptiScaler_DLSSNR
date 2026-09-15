// Host fixtures surround the candidate's unmodified RecordAdvancedVk body,
// allocation/layout helpers and composition class. No game/hook execution.
#define D18_VULKAN_ADVANCED_TEST
#include "host-production.h"
#include <cmath>
static unsigned validationErrors=0;
static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* info,void*){
 if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){++validationErrors;fprintf(stderr,"VALIDATION %s\n",info->pMessage);}return VK_FALSE;
}
#define CHECK(x) do{auto code=(x);if(code!=VK_SUCCESS){printf("FAIL line=%d result=%d\n",__LINE__,int(code));return 2;}}while(0)
int main(int argc,char** argv){
 const auto targetFormat=argc>1?VkFormat(atoi(argv[1])):VK_FORMAT_R32G32B32A32_SFLOAT;
 VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="D18 advanced NR host";app.apiVersion=VK_API_VERSION_1_2;
 std::vector<const char*> layers,extensions;uint32_t n=0;vkEnumerateInstanceLayerProperties(&n,nullptr);std::vector<VkLayerProperties> available(n);vkEnumerateInstanceLayerProperties(&n,available.data());
 for(auto& p:available)if(!strcmp(p.layerName,"VK_LAYER_KHRONOS_validation")){layers.push_back("VK_LAYER_KHRONOS_validation");extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);}
 VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ic.pApplicationInfo=&app;ic.enabledLayerCount=uint32_t(layers.size());ic.ppEnabledLayerNames=layers.data();ic.enabledExtensionCount=uint32_t(extensions.size());ic.ppEnabledExtensionNames=extensions.data();VkInstance instance{};CHECK(vkCreateInstance(&ic,nullptr,&instance));
 VkDebugUtilsMessengerEXT messenger{};if(!layers.empty()){VkDebugUtilsMessengerCreateInfoEXT d{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};d.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;d.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;d.pfnUserCallback=debugMessage;auto create=(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");if(create)create(instance,&d,nullptr,&messenger);}
 CHECK(vkEnumeratePhysicalDevices(instance,&n,nullptr));std::vector<VkPhysicalDevice> devices(n);CHECK(vkEnumeratePhysicalDevices(instance,&n,devices.data()));VkPhysicalDevice pd{};
 for(auto d:devices){VkPhysicalDeviceProperties p{};vkGetPhysicalDeviceProperties(d,&p);if(p.vendorID==0x10de){pd=d;printf("device=%s validation_layer=%d\n",p.deviceName,!layers.empty());break;}}if(!pd)return 3;
 uint32_t qn=0;vkGetPhysicalDeviceQueueFamilyProperties(pd,&qn,nullptr);std::vector<VkQueueFamilyProperties> families(qn);vkGetPhysicalDeviceQueueFamilyProperties(pd,&qn,families.data());unsigned family=UINT32_MAX;for(unsigned i=0;i<qn;++i)if(families[i].queueFlags&VK_QUEUE_COMPUTE_BIT){family=i;break;}if(family==UINT32_MAX)return 4;
 const char* wanted[]={"VK_NVX_binary_import","VK_NVX_image_view_handle","VK_EXT_buffer_device_address","VK_KHR_push_descriptor"};
 VkPhysicalDeviceBufferDeviceAddressFeaturesEXT bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT};bda.bufferDeviceAddress=VK_TRUE;
 VkPhysicalDeviceFeatures features{};features.shaderStorageImageExtendedFormats=VK_TRUE;
 float priority=1;VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qc.queueFamilyIndex=family;qc.queueCount=1;qc.pQueuePriorities=&priority;
 VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.pNext=&bda;dc.pEnabledFeatures=&features;dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qc;dc.enabledExtensionCount=4;dc.ppEnabledExtensionNames=wanted;VkDevice device{};CHECK(vkCreateDevice(pd,&dc,nullptr,&device));VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);
 DlssNr::VkAudit::RegisterDevice(device,vkGetDeviceProcAddr);DlssNr::VkAudit::RegisterQueueFamilies(device,families);{std::lock_guard lock(DlssNr::VkAudit::trackingMutex);DlssNr::VkAudit::deviceFunctions[device].storageExtended=true;}
 auto shim=LoadLibraryW(L"nvngx.dll_dlssnr.dll");if(!shim)return 5;
 auto init=(PFN_VkInit)GetProcAddress(shim,"dlssnr_vk_init");auto runtime=Util::DllPath().parent_path()/L"nvngx_dlssnr.dll";
 if(!init||init(runtime.c_str(),Util::DllPath().parent_path().c_str(),instance,pd,device,0x15)!=1)return 6;
 VkCommandPool pools[4]{};VkCommandBuffer cmds[4]{};VkFence fences[4]{};
 for(unsigned i=0;i<4;++i){VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pc.queueFamilyIndex=family;pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;CHECK(vkCreateCommandPool(device,&pc,nullptr,&pools[i]));VkCommandBufferAllocateInfo ac{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ac.commandPool=pools[i];ac.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ac.commandBufferCount=1;CHECK(vkAllocateCommandBuffers(device,&ac,&cmds[i]));VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};CHECK(vkCreateFence(device,&fc,nullptr,&fences[i]));DlssNr::VkAudit::Allocate(device,pools[i],family,1,&cmds[i]);}
 for(unsigned phase=0;phase<10;++phase){
  const unsigned w=phase==6?320:256,h=192;
  g_vk.device=device;g_vk.physicalDevice=pd;g_vk.instance=instance;g_vk.frames=0;g_vk.failed=false;
  g_vk.create=(PFN_VkCreate)GetProcAddress(shim,"dlssnr_vk_create");g_vk.evaluate=(PFN_VkEvaluate)GetProcAddress(shim,"dlssnr_vk_evaluate");g_vk.release=(PFN_VkRelease)GetProcAddress(shim,"dlssnr_vk_release");g_vk.options=(PFN_VkOptions)GetProcAddress(shim,"dlssnr_vk_set_options");
  if(!g_vk.create||!g_vk.evaluate||!g_vk.release||!g_vk.options)return 7;
  g_vk.pass=std::make_unique<DlssNr_Vk>("advanced host",device,pd,true);if(!g_vk.pass->IsInit())return 8;
  OwnedImage colour,depth,motion,readbackImage;
  if(!CreateImage(colour,w,h,targetFormat,true)||!CreateImage(depth,w/2,h/2,VK_FORMAT_R32_SFLOAT,false)||!CreateImage(motion,w/2,h/2,VK_FORMAT_R32G32_SFLOAT,false)||
     !CreateImage(readbackImage,w,h,VK_FORMAT_R32G32B32A32_SFLOAT,false)||!CreateImage(g_vk.proxy,w,h,VK_FORMAT_R16G16B16A16_SFLOAT,false)||!CreateImage(g_vk.keep,w,h,VK_FORMAT_R32G32B32A32_SFLOAT,false))return 9;
  DlssNrNative::AdvancedSettings settings;settings.count=phase==0?2:phase==1?3:phase==2||phase==3||phase==7?4:phase==6||phase==9?2:1;settings.shared=phase==3||phase==7;settings.highResolution=phase==4||phase==5||phase==8;settings.scale=phase==5?1.5f:1.25f;
  for(unsigned i=0;i<4;++i){settings.passes[i].ratio=settings.shared?.5f:i==1?.667f:i==2?1.f:.5f;settings.passes[i].intensity=settings.shared?1.f:1.f-.1f*i;}
  DlssNrNative::Settings common;common.customFilter=phase==1||phase==2;
  settings.preserveHighFrequency=phase==2;
  const bool failure=phase==7||phase==8;failAdvancedAllocation=phase==7?4:phase==8?2:-1;
  VkBuffer buffer{};VkDeviceMemory memory{};VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=w*h*16*4;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;CHECK(vkCreateBuffer(device,&bi,nullptr,&buffer));VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,buffer,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=FindMemoryTypeIndex(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);CHECK(vkAllocateMemory(device,&ai,nullptr,&memory));CHECK(vkBindBufferMemory(device,buffer,memory,0));
  for(unsigned f=0;f<12;++f){unsigned slot=f%4;auto cmd=cmds[slot];CHECK(vkResetCommandPool(device,pools[slot],0));DlssNr::VkAudit::InvalidatePool(pools[slot],false);CHECK(vkResetFences(device,1,&fences[slot]));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
   VkClearColorValue c{{.2f+.01f*f,.35f,.45f,1}},z{{.5f,0,0,0}},mv{};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
   for(auto pair:{std::pair<OwnedImage*,VkClearColorValue*>{&colour,&c},{&depth,&z},{&motion,&mv}}){Transition(cmd,*pair.first,VK_IMAGE_LAYOUT_GENERAL);vkCmdClearColorImage(cmd,pair.first->image,VK_IMAGE_LAYOUT_GENERAL,pair.second,1,&range);}
   // Make the clear writes visible even when the next layout is also GENERAL.
   VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};mb.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&mb,0,nullptr,0,nullptr);
   DlssNr::VkAudit::Handoff(cmd,nullptr,nullptr,0);auto lease=DlssNr::VkAudit::Acquire(cmd,device);if(!lease.Valid())return 10;
   DlssNrConstants encode{};encode.Width=w;encode.Height=h;encode.SourceWidth=w;encode.SourceHeight=h;encode.WhitePoint=1;encode.TransferStrength=encode.ColourStrength=1;encode.MaxRatio=2;encode.Passthrough=1;encode.CompareZoom=1;encode.NetworkRatioX=encode.NetworkRatioY=1;
   const bool neutral=f==11; if(neutral)encode.TransferStrength=encode.ColourStrength=0;
   bool ok=RecordAdvancedVk(cmd,settings,common,&colour.ngx,&depth.ngx,&motion.ngx,w/2,h/2,false,f==0,1,1,encode,lease);
   printf("phase=%u frame=%u result=%d recorded=%u ready=%u high=%u\n",phase,f,g_vk.advancedStatus.result,g_vk.advancedStatus.recorded,g_vk.advancedStatus.ready,settings.highResolution);fflush(stdout);
   if(failure){if(ok||g_vk.advancedStatus.result!=-26)return 11;}
   else if(!ok||g_vk.advancedStatus.recorded!=(settings.highResolution?1:settings.count))return 11;
   // Convert each typed game target to a common FP32 readback, with the same
   // sampled-image path used by production composition.
   Transition(cmd,colour,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);Transition(cmd,readbackImage,VK_IMAGE_LAYOUT_GENERAL);
   auto copyConstants=encode;copyConstants.Mode=2;
   if(!g_vk.pass->Dispatch(cmd,copyConstants,w,h,colour.view,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,readbackImage.view,VK_NULL_HANDLE,readbackImage.format))return 15;
   Transition(cmd,readbackImage,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);VkBufferImageCopy copy{};copy.bufferOffset=slot*w*h*16;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={w,h,1};vkCmdCopyImageToBuffer(cmd,readbackImage.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
   CHECK(vkEndCommandBuffer(cmd));VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cmd;auto batch=DlssNr::VkAudit::Prepare(DlssNr::VkAudit::Commands(1,&si));CHECK(vkQueueSubmit(queue,1,&si,fences[slot]));DlssNr::VkAudit::Submitted(queue,batch,VK_SUCCESS,[&](VkFence f){return vkQueueSubmit(queue,0,nullptr,f);});
   if(slot==3){CHECK(vkWaitForFences(device,4,fences,VK_TRUE,10000000000ull));void* data=nullptr;CHECK(vkMapMemory(device,memory,0,VK_WHOLE_SIZE,0,&data));auto pixels=(float*)data;bool valid=true;double sum=0,maximumError=0;
    for(size_t i=0;i<size_t(w)*h*16;++i){valid&=std::isfinite(pixels[i]);if(i%4!=3)sum+=pixels[i];}
    const double tolerance=targetFormat==VK_FORMAT_B10G11R11_UFLOAT_PACK32?.009:targetFormat==VK_FORMAT_R8G8B8A8_UNORM?.0041:targetFormat==VK_FORMAT_A2B10G10R10_UNORM_PACK32?.0011:targetFormat==VK_FORMAT_R32G32B32A32_SFLOAT?1e-6:.001;
    for(unsigned frame=0;frame<4;++frame)if(failure||(neutral&&frame==3))for(size_t i=0;i<size_t(w)*h;++i){const size_t offset=frame*size_t(w)*h*4+i*4;
      maximumError=std::max(maximumError,std::abs(double(pixels[offset])-(.2f+.01f*(f-3+frame))));
      maximumError=std::max(maximumError,std::abs(double(pixels[offset+1])-.35f));
      maximumError=std::max(maximumError,std::abs(double(pixels[offset+2])-.45f));}
    vkUnmapMemory(device,memory);printf("readback format=%d neutral_or_failure_max_error=%.9g tolerance=%.9g\n",int(targetFormat),maximumError,tolerance);
    if(!valid||sum<=1||maximumError>tolerance)return 12;}
  }
  CHECK(vkDeviceWaitIdle(device));for(auto pool:pools){CHECK(vkResetCommandPool(device,pool,0));DlssNr::VkAudit::InvalidatePool(pool,false);}
  for(auto& p:g_vk.models){if(p.feature)g_vk.release(p.feature);if(p.params)HostDestroyParameters(p.params);DestroyImage(p.output);DestroyImage(p.filtered);p={};}
  for(auto* p:{&g_vk.proxy,&g_vk.keep,&g_vk.delta[0],&g_vk.delta[1],&g_vk.largeInput,&g_vk.zeroMotion,&g_vk.sharedInput,&colour,&depth,&motion,&readbackImage})DestroyImage(*p);
  g_vk.pass.reset();vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,memory,nullptr);
 }
 auto shutdown=(int(*)(void*))GetProcAddress(shim,"dlssnr_vk_shutdown");if(shutdown&&shutdown(device)!=1)return 13;
 for(unsigned i=0;i<4;++i){vkDestroyFence(device,fences[i],nullptr);vkDestroyCommandPool(device,pools[i],nullptr);}DlssNr::VkAudit::DestroyDevice(device);vkDestroyDevice(device,nullptr);
 if(messenger){auto destroy=(PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");destroy(instance,messenger,nullptr);}vkDestroyInstance(instance,nullptr);
 printf("completed_phases=10 frames=120 validation_errors=%u\n",validationErrors);return validationErrors?14:0;
}

