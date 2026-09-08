#include "pch.h"

#include "DlssNrFeature_Vk.h"
#include "D24VkDiagnostics.h"
#include "D24VkTracking.h"
#include "NativeControl.h"
#include "VkColourCapture.h"
#include "NativeExposureVk.h"
#include <sstream>

#include <Config.h>
#include <State.h>
#include <Util.h>
#include <NVNGX_Parameter.h>
#include <hooks/Streamline_Hooks.h>

#include <shaders/dlssnr/DlssNr_Vk.h>

#include <memory>
#include <mutex>
#include <string>
#include <cmath>

namespace DlssNr
{

namespace
{

// The forwarder's Vulkan surface. The model checks its caller's module path and requires nvngx.dll in
// it, whichever API is being used, so these calls go through the same shim the D3D12 path does.
using PFN_VkProbe = int(__cdecl*)(const wchar_t*);
using PFN_VkInit = int(__cdecl*)(const wchar_t*, const wchar_t*, void*, void*, void*, int);
using PFN_VkCreate = void*(__cdecl*)(void*, void*, unsigned int, unsigned int, int, float, int, float, float, float,
                                     int, int);
using PFN_VkEvaluate = int(__cdecl*)(void*, void*, void*, void*, void*, void*, void*, unsigned int, unsigned int,
                                     unsigned int, unsigned int, int, int, float, int, float, float, float, int, float,
                                     float);
using PFN_VkRelease = void(__cdecl*)(void*);
using PFN_VkOptions = int(__cdecl*)(float,int,int);

// One image this pass owns: the storage, the view, and the NGX wrapper that describes it. Kept
// together because they are created, resized and destroyed as one thing.
struct OwnedImage
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    NVSDK_NGX_Resource_VK ngx {};
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;

    bool Valid() const { return image != VK_NULL_HANDLE && view != VK_NULL_HANDLE; }
};

struct VkState
{
    bool failed = false;
    bool active = false;
    bool exposureAllocationAttempted = false;
    const char* reason = "";

    HMODULE forwarder = nullptr;
    PFN_VkProbe probe = nullptr;
    PFN_VkInit init = nullptr;
    PFN_VkCreate create = nullptr;
    PFN_VkEvaluate evaluate = nullptr;
    PFN_VkRelease release = nullptr;
    PFN_VkOptions options = nullptr;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    bool ngxInitialised = false;
    void* feature = nullptr;
    NVSDK_NGX_Parameter* capabilityParams = nullptr;

    // What the model writes, the proxy it is shown, and the frame as the upscaler left it.
    OwnedImage output;
    OwnedImage proxy;
    OwnedImage filtered;
    OwnedImage keep;
    OwnedImage captureImage;
    OwnedImage exposureCopy;
    std::unique_ptr<NativeExposureVk> exposureReadback;
    std::unique_ptr<ColourCapture::Batch> capture;
    NVSDK_NGX_Resource_VK nrDepth {};
    NVSDK_NGX_Resource_VK nrMotion {};

    std::unique_ptr<DlssNr_Vk> pass;

    uint32_t width = 0;
    uint32_t height = 0;
    bool reset = true;
    uint32_t highlightEncoding = 0;
    unsigned long long frames = 0;

    // Timing. A pair of timestamps per frame across a ring, read back three frames later: a query
    // read the frame it was written stalls the CPU on the GPU, which would cost more than the pass
    // it is measuring. Vulkan reports ticks, and timestampPeriod is how many nanoseconds a tick is.
    VkQueryPool queryPool = VK_NULL_HANDLE;
    float timestampPeriod = 0.0f;
    unsigned long long timedFrames = 0;
    std::optional<double> lastGpuTime;

