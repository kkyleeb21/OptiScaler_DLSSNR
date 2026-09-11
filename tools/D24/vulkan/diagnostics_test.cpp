#include "D24VkDiagnostics.h"
#include "DlssNr_VkExtensions.h"
#include <cstring>
#include <cstdio>
#define CHECK(x) if (!(x)) {printf("FAIL line=%d\n", __LINE__); return 1;}
int main()
{
    using DlssNr::VkAudit::ReadFeatures;
    VkDeviceCreateInfo info {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    auto f = ReadFeatures(info);
    CHECK(!f.bufferDeviceAddress && !f.shaderInt64 && !f.timelineSemaphore);
    VkPhysicalDeviceFeatures base{}; base.shaderInt64 = VK_TRUE;
    info.pEnabledFeatures = &base;
    VkPhysicalDeviceVulkan12Features v12 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    v12.bufferDeviceAddress = v12.timelineSemaphore = VK_TRUE;
    // An unrelated node before the feature node must survive byte-for-byte.
    VkDevicePrivateDataCreateInfo other {VK_STRUCTURE_TYPE_DEVICE_PRIVATE_DATA_CREATE_INFO};
    other.pNext = &v12; other.privateDataSlotRequestCount = 7; info.pNext = &other;
    auto savedOther = other; auto savedV12 = v12; auto savedInfo = info;
    f = ReadFeatures(info);
    CHECK(f.bufferDeviceAddress && f.shaderInt64 && f.timelineSemaphore);
    CHECK(!memcmp(&other, &savedOther, sizeof(other)) && !memcmp(&v12, &savedV12, sizeof(v12)) && !memcmp(&info, &savedInfo, sizeof(info)));
    VkPhysicalDeviceBufferDeviceAddressFeatures bda {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    VkPhysicalDeviceFeatures2 features2 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.features.shaderInt64 = VK_TRUE; features2.pNext = &bda;
    bda.pNext = &timeline; bda.bufferDeviceAddress = timeline.timelineSemaphore = VK_TRUE;
    info.pEnabledFeatures = nullptr; info.pNext = &features2;
    f = ReadFeatures(info);
    CHECK(f.bufferDeviceAddress && f.shaderInt64 && f.timelineSemaphore);
    bda.bufferDeviceAddress = VK_FALSE;
    f = ReadFeatures(info);
    CHECK(!f.bufferDeviceAddress && f.shaderInt64 && f.timelineSemaphore);
    VkPhysicalDeviceBufferDeviceAddressFeaturesEXT ext {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT};
    ext.bufferDeviceAddress=VK_TRUE; info.pNext=&ext;
    f=ReadFeatures(info);
    CHECK(f.bufferDeviceAddressEXT && !f.bufferDeviceAddress);
    ext.pNext=&features2;
    auto originalExt=ext;auto originalFeatures=features2;
    auto patched=info;DlssNr::VkAudit::StorageFeatureCopy copy;
    CHECK(copy.Enable(patched));
    CHECK(ReadFeatures(patched).storageExtended && ReadFeatures(patched).shaderInt64);
    CHECK(!memcmp(&ext,&originalExt,sizeof(ext)) && !memcmp(&features2,&originalFeatures,sizeof(features2)));
    CHECK(!DlssNr::VkAudit::NativeExecutionValidated);
    const char* extensions[]={VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME};
    CHECK(!strcmp(DlssNr::VkExt::DeviceRequirement(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,extensions,1),extensions[0]));
    CHECK(!strcmp(DlssNr::VkExt::DeviceRequirement(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,nullptr,0),VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME));
    puts("PASS absent/core12/extension-chain/unknown-node/disabled-feature/immutable-input/native-gate");
    return 0;
}