    // Whether the game hands over an exposure texture. Observed, not consumed -- see where it is set.
    bool exposureOffered = false;
    std::vector<VkAudit::Lease> leases;
};

// Four frames of pairs. Three would do, four keeps the modulo cheap and the slot being written well
// clear of the slot being read.
constexpr uint32_t kTimingSlots = 4;

VkState g_vk;
std::atomic<bool> nativeExposureReady{false};
std::mutex g_vkMutex;
std::vector<std::unique_ptr<VkState>> retiredStates;

void Fail(const char* why)
{
    if (g_vk.failed)
        return;

    g_vk.failed = true;
    g_vk.reason = why;
    LOG_ERROR("DLSS-NR Vulkan unavailable: {}", why);
}

// ---------------------------------------------------------------------------------------------
// Images this pass owns
// ---------------------------------------------------------------------------------------------

void DestroyImage(OwnedImage& img, VkDevice device = g_vk.device)
{
    if (device == VK_NULL_HANDLE)
        return;

    if (img.view != VK_NULL_HANDLE)
        vkDestroyImageView(device, img.view, nullptr);

    if (img.image != VK_NULL_HANDLE)
        vkDestroyImage(device, img.image, nullptr);

    if (img.memory != VK_NULL_HANDLE)
        vkFreeMemory(device, img.memory, nullptr);

    img = OwnedImage {};
}

uint32_t FindMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProps {};
    vkGetPhysicalDeviceMemoryProperties(g_vk.physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    return UINT32_MAX;
}

// STORAGE and SAMPLED both, because every one of these is written by one dispatch and read by the
// next; TRANSFER_SRC so a capture can copy it out without a second surface.
bool CreateImage(OwnedImage& img, uint32_t width, uint32_t height, VkFormat format, bool readWrite,
                 const char* role = "optional", bool required = false)
{
    DestroyImage(img);
    const auto allocationFailed = [&](VkResult result) {
        LOG_WARN("D18 NR Vulkan allocation failed: {} {}x{} format {} result {}; {}", role,
                 width, height, (int)format, (int)result,
                 result == VK_ERROR_DEVICE_LOST ? "device lost; restart required" : required ? "NR bypassed; restart to retry" : "optional operation skipped");
        if (Config::Instance()->DlssNrDiagnostics.value_or_default() != 0)
            VkAudit::Write("event=allocation_failed role=%s width=%u height=%u format=%u result=%d", role,width,height,(unsigned)format,(int)result);
        if (result == VK_ERROR_DEVICE_LOST) Fail("Graphics device lost; restart required.");
        else if (required) Fail("NR resource allocation failed. Original SR/RR retained; restart to retry.");
        DestroyImage(img);
        return false;
    };

    VkImageCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = { width, height, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    auto result = vkCreateImage(g_vk.device, &info, nullptr, &img.image);
    if (result != VK_SUCCESS)
    {
        return allocationFailed(result);
    }

    VkMemoryRequirements req {};
    vkGetImageMemoryRequirements(g_vk.device, img.image, &req);

    VkMemoryAllocateInfo alloc {};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = FindMemoryTypeIndex(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (alloc.memoryTypeIndex == UINT32_MAX) return allocationFailed(VK_ERROR_FEATURE_NOT_PRESENT);
    result = vkAllocateMemory(g_vk.device, &alloc, nullptr, &img.memory);
    if (result == VK_SUCCESS) result = vkBindImageMemory(g_vk.device, img.image, img.memory, 0);
    if (result != VK_SUCCESS)
    {
        return allocationFailed(result);
    }

    VkImageViewCreateInfo view {};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = img.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    result = vkCreateImageView(g_vk.device, &view, nullptr, &img.view);
    if (result != VK_SUCCESS)
    {
        return allocationFailed(result);
    }

    img.width = width;
    img.height = height;
    img.format = format;
    img.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    // The NGX wrapper. Filled once, because none of it changes until the image is recreated.
    img.ngx.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    img.ngx.Resource.ImageViewInfo.ImageView = img.view;
    img.ngx.Resource.ImageViewInfo.Image = img.image;
    img.ngx.Resource.ImageViewInfo.SubresourceRange = view.subresourceRange;
    img.ngx.Resource.ImageViewInfo.Format = format;
    img.ngx.Resource.ImageViewInfo.Width = width;
    img.ngx.Resource.ImageViewInfo.Height = height;
    img.ngx.ReadWrite = readWrite;

    return true;
}

// A layout transition with the access masks that go with it. Vulkan has no equivalent of D3D12's
// state promotion, so every read and every write says which layout it needs and this is how it gets
// there. Tracked per image so a no-op transition is not recorded.
void Transition(VkCommandBuffer cmd, OwnedImage& img, VkImageLayout to)
{
    if (img.image == VK_NULL_HANDLE || img.layout == to)
        return;

    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = img.layout;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = img.image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    img.layout = to;
}

// A resource the game owns. Its layout is the game's business, so this records the transition and
// puts it back exactly as it was rather than tracking it.
void TransitionForeign(VkCommandBuffer cmd, VkImage image, VkImageSubresourceRange range, VkImageLayout from,
                       VkImageLayout to)
{
    if (image == VK_NULL_HANDLE || from == to)
        return;

    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = range;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
}

// ---------------------------------------------------------------------------------------------
// Bring-up
// ---------------------------------------------------------------------------------------------

bool LoadForwarder()
{
    if (g_vk.forwarder != nullptr)
        return g_vk.create != nullptr;

    auto path = Util::FindFilePath(Util::DllPath().remove_filename(), "nvngx.dll_dlssnr.dll");

    if (!path.has_value())
        path = Util::FindFilePath(Util::ExePath().remove_filename(), "nvngx.dll_dlssnr.dll");

    if (!path.has_value())
    {
        Fail("nvngx.dll_dlssnr.dll was not found beside OptiScaler or the game");
        return false;
    }

    g_vk.forwarder = LoadLibraryW(path->wstring().c_str());

    if (g_vk.forwarder == nullptr)
    {
        Fail("the forwarder would not load");
        return false;
    }

    g_vk.probe = (PFN_VkProbe) GetProcAddress(g_vk.forwarder, "dlssnr_vk_probe");
    g_vk.init = (PFN_VkInit) GetProcAddress(g_vk.forwarder, "dlssnr_vk_init");
    g_vk.create = (PFN_VkCreate) GetProcAddress(g_vk.forwarder, "dlssnr_vk_create");
    g_vk.options = (PFN_VkOptions) GetProcAddress(g_vk.forwarder, "dlssnr_vk_set_options");
    g_vk.evaluate = (PFN_VkEvaluate) GetProcAddress(g_vk.forwarder, "dlssnr_vk_evaluate");
    g_vk.release = (PFN_VkRelease) GetProcAddress(g_vk.forwarder, "dlssnr_vk_release");

    if (g_vk.init == nullptr || g_vk.create == nullptr || g_vk.evaluate == nullptr)
    {
        Fail("the forwarder is missing its Vulkan entry points");
        return false;
    }

    return true;
}

// Whether a format can hold linear, open-ended light. A frame the game already tone mapped has white
// at 1 and must not be encoded a second time; an 8-bit or normalised format cannot be scene-referred
// whatever the game says. The D3D12 path asks the same question of DXGI formats.
bool FormatCanHoldLinearHdr(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_R16G16B16_SFLOAT:
    case VK_FORMAT_R32G32B32_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
        return true;
    default:
        return false;
    }
}

// The create flags the game gave its own upscaler, which is where HDR and inverted depth are stated.
// Read from the parameter block rather than configured, because they describe the game's buffers and
// getting either wrong is silent: an encoded frame encoded twice, or depth read backwards.
unsigned int GameCreateFlags(NVSDK_NGX_Parameter* params)
{
    unsigned int flags = 0;

    if (params != nullptr)
        params->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &flags);

    return flags;
}

void DestroyState(VkState& state)
{
    if(state.feature && state.release) state.release(state.feature);
    state.feature=nullptr;
    DestroyImage(state.output,state.device);
    DestroyImage(state.proxy,state.device);
    DestroyImage(state.exposureCopy,state.device);
    DestroyImage(state.filtered,state.device);
    DestroyImage(state.keep,state.device);
    if(state.capture && state.capture->remaining)
        ColourCapture::Status("Capture interrupted by backend reset; previously saved files remain");
    state.capture.reset();
    DestroyImage(state.captureImage,state.device);
    state.pass.reset();
    state.exposureReadback.reset();
    if(state.capabilityParams) NVSDK_NGX_VULKAN_DestroyParameters(state.capabilityParams);
    state.capabilityParams=nullptr;
}
void CollectRetired()
{
    for(auto it=retiredStates.begin();it!=retiredStates.end();)
    {
        bool ready=true;
        for(auto lease:(*it)->leases) if(!VkAudit::Ready(lease)){ready=false;break;}
        if(ready){DestroyState(**it);it=retiredStates.erase(it);}else ++it;
    }
}

std::optional<std::filesystem::path> FindSnippet()
{
    auto snippet = Util::FindFilePath(Util::DllPath().remove_filename(), "D24Runtime.dll");

    if (!snippet.has_value())
        snippet = Util::FindFilePath(Util::ExePath().remove_filename(), "nvngx_dlssnr.dll");

    return snippet;
}

} // namespace

// ---------------------------------------------------------------------------------------------

bool IsRunningVk() { return g_vk.active && g_vk.feature != nullptr && !g_vk.failed; }
bool ExposureReadyVk() { return nativeExposureReady.load(std::memory_order_relaxed); }

const char* FailureReasonVk()
{
    if (!VkAudit::NativeArmed())
        return "Vulkan NR disabled pending resource and submission contract validation";
    return g_vk.failed ? g_vk.reason : "";
}

unsigned long long FramesVk() { return g_vk.frames; }

bool ExposureOfferedVk() { return g_vk.exposureOffered; }

std::optional<double> LastGpuTimeVk() { return g_vk.lastGpuTime; }

void EvaluateAfterUpscaleVk(VkCommandBuffer cmdBuffer, NVSDK_NGX_Parameter* params, VkInstance instance,
                            VkPhysicalDevice physicalDevice, VkDevice device, int featureFlags)
{
    if (Config::Instance()->DlssNrDiagnostics.value_or_default()!=0 && params != nullptr)
    {
        // Observe CPU-side metadata only. In particular, do not create resources, bind descriptors,
        // insert barriers, or initialize the NR runtime in this diagnostic phase.
        static std::mutex auditMutex;
        std::lock_guard<std::mutex> auditLock(auditMutex);
        static unsigned long long observed = 0;
        static uint64_t lastSignature = 0;
        static unsigned snapshots = 0;
        ++observed;
        const char* keys[] = { NVSDK_NGX_Parameter_Output, NVSDK_NGX_Parameter_Depth,
                              NVSDK_NGX_Parameter_MotionVectors, NVSDK_NGX_Parameter_ExposureTexture };
        const char* roles[] = { "output", "depth", "motion", "exposure" };
        NVSDK_NGX_Resource_VK* resources[4] {};
        uint64_t signature = 14695981039346656037ull;
        for (unsigned i = 0; i < 4; ++i)
        {
            params->Get(keys[i], reinterpret_cast<void**>(&resources[i]));
            if (resources[i] && resources[i]->Type == NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW)
            {
                const auto& v = resources[i]->Resource.ImageViewInfo;
                for (auto value : { v.Width, v.Height, uint32_t(v.Format), v.SubresourceRange.aspectMask,
                                    v.SubresourceRange.baseMipLevel, v.SubresourceRange.levelCount,
                                    v.SubresourceRange.baseArrayLayer, v.SubresourceRange.layerCount })
                    signature = (signature ^ value) * 1099511628211ull;
            }
            else
                signature = (signature ^ (i + 1)) * 1099511628211ull;
        }
        if (snapshots < 32 && (observed == 1 || observed == 120 || signature != lastSignature))
        {
            ++snapshots;
            lastSignature = signature;
            VkAudit::Write("event=sr_snapshot frame=%llu snapshot=%u cmd=%p device=%p native_armed=%d", observed, snapshots, (void*)cmdBuffer, (void*)device,VkAudit::NativeArmed());
            VkAudit::Write("event=feature_flags known=%d value=%d", featureFlags >= 0, featureFlags);
            VkAudit::Handoff(cmdBuffer, resources, roles, 4);
            for (unsigned i = 0; i < 4; ++i)
                VkAudit::Resource(roles[i], resources[i]);
            for (const char* key : { NVSDK_NGX_Parameter_MV_Scale_X, NVSDK_NGX_Parameter_MV_Scale_Y,
                                    NVSDK_NGX_Parameter_Jitter_Offset_X, NVSDK_NGX_Parameter_Jitter_Offset_Y,
                                    NVSDK_NGX_Parameter_DLSS_Pre_Exposure })
            {
                float value = 0;
                const auto result = params->Get(key, &value);
                VkAudit::Write("event=float key=%s result=%u value=%.9g", key, unsigned(result), value);
            }
            for (const char* key : { NVSDK_NGX_Parameter_Reset, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
                                    "DLSS.Render.Subrect.Dimensions.Width", "DLSS.Render.Subrect.Dimensions.Height",
                                    "DLSS.Input.Color.Subrect.Base.X", "DLSS.Input.Color.Subrect.Base.Y",
                                    "DLSS.Input.Depth.Subrect.Base.X", "DLSS.Input.Depth.Subrect.Base.Y",
                                    "DLSS.Input.MV.Subrect.Base.X", "DLSS.Input.MV.Subrect.Base.Y",
                                    "DLSS.Output.Subrect.Base.X", "DLSS.Output.Subrect.Base.Y" })
            {
                unsigned value = 0;
                const auto result = params->Get(key, &value);
                VkAudit::Write("event=uint key=%s result=%u value=%u", key, unsigned(result), value);
            }
        }
    }

    if (!VkAudit::NativeArmed())
        return;

    auto& cfg = *Config::Instance();

    static unsigned previousMode=0;
    const unsigned mode=NativeControl::Settings().mode;
    if(mode != previousMode) {
        g_vk.reset=true;
        VkAudit::Write("event=nr_mode mode=%u meaning=0_off_1_conversion_2_nr",mode);
    }
    previousMode=mode;
    g_vk.active=mode==2;
    static unsigned auditSamples[3]{};
    const auto auditSample=++auditSamples[mode];
    if(params && (auditSample<=16 || (auditSample<=24000 && auditSample%120==0)))
    {
        if(auditSample==1 || (auditSample<=24000 && auditSample%600==0))
            VkAudit::Write("event=fg_coverage mode=%u interposer=%p plugin=%p ngx_fg=%p interposer_hook=%d plugin_hook=%d skip=%d",
                mode,GetModuleHandleW(L"sl.interposer.dll"),GetModuleHandleW(L"sl.dlss_g.dll"),
                GetModuleHandleW(L"nvngx_dlssg.dll"),StreamlineHooks::isInterposerHooked(),StreamlineHooks::isDlssgHooked(),
                cfg.SkipStreamlineHooks.value_or_default());
        NVSDK_NGX_Resource_VK* auditOutput=nullptr;
        params->Get(NVSDK_NGX_Parameter_Output,(void**)&auditOutput);
        if(auditOutput && auditOutput->Type==NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW)
            VkAudit::Write("event=nr_handoff mode=%u sample=%u cmd=%p output=%p view=%p",mode,auditSample,
                (void*)cmdBuffer,(void*)auditOutput->Resource.ImageViewInfo.Image,(void*)auditOutput->Resource.ImageViewInfo.ImageView);
    }
    if(mode==0) return;

    if (cmdBuffer == VK_NULL_HANDLE || params == nullptr || device == VK_NULL_HANDLE ||
        physicalDevice == VK_NULL_HANDLE)
        return;

    std::lock_guard<std::mutex> lock(g_vkMutex);
    CollectRetired();

    if(g_vk.capture && g_vk.capture->Poll() && !g_vk.capture->remaining){
        g_vk.capture.reset();DestroyImage(g_vk.captureImage,g_vk.device);
    }

    if (g_vk.failed)
        return;

    // The game's own resources, already wrapped: NGX hands Vulkan resources over as
    // NVSDK_NGX_Resource_VK, so only this pass's own images need building.
    NVSDK_NGX_Resource_VK* colour = nullptr;
    NVSDK_NGX_Resource_VK* depth = nullptr;
    NVSDK_NGX_Resource_VK* motion = nullptr;

    params->Get(NVSDK_NGX_Parameter_Output, (void**) &colour);
    params->Get(NVSDK_NGX_Parameter_Depth, (void**) &depth);
    params->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &motion);

    // Whether the game supplies an exposure, reported but deliberately not read.
    //
    // Reading it means binding the game's own image in a descriptor, and a descriptor names the
    // layout the image will be in when the shader runs. That layout is the game's business, NVIDIA's
    // Vulkan header does not state what NGX leaves its inputs in, and naming the wrong one is
    // undefined behaviour rather than a failure that can be caught and backed out of. Transitioning
    // it is no safer: a barrier needs the layout it is coming from, and the one value that is always
    // legal to claim -- UNDEFINED -- is defined to discard the contents, which are the whole point.
    //
    // So this answers the question that decides whether any of that is worth doing: does a Vulkan
    // game supply one at all? The D3D12 path found only one game in six that did.
    NVSDK_NGX_Resource_VK* exposure = nullptr;
    float preExposure = 1.0f;
    const bool havePre =
        params->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &preExposure) == NVSDK_NGX_Result_Success;

    params->Get(NVSDK_NGX_Parameter_ExposureTexture, (void**) &exposure);

    static bool saidExposure = false;

    if (!saidExposure)
    {
        saidExposure = true;
        LOG_INFO("DLSS-NR Vulkan: exposure from the game: DLSS.Pre.Exposure {}, ExposureTexture {}",
                 havePre ? std::to_string(preExposure) : std::string("not supplied"),
                 exposure != nullptr ? "supplied" : "not supplied");
    }

    g_vk.exposureOffered = exposure != nullptr;

    if (colour == nullptr || depth == nullptr || motion == nullptr)
    {
        static bool said = false;

        if (!said)
        {
            said = true;
            LOG_INFO("DLSS-NR Vulkan: the parameter block carried no {}",
                     colour == nullptr ? "output" : (depth == nullptr ? "depth" : "motion vectors"));
        }

        return;
    }

    if(colour->Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW || depth->Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW || motion->Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW) return;
    const uint32_t width = colour->Resource.ImageViewInfo.Width;
    const uint32_t height = colour->Resource.ImageViewInfo.Height;
    const uint32_t guideWidth = depth->Resource.ImageViewInfo.Width;
    const uint32_t guideHeight = depth->Resource.ImageViewInfo.Height;

    float mvScaleX = 1.0f, mvScaleY = 1.0f;
    params->Get(NVSDK_NGX_Parameter_MV_Scale_X, &mvScaleX);
    params->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &mvScaleY);
    unsigned int gameReset = 0;
    params->Get(NVSDK_NGX_Parameter_Reset, &gameReset);

    if (width == 0 || height == 0 || guideWidth == 0 || guideHeight == 0 ||
        motion->Resource.ImageViewInfo.Width == 0 || motion->Resource.ImageViewInfo.Height == 0)
    { Fail("invalid Vulkan colour or guide dimensions"); return; }
    if (!std::isfinite(mvScaleX) || !std::isfinite(mvScaleY))
    { Fail("non-finite Vulkan motion vector scale"); return; }
    // The current native ABI supplies one extent for both guides. Do not silently
    // crop full-resolution motion vectors or treat padded allocations as valid pixels.
    unsigned renderWidth=guideWidth, renderHeight=guideHeight;
    params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &renderWidth);
    params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &renderHeight);
    if (motion->Resource.ImageViewInfo.Width != guideWidth ||
        motion->Resource.ImageViewInfo.Height != guideHeight ||
        renderWidth != guideWidth || renderHeight != guideHeight)
    { Fail("Vulkan NR requires matching, unpadded depth and motion regions; original SR retained"); return; }
    // This pass binds whole views; silently treating a nonzero origin as zero
    // produces a shifted/cropped result. Preserve SR until subrect support exists.
    for (const char* key : {"DLSS.Output.Subrect.Base.X", "DLSS.Output.Subrect.Base.Y",
                           "DLSS.Input.Depth.Subrect.Base.X", "DLSS.Input.Depth.Subrect.Base.Y",
                           "DLSS.Input.MV.Subrect.Base.X", "DLSS.Input.MV.Subrect.Base.Y"})
    {
        unsigned offset=0;
        params->Get(key,&offset);
        if(offset) { Fail("Vulkan NR requires zero-origin output and guide subrects"); return; }
    }

    static DlssNrNative::Settings builtSettings;
    const auto requestedSettings=NativeControl::Settings();
    const bool nativeOptionsChanged = builtSettings.customFilter!=requestedSettings.customFilter || builtSettings.networkRatio!=requestedSettings.networkRatio ||
        builtSettings.linearResolve!=requestedSettings.linearResolve || builtSettings.linearColorInput!=requestedSettings.linearColorInput;
    const bool tuningChanged=g_vk.feature && (nativeOptionsChanged || builtSettings.preset!=requestedSettings.preset || builtSettings.intensity!=requestedSettings.intensity || builtSettings.style!=requestedSettings.style || builtSettings.localStructure!=requestedSettings.localStructure || builtSettings.localTone!=requestedSettings.localTone || builtSettings.skinStructure!=requestedSettings.skinStructure || builtSettings.autoMask!=requestedSettings.autoMask);
    if(g_vk.device && (g_vk.device!=device || g_vk.width!=width || g_vk.height!=height || tuningChanged)) {
        if(retiredStates.size()>=4){Fail("too many pending resource generations");return;}
        retiredStates.push_back(std::make_unique<VkState>(std::move(g_vk)));
        g_vk=VkState{};
        nativeExposureReady.store(false,std::memory_order_relaxed);
        VkAudit::Write("event=nr_resize width=%u height=%u retired=%zu",width,height,retiredStates.size());
    }
    const char* leaseReason=nullptr;
    const auto lease=VkAudit::Acquire(cmdBuffer,device,&leaseReason);
    if(!lease.Valid()){Fail(leaseReason ? leaseReason : "Vulkan tracking unavailable");return;}
    g_vk.leases.erase(std::remove_if(g_vk.leases.begin(),g_vk.leases.end(),[](auto oldLease){return VkAudit::Ready(oldLease);}),g_vk.leases.end());
    if(g_vk.leases.size()>=64){Fail("too many pending Vulkan recordings");return;}
    g_vk.leases.push_back(lease);
    g_vk.instance = instance;
    g_vk.physicalDevice = physicalDevice;

    // A device change invalidates everything. Rebuild rather than reuse handles from a dead device.
    if (g_vk.device != device)
    {
        g_vk.device = device;
        g_vk.instance = instance;
        g_vk.physicalDevice = physicalDevice;
    }

    if (mode==2 && !LoadForwarder())
        return;

    // Initialise NGX on this device, once. The snippet path is the model itself; the forwarder loads
    // it so the caller gate sees a module named nvngx.dll.
    if (mode==2 && !g_vk.ngxInitialised)
    {
        auto snippet = FindSnippet();

        if (!snippet.has_value())
        {
            Fail("nvngx_dlssnr.dll was not found beside OptiScaler or the game");
            return;
        }

        const int probe = g_vk.probe != nullptr ? g_vk.probe(snippet->wstring().c_str()) : 0;

        // Four bits, one per entry point. Anything short of fifteen means the model's Vulkan surface
        // is not entirely reachable and there is no point going further.
        if (probe != 15)
        {
            LOG_ERROR("DLSS-NR Vulkan: the model's Vulkan surface is incomplete (probe {})", probe);
            Fail("the model does not expose a complete Vulkan surface");
            return;
        }

        const int result =
            g_vk.init(snippet->wstring().c_str(), State::Instance().NVNGX_ApplicationDataPath.c_str(),
                      (void*) instance, (void*) physicalDevice, (void*) device, 0x0000015);

        if (result != 1)
        {
            LOG_ERROR("DLSS-NR Vulkan: NVSDK_NGX_VULKAN_Init_Ext returned {}", result);
            Fail("the model would not initialise on this Vulkan device");
            return;
        }

        g_vk.ngxInitialised = true;
        LOG_INFO("DLSS-NR Vulkan: the model initialised on this device");
    }

    if (mode==2 && g_vk.capabilityParams == nullptr)
    {
        if (NVSDK_NGX_VULKAN_AllocateParameters(&g_vk.capabilityParams) != NVSDK_NGX_Result_Success ||
            g_vk.capabilityParams == nullptr)
        {
            Fail("a parameter block could not be allocated");
            return;
        }
    }

    // GPU timing stays disabled until it shares the completion lease.
    if (g_vk.pass == nullptr)
    {
        g_vk.pass = std::make_unique<DlssNr_Vk>("Neural Rendering", device, physicalDevice);

        if (!g_vk.pass->IsInit())
        {
            g_vk.pass.reset();
            Fail("the composition pass could not be created");
            return;
        }
    }

    // Resize. The model's feature is built for a size and has to be rebuilt when it changes.
    if (g_vk.width != width || g_vk.height != height)
    {
        if (g_vk.feature != nullptr && g_vk.release != nullptr)
        {
            g_vk.release(g_vk.feature);
            g_vk.feature = nullptr;
        }

        const VkFormat working = VK_FORMAT_R16G16B16A16_SFLOAT;

        if (!CreateImage(g_vk.output, width, height, working, true, "output", true) ||
            !CreateImage(g_vk.proxy, width, height, working, true, "proxy", true) ||
            !CreateImage(g_vk.keep, width, height, VK_FORMAT_R32G32B32A32_SFLOAT, true, "original", true))
        {
            DestroyImage(g_vk.output); DestroyImage(g_vk.proxy); DestroyImage(g_vk.keep);
            Fail("the pass could not allocate its own surfaces");
            return;
        }

        g_vk.width = width;
        g_vk.height = height;
        g_vk.reset = true;
    }

    if (g_vk.feature == nullptr && mode==2)
    {
        if(requestedSettings.customFilter && !g_vk.filtered.Valid() &&
           !CreateImage(g_vk.filtered,width,height,VK_FORMAT_R16G16B16A16_SFLOAT,false,"prefilter",true))
        { Fail("model Color prefilter allocation failed"); return; }
        if (!g_vk.options || !g_vk.options(requestedSettings.networkRatio, requestedSettings.linearResolve, requestedSettings.linearColorInput))
        { Fail("native runtime options unavailable; update the D18 forwarder and verify runtime version"); return; }
        g_vk.feature = g_vk.create(
            (void*) cmdBuffer, g_vk.capabilityParams, width, height, (int)requestedSettings.preset,
            cfg.DlssNrIntensity.value_or_default(), (int) cfg.DlssNrStyle.value_or_default(),
            cfg.DlssNrLocalStructure.value_or_default(), cfg.DlssNrLocalTone.value_or_default(),
            cfg.DlssNrSkinStructure.value_or_default(), cfg.DlssNrAutoMask.value_or_default() ? 1 : 0, 1);

        if (g_vk.feature == nullptr)
        {
            Fail("the model would not build a feature on this device");
            return;
        }

        LOG_INFO("DLSS-NR Vulkan: feature up at {}x{}", width, height);
        builtSettings=requestedSettings;
        bool floatsValid = true;
        for (const auto& field : {
            std::pair<const char*, float>{"DLSSNR.ScalingRatio", requestedSettings.networkRatio},
            {"DLSSNR.Intensity", requestedSettings.intensity},
            {"DLSSNR.LocalStructureStrength", requestedSettings.localStructure},
            {"DLSSNR.LocalToneStrength", requestedSettings.localTone},
            {"DLSSNR.SkinStructureStrength", requestedSettings.skinStructure}})
        {
            float actual = 0;
            const auto result = g_vk.capabilityParams->Get(field.first, &actual);
            const bool matches = result == NVSDK_NGX_Result_Success && actual == field.second;
            floatsValid &= matches;
            if (cfg.DlssNrDiagnostics.value_or_default() != 0)
                VkAudit::Write("event=nr_parameter key=%s requested=%.9g readback=%.9g result=%u matched=%d",
                               field.first, field.second, actual, (unsigned)result, matches);
        }
        if (!floatsValid) { Fail("NR float parameter round-trip failed; update the matching D18 forwarder."); return; }
        g_vk.reset = true;
    }

    // -----------------------------------------------------------------------------------------
    // Encode: the frame the upscaler wrote -> a display-referred proxy, plus an untouched copy
    // -----------------------------------------------------------------------------------------

    const unsigned int createFlags = featureFlags>=0 ? unsigned(featureFlags) : GameCreateFlags(params);
    const bool gameSaysHdr = (createFlags & NVSDK_NGX_DLSS_Feature_Flags_IsHDR) != 0;
    const bool depthInverted = (createFlags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) != 0;

    // Both have to agree. A game can set the HDR flag on a buffer that cannot hold open-ended light,
    // and encoding an already tone-mapped frame a second time looks washed out and banded.
    const bool linearHdr = gameSaysHdr && FormatCanHoldLinearHdr(colour->Resource.ImageViewInfo.Format);

    // The slider only. The exposure source rides on the D3D12 meter's readback, which has no Vulkan
    // counterpart yet, so this path is deliberately manual rather than quietly reading nothing.
    if(g_vk.exposureReadback)g_vk.exposureReadback->Poll();
    nativeExposureReady.store(g_vk.exposureReadback && g_vk.exposureReadback->value>0,std::memory_order_relaxed);
    VkImageLayout exposureLayout{};
    // Missing/unknown exposure remains manual. Never infer GENERAL from a resource pointer.
    if(ReadableExposure(cmdBuffer,exposure,exposureLayout) && havePre && std::isfinite(preExposure) && preExposure>0){
        if(!g_vk.exposureReadback && !g_vk.exposureAllocationAttempted){
            g_vk.exposureAllocationAttempted = true;
            auto reader=std::make_unique<NativeExposureVk>();
            if(reader->Init(device,physicalDevice)&&CreateImage(g_vk.exposureCopy,1,1,VK_FORMAT_R32G32B32A32_SFLOAT,true))
                g_vk.exposureReadback=std::move(reader);
            else LOG_WARN("D18 exposure readback unavailable; manual paper white until NR resources rebuild.");
        }
        if(g_vk.exposureReadback&&!g_vk.exposureReadback->pending){
            const auto& view=exposure->Resource.ImageViewInfo;
            DlssNrConstants copy{};copy.Mode=DlssNrMode_Downsample;copy.Width=copy.Height=copy.SourceWidth=copy.SourceHeight=1;
            TransitionForeign(cmdBuffer,view.Image,view.SubresourceRange,exposureLayout,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Transition(cmdBuffer,g_vk.exposureCopy,VK_IMAGE_LAYOUT_GENERAL);
            if(g_vk.pass->Dispatch(cmdBuffer,copy,1,1,view.ImageView,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,
                g_vk.exposureCopy.view,VK_NULL_HANDLE,VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT) &&
                g_vk.exposureReadback->Begin(lease,preExposure)){
                Transition(cmdBuffer,g_vk.exposureCopy,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                g_vk.exposureReadback->Copy(cmdBuffer,g_vk.exposureCopy.image);
            }
            TransitionForeign(cmdBuffer,view.Image,view.SubresourceRange,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,exposureLayout);
        }
    }
    if (g_vk.failed) return;
    const float whitePoint = cfg.DlssNrWhitePointFromExposure.value_or(false) && ExposureReadyVk()
        ? std::clamp(g_vk.exposureReadback->pre/g_vk.exposureReadback->value*cfg.DlssNrWhitePointScale.value_or_default(),0.01f,4096.0f)
        : cfg.DlssNrWhitePointScale.value_or_default();
    const uint32_t requestedEncoding = cfg.DlssNrHighlightEncoding.value_or_default();
    const uint32_t highlightEncoding = linearHdr && requestedEncoding <= 2 ? requestedEncoding : 0u;
    if (g_vk.highlightEncoding != highlightEncoding)
    {
        g_vk.highlightEncoding = highlightEncoding;
        g_vk.reset = true; // discard history encoded with the previous curve
        if (cfg.DlssNrDiagnostics.value_or_default() != 0)
            VkAudit::Write("event=highlight_encoding mode=%u history_reset=1", highlightEncoding);
    }

    static bool saidEncoding = false;

    if (!saidEncoding)
    {
        saidEncoding = true;
        LOG_INFO("DLSS-NR Vulkan: the game's buffer is {} (flag {}, format {}), depth {}",
                 linearHdr ? "linear HDR" : "already tone-mapped", gameSaysHdr ? "set" : "clear",
                 (int) colour->Resource.ImageViewInfo.Format, depthInverted ? "inverted" : "normal");
    }

    DlssNrConstants encode {};
    encode.Mode = DlssNrMode_Encode;
    encode.HighlightEncoding = highlightEncoding;
    encode.RelativeColour = cfg.DlssNrRelativeColour.value_or_default() ? 1u : 0u;
    encode.Width = width;
    encode.Height = height;
    encode.WhitePoint = whitePoint;
    encode.Passthrough = linearHdr ? 0u : 1u;
    encode.TransferStrength = cfg.DlssNrTransferStrength.value_or_default();
    encode.ColourStrength = cfg.DlssNrColourStrength.value_or_default();
    encode.MaxRatio = cfg.DlssNrMaxRatio.value_or_default();
    encode.Transfer = cfg.DlssNrTransfer.value_or_default();
    encode.DebugScale = cfg.DlssNrWhitePointScale.value_or_default();
    encode.GuideWidth = guideWidth;
    encode.GuideHeight = guideHeight;
    // Full-resolution contract, also used by the shared diagnostic views.
    encode.NetworkRatioX = requestedSettings.networkRatio<1 ? float(std::max(16u,unsigned(width*requestedSettings.networkRatio+0.5f)&~15u))/width : 1;
    encode.NetworkRatioY = requestedSettings.networkRatio<1 ? float(std::max(8u,unsigned(height*requestedSettings.networkRatio+0.5f)&~7u))/height : 1;
    encode.SourceWidth = width;
    encode.SourceHeight = height;
    encode.CompareZoom = std::max(1.0f, cfg.DlssNrCompareZoom.value_or_default());

    const VkImageSubresourceRange colourRange = colour->Resource.ImageViewInfo.SubresourceRange;
    bool captureFrame=false;
    if(ColourCapture::requested.exchange(false)) {
        if(g_vk.capture) ColourCapture::Status("Batch already active; keep NR on until completion");
        else {
            auto capture=std::make_unique<ColourCapture::Batch>();
            if(capture->Init(device,physicalDevice,width,height) &&
               CreateImage(g_vk.captureImage,width,height,VK_FORMAT_R32G32B32A32_SFLOAT,true))
                g_vk.capture=std::move(capture);
            else ColourCapture::Status("Capture allocation failed or frame exceeds 512 MiB limit; NR unchanged");
        }
    }
    if(g_vk.capture) {
        float exposureScale=0;const bool haveScale=params->Get(NVSDK_NGX_Parameter_DLSS_Exposure_Scale,&exposureScale)==NVSDK_NGX_Result_Success;
        auto scalar=[](bool present,float value){return present&&std::isfinite(value)?std::to_string(value):std::string("null");};
        std::ostringstream meta;
        meta<<"{\"schema\":1,\"api\":\"Vulkan\",\"width\":"<<width<<",\"height\":"<<height
            <<",\"frame\":"<<g_vk.frames<<",\"tick\":"<<GetTickCount64()<<",\"epoch\":"<<lease.epoch
            <<",\"flags\":"<<createFlags<<",\"displaySpace\":"<<ColourCapture::displaySpace.load()
            <<",\"preExposure\":"<<scalar(havePre,preExposure)<<",\"exposureScale\":"<<scalar(haveScale,exposureScale)
            <<",\"exposureTexturePresent\":"<<(exposure?"true":"false")<<",\"exposureTextureValue\":"
            <<scalar(ExposureReadyVk(),g_vk.exposureReadback?g_vk.exposureReadback->value:0)
            <<",\"exposureSamplePre\":"<<scalar(ExposureReadyVk(),g_vk.exposureReadback?g_vk.exposureReadback->pre:0)
            <<",\"exposureReadback\":\""<<(ExposureReadyVk()?"ready":"unavailable_or_pending")<<"\",\"whitePoint\":"<<whitePoint
            <<",\"networkRatio\":"<<requestedSettings.networkRatio<<",\"preset\":"<<requestedSettings.preset
            <<",\"linearResolve\":"<<requestedSettings.linearResolve<<",\"linearColorInput\":"<<requestedSettings.linearColorInput
            <<",\"customFilter\":"<<requestedSettings.customFilter<<",\"catmullRom\":"<<requestedSettings.catmullRom
            <<",\"encoding\":"<<highlightEncoding<<",\"passthrough\":"<<encode.Passthrough
            <<",\"colourStrength\":"<<encode.ColourStrength<<",\"detailStrength\":"<<encode.TransferStrength
            <<",\"relativeColour\":"<<encode.RelativeColour
            <<",\"maxRatio\":"<<encode.MaxRatio<<",\"mode\":"<<mode<<",\"historyReset\":"<<(g_vk.reset||gameReset?1:0)
            <<",\"intensity\":"<<cfg.DlssNrIntensity.value_or_default()<<",\"style\":"<<cfg.DlssNrStyle.value_or_default()
            <<",\"localStructure\":"<<cfg.DlssNrLocalStructure.value_or_default()<<",\"localTone\":"<<cfg.DlssNrLocalTone.value_or_default()
            <<",\"skinStructure\":"<<cfg.DlssNrSkinStructure.value_or_default()<<",\"autoMask\":"<<(cfg.DlssNrAutoMask.value_or_default()?1:0)
            <<",\"debugView\":"<<cfg.DlssNrDebugView.value_or_default()<<",\"compare\":"<<cfg.DlssNrCompare.value_or_default()
            <<",\"sourceFormat\":"<<int(colour->Resource.ImageViewInfo.Format)
            <<",\"stages\":[\"sr_rgba32f\",\"proxy_rgba16f\",\"model_rgba16f\",\"composed_rgba32f\"]}";
        auto path=Util::DllPath().parent_path()/L"D18ColourCaptures"/(std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(g_vk.frames));
        captureFrame=g_vk.capture->Begin(lease,meta.str(),path);
    }
    auto snapshot=[&](unsigned stage){
        TransitionForeign(cmdBuffer,colour->Resource.ImageViewInfo.Image,colourRange,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Transition(cmdBuffer,g_vk.captureImage,VK_IMAGE_LAYOUT_GENERAL);
        auto copy=encode;copy.Mode=DlssNrMode_Downsample;
        bool ok=g_vk.pass->Dispatch(cmdBuffer,copy,width,height,colour->Resource.ImageViewInfo.ImageView,
            VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,g_vk.captureImage.view,VK_NULL_HANDLE,
            VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT);
        if(ok){Transition(cmdBuffer,g_vk.captureImage,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);g_vk.capture->Copy(cmdBuffer,g_vk.captureImage.image,stage);}
        TransitionForeign(cmdBuffer,colour->Resource.ImageViewInfo.Image,colourRange,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
        return ok;
    };
    if(captureFrame && !snapshot(0)) captureFrame=false;

    // Open the measurement. Reset immediately before writing: a query pool slot must be reset before
    // it is written again, and doing it here rather than at the end keeps the two in one place.
    const uint32_t timingSlot = (uint32_t) (g_vk.timedFrames % kTimingSlots);

    if (g_vk.queryPool != VK_NULL_HANDLE)
    {
        vkCmdResetQueryPool(cmdBuffer, g_vk.queryPool, timingSlot * 2, 2);
        vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_vk.queryPool, timingSlot * 2);
    }

    // The game's colour is read here and written at the end. Its layout on arrival is GENERAL, which
    // is what NGX requires of a resource it is handed, so it is left alone.
    Transition(cmdBuffer, g_vk.proxy, VK_IMAGE_LAYOUT_GENERAL);
    Transition(cmdBuffer, g_vk.keep, VK_IMAGE_LAYOUT_GENERAL);
    TransitionForeign(cmdBuffer,colour->Resource.ImageViewInfo.Image,colourRange,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (!g_vk.pass->Dispatch(cmdBuffer, encode, width, height, colour->Resource.ImageViewInfo.ImageView,
                             VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, g_vk.proxy.view, g_vk.keep.view,
                             g_vk.proxy.format,g_vk.keep.format))
    {
        TransitionForeign(cmdBuffer,colour->Resource.ImageViewInfo.Image,colourRange,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
        Fail("the encode dispatch failed");
        return;
    }

    // -----------------------------------------------------------------------------------------
    // The model
    // -----------------------------------------------------------------------------------------

    Transition(cmdBuffer, g_vk.output, VK_IMAGE_LAYOUT_GENERAL);

    // SR/FG may describe a guide as writable. NR only samples these guides;
    // do not inherit another feature's access contract or mutate its wrapper.
    g_vk.nrDepth = *depth;
    g_vk.nrMotion = *motion;
    g_vk.nrDepth.ReadWrite = false;
    g_vk.nrMotion.ReadWrite = false;
    if (mode == 2 && (g_vk.reset || g_vk.frames % 600 == 0))
        VkAudit::Write("event=nr_guide_access depth_source_rw=%d motion_source_rw=%d nr_rw=0 output=%p motion=%p",
                       depth->ReadWrite, motion->ReadWrite,
                       (void*)colour->Resource.ImageViewInfo.Image, (void*)motion->Resource.ImageViewInfo.Image);

    auto* modelColor=&g_vk.proxy.ngx;
    if(mode==2 && requestedSettings.customFilter){
        auto filter=encode;filter.Mode=DlssNrMode_ColorPrefilter;filter.CatmullRomInput=requestedSettings.catmullRom;
        Transition(cmdBuffer,g_vk.proxy,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Transition(cmdBuffer,g_vk.filtered,VK_IMAGE_LAYOUT_GENERAL);
        if(!g_vk.pass->Dispatch(cmdBuffer,filter,width,height,g_vk.proxy.view,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,
            g_vk.filtered.view,VK_NULL_HANDLE,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT))
        { Fail("model Color prefilter dispatch failed"); return; }
        Transition(cmdBuffer,g_vk.filtered,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        modelColor=&g_vk.filtered.ngx;
    }
    const int evaluated = mode==1 ? 1 : g_vk.evaluate(
        (void*) cmdBuffer, g_vk.feature, g_vk.capabilityParams, modelColor, &g_vk.nrDepth, &g_vk.nrMotion, &g_vk.output.ngx, width,
        height, guideWidth, guideHeight, depthInverted ? 1 : 0, (g_vk.reset || gameReset) ? 1 : 0,
        cfg.DlssNrIntensity.value_or_default(), (int) cfg.DlssNrStyle.value_or_default(),
        cfg.DlssNrLocalStructure.value_or_default(), cfg.DlssNrLocalTone.value_or_default(),
        cfg.DlssNrSkinStructure.value_or_default(), cfg.DlssNrAutoMask.value_or_default() ? 1 : 0, mvScaleX, mvScaleY);

    if (evaluated != 1)
    {
        TransitionForeign(cmdBuffer,colour->Resource.ImageViewInfo.Image,colourRange,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
        LOG_ERROR("DLSS-NR Vulkan: evaluate returned {}", evaluated);
        Fail("the model refused to evaluate");
        return;
    }

    // -----------------------------------------------------------------------------------------
    // Resolve: proxy + the model's answer + the untouched copy -> the frame
    // -----------------------------------------------------------------------------------------

    DlssNrConstants resolve = encode;
    resolve.Mode = DlssNrMode_Resolve;
    resolve.DebugView = cfg.DlssNrDebugView.value_or_default();
    resolve.CompareMode = cfg.DlssNrCompare.value_or_default();
    resolve.CompareSplit = cfg.DlssNrCompareSplit.value_or_default();
    resolve.CompareSwap = cfg.DlssNrCompareSwap.value_or_default() ? 1u : 0u;
    if(mode==1) resolve.TransferStrength=0.0f;

    Transition(cmdBuffer, g_vk.proxy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmdBuffer, g_vk.output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmdBuffer, g_vk.keep, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    TransitionForeign(cmdBuffer,colour->Resource.ImageViewInfo.Image,colourRange,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);

    if (!g_vk.pass->Dispatch(cmdBuffer, resolve, width, height, g_vk.proxy.view, mode==1?g_vk.proxy.view:g_vk.output.view, g_vk.keep.view,
                             VK_NULL_HANDLE, colour->Resource.ImageViewInfo.ImageView, VK_NULL_HANDLE,
                             colour->Resource.ImageViewInfo.Format,VK_FORMAT_R16G16B16A16_SFLOAT))
    {
        Fail("the resolve dispatch failed");
        return;
    }
    g_vk.reset = false;
    if(captureFrame) {
        for(auto pair:{std::pair<OwnedImage*,unsigned>{mode==2&&requestedSettings.customFilter?&g_vk.filtered:&g_vk.proxy,1u},{mode==1?&g_vk.proxy:&g_vk.output,2u}}){
            auto layout=pair.first->layout;Transition(cmdBuffer,*pair.first,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            g_vk.capture->Copy(cmdBuffer,pair.first->image,pair.second);Transition(cmdBuffer,*pair.first,layout);
        }
        if(snapshot(3))g_vk.capture->Seal(cmdBuffer);
    }
    g_vk.frames++;
    if(g_vk.frames<=120 || g_vk.frames%600==0)
        VkAudit::Write("event=nr_frame frame=%llu mode=%u width=%u height=%u result=%d",g_vk.frames,mode,width,height,evaluated);

    // Close it, and read the pair from three frames ago -- retired by now, so the read does not wait.
    if (g_vk.queryPool != VK_NULL_HANDLE)
    {
        vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_vk.queryPool, timingSlot * 2 + 1);
        g_vk.timedFrames++;

        if (g_vk.timedFrames > kTimingSlots)
        {
            const uint32_t readSlot = (uint32_t) (g_vk.timedFrames % kTimingSlots);
            uint64_t ticks[2] = {};

            // Without WAIT: a slot this old is retired, and if it somehow is not, NOT_READY is the
            // right answer rather than a stall.
            if (vkGetQueryPoolResults(device, g_vk.queryPool, readSlot * 2, 2, sizeof(ticks), ticks, sizeof(uint64_t),
                                      VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
                ticks[1] > ticks[0])
            {
                const double ms = (double) (ticks[1] - ticks[0]) * (double) g_vk.timestampPeriod / 1e6;

                // A pass that appears to have taken over a second did not; the queue was reset under
                // it or the pair straddled a device change.
                if (ms > 0.0 && ms < 1000.0)
                    g_vk.lastGpuTime = ms;
            }
        }
    }

    static bool reported = false;

    if (!reported && g_vk.frames > 2)
    {
        reported = true;
        LOG_INFO("DLSS-NR Vulkan: running natively at {}x{}, guides {}x{}", width, height, guideWidth, guideHeight);
    }
}

void ShutdownVk()
{
    nativeExposureReady.store(false,std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_vkMutex);
    retiredStates.push_back(std::make_unique<VkState>(std::move(g_vk)));
    g_vk=VkState{};
    CollectRetired();
}

void ShutdownDeviceVk(VkDevice device)
{
    nativeExposureReady.store(false,std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_vkMutex);
    bool owned=g_vk.device==device;
    for(const auto& state:retiredStates) owned|=state->device==device;
    if(!owned) return;
    const auto idle=vkDeviceWaitIdle(device);
    if(idle!=VK_SUCCESS && idle!=VK_ERROR_DEVICE_LOST) return;
    HMODULE forwarder=g_vk.device==device ? g_vk.forwarder : nullptr;
    for(const auto& state:retiredStates)if(state->device==device && state->forwarder)forwarder=state->forwarder;
    if(g_vk.device==device){DestroyState(g_vk);g_vk=VkState{};}
    for(auto it=retiredStates.begin();it!=retiredStates.end();)
        if((*it)->device==device){DestroyState(**it);it=retiredStates.erase(it);}else ++it;
    VkAudit::Write("event=nr_device_shutdown result=%d",int(idle));
    if(forwarder)
    {
        auto shutdown=(int(__cdecl*)(void*))GetProcAddress(forwarder,"dlssnr_vk_shutdown");
        if(shutdown)VkAudit::Write("event=nr_runtime_shutdown result=%d",shutdown((void*)device));
    }
}
} // namespace DlssNr
