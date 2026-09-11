#include "pch.h"
#include <dlssnr/PerformanceMonitor.h>
#include <dlssnr/ReGameProfile.h>
#include <dlssnr/NativeFgStatus.h>
#include <framegen/VulkanFgFrame.h>
#include "menu_common.h"
#include "D18ChineseFont.h"
#include <menu/menu_overlay_base.h>

#include <algorithm>
#include <cfloat>

#include <dlssnr/DlssNr.h>
#include <dlssnr/NativeSrStatus.h>
#include <dlssnr/Submission.h>
#include <dlssnr/DlssNrFeature_Vk.h>
#include <dlssnr/NativeControl.h>
#include <dlssnr/Diagnostics.h>

#include "input/input_system.h"

#include "font/Hack_Compressed.h"

#include <proxies/XeSS_Proxy.h>
#include <proxies/XeFG_Proxy.h>
#include <proxies/FfxApi_Proxy.h>
#include <proxies/Streamline_Proxy.h>

#include <framegen/nvngx/Nvngx_FG.h>

#include <nvapi/fakenvapi.h>
#include <hooks/Reflex_Hooks.h>

#include <version_check.h>

#include <upscaler_time/UpscalerTime_Vk.h>

#include <imgui/imgui_internal.h>
#include <imgui/ImGuiNotify.hpp>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_uwp.h>

#include <mutex>
#include <cstdarg>

#include <array>
#include <chrono>
#include <memory>
#include <type_traits>
#include <misc/IdentifyGpu.h>
#include <hooks/Xell_Hooks.h>
#include <low_latency/input/input_common.h>

#define MARK_ALL_BACKENDS_CHANGED()                                                                                    \
    for (auto& singleChangeBackend : State::Instance().changeBackend)                                                  \
        singleChangeBackend.second = true;

static float fontSize = 14.0f; // just changing this doesn't make other elements scale ideally
static ImVec2 overlaySize(0.0f, 0.0f);
static ImVec2 overlayPosition(-1000.0f, -1000.0f);
static bool _hdrTonemapApplied = false;
static ImVec4 SdrColors[ImGuiCol_COUNT];

static bool inputMenu = false;
static bool inputFG = false;
static bool inputFps = false;
static bool inputFpsCycle = false;
static uint64_t lastInputTick = 0;
constexpr uint64_t debounceThreshold = 1000;

static bool hasGamepad = false;
static bool ffxInitTried = false;
static bool xefgInitTried = false;
static std::string windowTitle;
static std::string selectedUpscalerName = "";
static Upscaler currentBackend = Upscaler::Reset;
static std::string currentBackendName = "";
static int refreshRate = 0;
static ImVec2 lastPosition(-1000.0f, -1000.0f);
static bool d18WindowSizeInitialized = false;

static ImVec2 splashPosition(-1000.0f, -1000.0f);
static ImVec2 splashSize(0.0f, 0.0f);
static double splashStart = 0.0;
static double splashLimit = 0.0;

static std::string updateNoticeTag;
static std::string updateNoticeUrl;
static float lastMenuScale = 0.0f;
static CustomOptional<uint32_t> comboPreset { 0 };
static int lastKey = 0;
static bool inputDlssNr = false;
static bool capturingKey = false;

template <typename T, size_t N> struct RingBuffer
{
    std::array<T, N> data {};
    size_t head { 0 };
    size_t count { N };
    double sum { 0.0 };

    RingBuffer() { data.fill(static_cast<T>(0)); }

    void Push(T v)
    {
        if (count == N)
        {
            sum -= data[head];
        }
        else
        {
            ++count;
        }
        data[head] = v;
        sum += v;
        head = (head + 1) % N;
    }

    size_t Size() const { return N; }

    T At(size_t i) const
    {
        size_t start = head;
        return data[(start + i) % N];
    }

    float Average() const { return static_cast<float>(sum / static_cast<double>(N)); }
};

const int plotWidth = 360;
static RingBuffer<float, plotWidth> gFrameTimes;
static RingBuffer<float, plotWidth> gUpscalerTimes;

struct FsExistsCache
{
    std::wstring lastPath;
    bool cached { false };
    std::chrono::steady_clock::time_point nextRefresh { std::chrono::steady_clock::time_point::min() };
    std::chrono::milliseconds interval { 2000 };

    bool Get(const std::filesystem::path& path)
    {
        auto now = std::chrono::steady_clock::now();
        if (path != lastPath || now >= nextRefresh)
        {
            lastPath = path;
            cached = std::filesystem::exists(path);
            nextRefresh = now + interval;
        }
        return cached;
    }
};

static FsExistsCache nukemsExists;
static FsExistsCache enablerExists;

struct FlagDefinition
{
    std::string name;
    uint32_t mask;
    std::string description;
};

inline std::string StrFmt(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = std::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    std::string out(len, '\0');
    va_start(args, fmt);
    std::vsnprintf(out.data(), len + 1, fmt, args);
    va_end(args);
    return out;
}

void MenuCommon::UpdateManualInput(HWND targetHwnd)
{
    OptiInput::BeginFrame(targetHwnd);

    const auto config = Config::Instance();

    auto CheckShortcut = [&](int vk, bool& inputFlag, const char* logMessage)
    {
        if (inputFlag)
            return;

        if (vk <= 0 || vk >= 256)
            return;

        if (OptiInput::IsKeyReleased(vk))
        {
            lastKey = vk;
            // receivingWmInputs = false;
            inputFlag = true;
            LOG_DEBUG("{}", logMessage);
        }
    };

    const auto currentTick = GetTickCount64();
    const bool canAcceptInputs = lastInputTick + debounceThreshold < currentTick;

    if (!capturingKey && canAcceptInputs)
    {
        CheckShortcut(config->ShortcutKey.value_or_default(), inputMenu, "Menu key pressed, will be switching menu");
        CheckShortcut(config->FpsShortcutKey.value_or_default(), inputFps, "Menu key pressed, will be switching FPS");
        CheckShortcut(config->FGShortcutKey.value_or_default(), inputFG, "Menu key pressed, will be switching FG mode");
        CheckShortcut(config->FpsCycleShortcutKey.value_or_default(), inputFpsCycle,
                      "Menu key pressed, will be switching FPS mode");
        CheckShortcut(config->DlssNrToggleKey.value_or_default(), inputDlssNr,
                      "Neural Rendering key pressed, will be toggling the pass");
    }
    else if (capturingKey)
    {
        lastInputTick = currentTick;
    }

    lastKey = OptiInput::GetLastPressedKey();
}

void MenuCommon::ShowTooltip(const char* tip)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        D18Ui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void MenuCommon::ShowHelpMarker(const char* tip)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ShowTooltip(tip);
}

void MenuCommon::ShowResetButton(CustomOptional<bool, NoDefault>* initFlag, std::string buttonName)
{
    ImGui::SameLine();

    ImGui::BeginDisabled(!initFlag->has_value());

    if (ImGui::Button(buttonName.c_str()))
    {
        initFlag->reset();
        ReInitUpscaler();
    }

    ImGui::EndDisabled();
}

inline void MenuCommon::ReInitUpscaler()
{
    if (!State::Instance().currentFeature)
        return;

    if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSSD)
        State::Instance().newBackend = Upscaler::DLSSD;
    else
        State::Instance().newBackend = currentBackend;

    MARK_ALL_BACKENDS_CHANGED();
}

void MenuCommon::SeparatorWithHelpMarker(const char* label, const char* tip)
{
    auto marker = "(?) ";
    ImGui::SeparatorTextEx(0, label, ImGui::FindRenderedTextEnd(label),
                           D18Ui::CalcTextSize(marker, ImGui::FindRenderedTextEnd(marker)).x);
    ShowHelpMarker(tip);
}

class Keybind
{
    std::string name;
    int id;
    bool waitingForKey = false;

  public:
    Keybind(std::string name, int id) : name(name), id(id) {}

    static std::string KeyNameFromVirtualKeyCode(USHORT virtualKey)
    {
        if (virtualKey == (USHORT) UnboundKey)
            return "Unbound";

        UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);

        // Keys like Home would display as Num 0 without this fix
        switch (virtualKey)
        {
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NUMLOCK:
        case VK_DIVIDE:
        case VK_RCONTROL:
        case VK_RMENU:
            scanCode |= 0xE000;
            break;
        }

        LONG lParam = (scanCode & 0xFF) << 16;
        if (scanCode & 0xE000)
            lParam |= 1 << 24;

        wchar_t buf[64] = {};
        if (GetKeyNameTextW(lParam, buf, static_cast<int>(std::size(buf))) != 0)
            return wstring_to_string(buf);

        return "Unknown";
    }

    void Render(CustomOptional<int>& configKey)
    {
        ImGui::PushID(id);
        if (D18Ui::Button(name.c_str()))
        {
            waitingForKey = true;
            capturingKey = true;
            lastKey = 0;
        }
        ImGui::PopID();

        if (waitingForKey)
        {
            ImGui::SameLine();
            D18Ui::Text("Press any key...");

            if (lastKey == 0 || lastKey == VK_LBUTTON || lastKey == VK_RBUTTON || lastKey == VK_MBUTTON)
                return;

            if (lastKey == VK_ESCAPE)
            {
                waitingForKey = false;
                capturingKey = false;
                return;
            }

            if (lastKey == VK_BACK)
                lastKey = UnboundKey;

            configKey = lastKey;
            waitingForKey = false;
            capturingKey = false;
            return;
        }

        ImGui::SameLine();
        D18Ui::Text(KeyNameFromVirtualKeyCode(configKey.value_or_default()).c_str());

        ImGui::SameLine();
        ImGui::PushID(id);
        if (D18Ui::Button("R"))
        {
            configKey.reset();
        }
        ImGui::PopID();
    }
};

Upscaler MenuCommon::GetBackendCode(const API api)
{
    if (auto feature = State::Instance().currentFeature)
        return feature->GetUpscalerType();

    Upscaler upscaler;

    if (api == DX11)
        upscaler = Config::Instance()->Dx11Upscaler.value_or_default();
    else if (api == DX12)
        upscaler = Config::Instance()->Dx12Upscaler.value_or_default();
    else
        upscaler = Config::Instance()->VulkanUpscaler.value_or_default();

    return upscaler;
}

void MenuCommon::GetCurrentBackendInfo(const API api, Upscaler& upscaler, std::string* name)
{
    upscaler = GetBackendCode(api);
    *name = UpscalerDisplayName(upscaler, api);
}

void MenuCommon::RenderUpscalerCombo(const API api, Upscaler currentUpscaler, const std::vector<Upscaler>& options)
{
    auto primaryGpu = IdentifyGpu::getPrimaryGpu();

    // Determine display name
    Upscaler targetBackend = State::Instance().newBackend;
    if (targetBackend == Upscaler::Reset)
        targetBackend = currentUpscaler;

    std::string selectedName = UpscalerDisplayName(targetBackend, api);

    if (ImGui::BeginCombo("##UpscalerCombo", selectedName.c_str()))
    {
        for (auto opt : options)
        {
            // Check if GPU is capable of a given backend
            if (opt == Upscaler::DLSS && !primaryGpu.dlssCapable)
                continue;

            // Not all Intel GPUs support native DX11 XeSS but don't think we have a good way to check exactly
            if (opt == Upscaler::XeSS && api == API::DX11 && primaryGpu.vendorId != VendorId::Intel)
                continue;

            bool isSelected = (currentUpscaler == opt);
            if (ImGui::Selectable(UpscalerDisplayName(opt, api).c_str(), isSelected))
            {
                State::Instance().newBackend = opt;
            }
        }
        ImGui::EndCombo();
    }
}

void MenuCommon::AddDx11Backends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::DX11, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR22, Upscaler::FSR31, Upscaler::XeSS_on12, Upscaler::FSR21_on12,
                          Upscaler::FSR22_on12, Upscaler::FFX_on12, Upscaler::DLSS });
}

void MenuCommon::AddDx12Backends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::DX12, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR21, Upscaler::FSR22, Upscaler::FFX, Upscaler::DLSS });
}

void MenuCommon::AddVulkanBackends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::Vulkan, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR21, Upscaler::FSR22, Upscaler::FFX, Upscaler::FSR21_on12,
                          Upscaler::FFX_on12, Upscaler::DLSS });
}

template <HasDefaultValue B> void MenuCommon::AddResourceBarrier(std::string name, CustomOptional<int32_t, B>* value)
{
    const char* states[] = { "AUTO",
                             "COMMON",
                             "VERTEX_AND_CONSTANT_BUFFER",
                             "INDEX_BUFFER",
                             "RENDER_TARGET",
                             "UNORDERED_ACCESS",
                             "DEPTH_WRITE",
                             "DEPTH_READ",
                             "NON_PIXEL_SHADER_RESOURCE",
                             "PIXEL_SHADER_RESOURCE",
                             "STREAM_OUT",
                             "INDIRECT_ARGUMENT",
                             "COPY_DEST",
                             "COPY_SOURCE",
                             "RESOLVE_DEST",
                             "RESOLVE_SOURCE",
                             "RAYTRACING_ACCELERATION_STRUCTURE",
                             "SHADING_RATE_SOURCE",
                             "GENERIC_READ",
                             "ALL_SHADER_RESOURCE",
                             "PRESENT",
                             "PREDICATION",
                             "VIDEO_DECODE_READ",
                             "VIDEO_DECODE_WRITE",
                             "VIDEO_PROCESS_READ",
                             "VIDEO_PROCESS_WRITE",
                             "VIDEO_ENCODE_READ",
                             "VIDEO_ENCODE_WRITE" };
    const int values[] = { -1,  0,   1,     2,      4,      8,      16,      32,       64,   128,
                           256, 512, 1024,  2048,   4096,   8192,   4194304, 16777216, 2755, 192,
                           0,   310, 65536, 131072, 262144, 524288, 2097152, 8388608 };

    int selected = value->value_or(-1);

    const char* selectedName = "";

    for (int n = 0; n < 28; n++)
    {
        if (values[n] == selected)
        {
            selectedName = states[n];
            break;
        }
    }

    if (ImGui::BeginCombo(name.c_str(), selectedName))
    {
        if (ImGui::Selectable(states[0], !value->has_value()))
            value->reset();

        for (int n = 1; n < 28; n++)
        {
            if (ImGui::Selectable(states[n], selected == values[n]))
                *value = values[n];
        }

        ImGui::EndCombo();
    }
}

static uint32_t GetPresetIndex(IFeature* feature, bool dlssd = false)
{
    auto ratio = (float) feature->TargetWidth() / (float) feature->RenderWidth();

    if (!dlssd)
    {
        if (State::Instance().dlssPresetsOverridenByOpti)
        {
            LOG_DEBUG("DLSS Presets overridden by Opti, using Opti preset indices with ratio: {}", ratio);

            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetUltraPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetBalanced.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetQuality.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetUltraQuality.value_or_default());
            }
            else
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetDLAA.value_or_default());
            }
        }
        else if (State::Instance().dlssPresetsOverriddenExternally)
        {
            LOG_DEBUG("DLSS Presets overridden externally, using external preset index: {}",
                      State::Instance().dlssRenderPresetExternal);

            return State::Instance().dlssRenderPresetExternal;
        }
        else
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetUltraPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetBalanced;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetQuality;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetUltraQuality;
            }
            else
            {
                return State::Instance().dlssRenderPresetDLAA;
            }
        }
    }
    else
    {
        if (State::Instance().dlssdPresetsOverridenByOpti)
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetUltraPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetBalanced.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetQuality.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetUltraQuality.value_or_default());
            }
            else
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetDLAA.value_or_default());
            }
        }
        else if (State::Instance().dlssdPresetsOverriddenExternally)
        {
            return State::Instance().dlssdRenderPresetExternal;
        }
        else
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetUltraPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetBalanced;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetQuality;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetUltraQuality;
            }
            else
            {
                return State::Instance().dlssdRenderPresetDLAA;
            }
        }
    }

    return 0;
}

// TODO: disable presets based on the detected DLSS version
template <HasDefaultValue B> void MenuCommon::AddDLSSRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    // clang-format off
    static const std::vector<MenuOption<uint32_t>> presets = {
        { NVSDK_NGX_DLSS_Hint_Render_Preset_Default, "DEFAULT", 
            "Whatever the game uses" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_A, "PRESET A",
            "Intended for Performance/Balanced/Quality modes.\nAn older variant best suited to combat ghosting...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_B, "PRESET B",
            "Intended for Ultra Performance mode.\nSimilar to Preset A...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_C, "PRESET C",
            "Intended for Performance/Balanced/Quality modes.\nGenerally favors current frame information...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_D, "PRESET D",
            "Default preset for Performance/Balanced/Quality modes;\ngenerally favors image stability.\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_E, "PRESET E",
            "DLSS 3.7+, a better D preset\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_F, "PRESET F",
            "Default preset for Ultra Performance and DLAA modes\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_G, "PRESET G",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_H_Reserved, "PRESET H",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_I_Reserved, "PRESET I",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_J, "PRESET J",
            "Similar to preset K. Preset J might exhibit slightly\nless ghosting...\n1st Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_K, "PRESET K",
            "Default preset for DLAA/Balanced/Quality modes...\n1st Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_L, "PRESET L",
            "Default for Ultra Perf mode\n2nd Gen Transformers" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_M, "PRESET M",
            "Default for Perf mode\n2nd Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_N, "PRESET N",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_O, "PRESET O",
            "Unused" },
        { NV_PRESET_LATEST, "Latest",
            "Latest supported by the dll" }
    };
    // clang-format on

    PopulateCombo(name, *value, presets);
}

template <HasDefaultValue B> void MenuCommon::AddDLSSDRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    // We don't have DLSSD definitions so using raw values
    static const std::vector<MenuOption<uint32_t>> presets = {
        { 0, "DEFAULT", "Whatever the game uses" },
        { 1, "PRESET A", "Preset A\nRemoved on recent versions!" },
        { 2, "PRESET B", "Preset B\nRemoved on recent versions!" },
        { 3, "PRESET C", "Preset C\nRemoved on recent versions!" },
        { 4, "PRESET D", "Default model, Transformer" },
        { 5, "PRESET E", "Latest Transformer model\nMust use if DoF guide is needed" },
        { 6, "PRESET F", "Latest Transformer model\nMust use if DoF guide is needed" },
        { NV_PRESET_LATEST, "Latest", "Latest supported by the dll" }
    };

    PopulateCombo(name, *value, presets);
}

template <typename TStorage, typename T>
void MenuCommon::PopulateCombo(const std::string& name, TStorage& currentValue,
                               const std::vector<MenuOption<T>>& options)
{
    if (options.empty())
        return;

    // Assumes that different types mean that TStorage is std::optional
    T currentVal;
    if constexpr (std::is_same_v<TStorage, T>)
        currentVal = currentValue;
    else
        currentVal = currentValue.value_or(options[0].value);

    // Find the label for the currently selected item
    std::string preview = "Unknown";
    for (const auto& opt : options)
    {
        if (opt.value == currentVal)
        {
            preview = opt.label;
            break;
        }
    }

    if (D18Ui::BeginCombo(name.c_str(), preview.c_str()))
    {
        for (const auto& opt : options)
        {
            if (opt.hidden)
                continue;

            if (opt.disabled)
                ImGui::BeginDisabled();

            bool isSelected = (currentVal == opt.value);
            if (D18Ui::Selectable(opt.label.c_str(), isSelected))
                currentValue = opt.value;

            // Show tooltip for the individual item if it exists
            if (!opt.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", opt.tooltip.c_str());

            if (opt.disabled)
                ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
}

static ImVec4 toneMapColor(const ImVec4& color)
{
    if (State::Instance().isHdrActive ||
        (!Config::Instance()->OverlayMenu.value_or_default() && State::Instance().currentFeature != nullptr &&
         State::Instance().currentFeature->IsHdr()))
    {
        // Controls how strongly HDR/UI colors are pushed into the tone mapper before compression.
        // Higher values make colors brighter before mapping; lower values make the result dimmer.
        constexpr float exposure = 1.0f;

        // Blends between original color and fully tone-mapped color.
        // 0.0 = no tone mapping, 1.0 = full Reinhard compression.
        constexpr float strength = 1.0f;

        float peak = std::max(color.x, std::max(color.y, color.z));

        if (peak <= 0.0f)
            return color;

        float exposedPeak = peak * exposure;
        float mappedPeak = exposedPeak / (1.0f + exposedPeak);

        float reinhardScale = mappedPeak / peak;
        float scale = 1.0f + (reinhardScale - 1.0f) * strength;

        return ImVec4(color.x * scale, color.y * scale, color.z * scale, color.w);
    }

    return color;
}

static void MenuHdrCheck(ImGuiIO io)
{
    // If game is using HDR, apply tone mapping to the ImGui style
    if (State::Instance().isHdrActive ||
        (!Config::Instance()->OverlayMenu.value_or_default() && State::Instance().currentFeature != nullptr &&
         State::Instance().currentFeature->IsHdr()))
    {
        if (!_hdrTonemapApplied)
        {
            ImGuiStyle& style = ImGui::GetStyle();

            CopyMemory(SdrColors, style.Colors, sizeof(style.Colors));

            // Apply tone mapping to the ImGui style
            for (int i = 0; i < ImGuiCol_COUNT; ++i)
            {
                ImVec4 color = style.Colors[i];
                style.Colors[i] = toneMapColor(color);
            }

            _hdrTonemapApplied = true;
        }
    }
    else
    {
        if (_hdrTonemapApplied)
        {
            ImGuiStyle& style = ImGui::GetStyle();
            CopyMemory(style.Colors, SdrColors, sizeof(style.Colors));
            _hdrTonemapApplied = false;
        }
    }
}

static float MenuResolutionScale(ImGuiIO io)
{
    if (Config::Instance()->MenuScale.has_value())
        return Config::Instance()->MenuScale.value();

    // Calculate menu scale according to display resolution
    float y = State::Instance().screenHeight;

    if (io.DisplaySize.y != 0)
        y = (float) io.DisplaySize.y;

    // 1000p is minimum for 1.0 menu ratio
    float result = (float) ((int) (y / 108.0f)) / 10.0f;

    result = std::round(result * 10.0f) / 10.0f;

    if (result < 0.5f)
        result = 0.5f;

    if (result > 2.0f)
        result = 2.0f;

    return result;
}

inline static std::string GetSourceString(UINT source)
{
    switch (source)
    {
    case 1:
        return "RTV";
    case 2:
        return "SRV";
    case 4:
        return "UAV";
    case 8:
        return "OM";
    case 16:
        return "Ups";
    case 32:
        return "SCR";
    case 64:
        return "SGR";
    default:
        return std::format("{}", source);
    }
}

inline static std::string GetDispatchString(UINT source)
{
    switch (source)
    {
    case 512:
        return "DI";
    case 1024:
        return "DII";
    case 256:
        return "Disp";
    default:
        return std::format("{}", source);
    }
}

static void ApplyThemeStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();

    auto conf = Config::Instance();
    bool lightTheme = conf->LightTheme.value_or_default();

    style.WindowRounding = 2.0f;
    style.ChildRounding = 1.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    style.FrameBorderSize = lightTheme ? 1.0f : 0.0f;
    style.TabBorderSize = lightTheme ? 1.0f : 0.0f;

    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 10.0f;

    auto Clamp01 = [](float v) { return std::max(0.0f, std::min(v, 1.0f)); };

    auto Mix = [](const ImVec4& a, const ImVec4& b, float t, float alpha = 1.0f)
    { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha); };

    auto Luminance = [](const ImVec4& c) { return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f; };

    auto Saturate = [&](const ImVec4& color, float amount)
    {
        float lum = Luminance(color);

        return ImVec4(Clamp01(lum + (color.x - lum) * amount), Clamp01(lum + (color.y - lum) * amount),
                      Clamp01(lum + (color.z - lum) * amount), color.w);
    };

    ImVec4 accent = ImVec4(conf->MenuAccentColorR.value_or_default(), conf->MenuAccentColorG.value_or_default(),
                           conf->MenuAccentColorB.value_or_default(), 1.0f);

    ImVec4 bgAccent = ImVec4(conf->MenuBGColorR.value_or_default(), conf->MenuBGColorG.value_or_default(),
                             conf->MenuBGColorB.value_or_default(), 1.0f);

    float luminance = Luminance(accent);

    const ImVec4 bgDark = lightTheme ? ImVec4(0.80f, 0.82f, 0.86f, 1.00f) : ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    const ImVec4 bgMid = lightTheme ? ImVec4(0.89f, 0.91f, 0.95f, 1.00f) : ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    const ImVec4 bgLight = lightTheme ? ImVec4(0.96f, 0.97f, 0.99f, 1.00f) : ImVec4(0.14f, 0.14f, 0.15f, 1.00f);

    const ImVec4 textPrimary = lightTheme ? ImVec4(0.05f, 0.06f, 0.08f, 1.00f) : ImVec4(0.90f, 0.93f, 0.95f, 1.00f);
    const ImVec4 textDim = lightTheme ? ImVec4(0.22f, 0.25f, 0.31f, 1.00f) : ImVec4(0.54f, 0.58f, 0.62f, 1.00f);

    const ImVec4 borderCol = lightTheme ? ImVec4(0.35f, 0.40f, 0.50f, 1.00f) : ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
    const ImVec4 dimBg = lightTheme ? ImVec4(0.30f, 0.33f, 0.38f, 0.20f) : ImVec4(0.09f, 0.10f, 0.13f, 0.20f);
    const ImVec4 modalDimBg = lightTheme ? ImVec4(0.22f, 0.24f, 0.28f, 0.55f) : ImVec4(0.04f, 0.04f, 0.07f, 0.55f);

    // MenuBGColor: only background/surface tint.
    auto BgTint = [&](const ImVec4& base, float strength = 1.0f, float alpha = 1.0f)
    {
        float t = lightTheme ? (0.180f * strength) : (0.120f * strength);
        return Mix(base, bgAccent, t, alpha);
    };

    // MenuAccentColor: all visible interactive accent colors.
    auto AccentSoft = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.14f, alpha) : Mix(bgDark, accent, 0.32f, alpha); };

    auto AccentMed = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.42f, alpha) : Mix(bgDark, accent, 0.55f, alpha); };

    auto AccentStrong = [&](float alpha = 1.0f) { return ImVec4(accent.x, accent.y, accent.z, alpha); };

    const ImVec4 bgTitle = AccentSoft();

    auto SurfaceHover = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.12f, alpha) : Mix(bgLight, accent, 0.18f, alpha); };

    auto SurfaceActive = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.20f, alpha) : Mix(bgLight, accent, 0.28f, alpha); };

    auto TitleActive = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgTitle, accent, 0.18f, alpha) : Mix(bgTitle, accent, 0.16f, alpha); };

    auto PlotAccent = [&](float alpha = 1.0f)
    {
        if (lightTheme)
        {
            // Darken slightly for contrast on light bg — no channel floors
            return Mix(accent, ImVec4(0.00f, 0.00f, 0.00f, 1.00f), 0.20f, alpha);
        }

        // Brighten slightly for visibility on dark bg — no channel floors
        return Mix(accent, ImVec4(1.00f, 1.00f, 1.00f, 1.00f), 0.35f, alpha);
    };

    auto PlotAccentHovered = [&](float alpha = 1.0f)
    {
        if (lightTheme)
        {
            return Mix(PlotAccent(alpha), ImVec4(0.00f, 0.00f, 0.00f, 1.00f), 0.15f, alpha);
        }

        return Mix(PlotAccent(alpha), ImVec4(1.00f, 1.00f, 1.00f, 1.00f), 0.25f, alpha);
    };

    auto AccentReadable = [&](float alpha = 1.0f)
    {
        // Apply saturation boost and luminance correction only here,
        // so AccentStrong / AccentMed / AccentSoft stay true to the user's pick.
        ImVec4 a = Saturate(accent, lightTheme ? 1.35f : 1.25f);
        float lum = Luminance(a);

        if (lightTheme && lum > 0.72f)
            a = Mix(a, ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.35f, 1.0f);

        if (!lightTheme && lum < 0.25f)
            a = Mix(a, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.30f, 1.0f);

        return ImVec4(a.x, a.y, a.z, alpha);
    };

    ImVec4* c = ImGui::GetStyle().Colors;

    float minAlpha = Config::Instance()->MenuBGColorA.value_or_default() >= 0.5f
                         ? Config::Instance()->MenuBGColorA.value_or_default()
                         : 0.5f;

    c[ImGuiCol_Text] = textPrimary;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_TextLink] = AccentReadable();

    // MenuBGColor only.
    c[ImGuiCol_WindowBg] = BgTint(bgDark, 1.00f, Config::Instance()->MenuBGColorA.value_or_default());
    c[ImGuiCol_ChildBg] = BgTint(bgMid, 1.10f, minAlpha + 0.1f);
    c[ImGuiCol_PopupBg] =
        lightTheme ? BgTint(bgLight, 0.90f) : BgTint(ImVec4(0.09f, 0.10f, 0.13f, 0.97f), 0.90f, 0.97f);
    c[ImGuiCol_MenuBarBg] = BgTint(bgDark, 0.85f);
    c[ImGuiCol_DockingEmptyBg] = BgTint(bgDark, 0.75f);

    c[ImGuiCol_Border] = borderCol;
    c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Neutral background, not MenuBGColor.
    c[ImGuiCol_FrameBg] = BgTint(bgLight, 0.50f, minAlpha + 0.15f);
    c[ImGuiCol_FrameBgHovered] = SurfaceHover();
    c[ImGuiCol_FrameBgActive] = SurfaceActive();

    c[ImGuiCol_TitleBg] = BgTint(bgTitle, 0.40f);
    c[ImGuiCol_TitleBgActive] = TitleActive();
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(bgTitle.x, bgTitle.y, bgTitle.z, 0.75f);

    c[ImGuiCol_ScrollbarBg] = BgTint(bgDark, 0.60f, minAlpha + 0.2f);
    c[ImGuiCol_ScrollbarGrab] = AccentSoft();
    c[ImGuiCol_ScrollbarGrabHovered] = AccentMed();
    c[ImGuiCol_ScrollbarGrabActive] = AccentStrong();

    c[ImGuiCol_CheckMark] = AccentReadable();
    c[ImGuiCol_SliderGrab] = AccentMed();
    c[ImGuiCol_SliderGrabActive] = AccentReadable();
    c[ImGuiCol_InputTextCursor] = AccentReadable();

    c[ImGuiCol_Button] = AccentSoft();
    c[ImGuiCol_ButtonHovered] = AccentMed();
    c[ImGuiCol_ButtonActive] = AccentStrong();

    c[ImGuiCol_Header] = AccentSoft(0.90f);
    c[ImGuiCol_HeaderHovered] = AccentMed(0.95f);
    c[ImGuiCol_HeaderActive] = AccentStrong();

    c[ImGuiCol_Separator] = borderCol;
    c[ImGuiCol_SeparatorHovered] = AccentMed(0.85f);
    c[ImGuiCol_SeparatorActive] = AccentStrong();

    c[ImGuiCol_ResizeGrip] = AccentSoft(0.30f);
    c[ImGuiCol_ResizeGripHovered] = AccentStrong(0.70f);
    c[ImGuiCol_ResizeGripActive] = AccentStrong(0.95f);

    c[ImGuiCol_Tab] = AccentSoft();
    c[ImGuiCol_TabHovered] = AccentMed();
    c[ImGuiCol_TabSelected] = AccentSoft();
    c[ImGuiCol_TabSelectedOverline] = AccentStrong();
    c[ImGuiCol_TabDimmed] = BgTint(bgDark, 0.60f);
    c[ImGuiCol_TabDimmedSelected] = AccentSoft(0.75f);
    c[ImGuiCol_TabDimmedSelectedOverline] = borderCol;

    c[ImGuiCol_DockingPreview] = AccentStrong(0.70f);

    c[ImGuiCol_PlotLines] = PlotAccent();
    c[ImGuiCol_PlotLinesHovered] = PlotAccentHovered();
    c[ImGuiCol_PlotHistogram] = PlotAccent(0.85f);
    c[ImGuiCol_PlotHistogramHovered] = PlotAccentHovered();

    c[ImGuiCol_TableHeaderBg] = BgTint(bgMid, 0.80f, minAlpha + 0.25f);
    c[ImGuiCol_TableBorderStrong] = borderCol;
    c[ImGuiCol_TableBorderLight] = lightTheme ? ImVec4(0.68f, 0.72f, 0.80f, 1.00f) : AccentSoft();
    c[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = lightTheme ? ImVec4(0.00f, 0.00f, 0.00f, 0.045f) : ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    c[ImGuiCol_TreeLines] = borderCol;
    c[ImGuiCol_TextSelectedBg] = AccentMed(0.38f);
    c[ImGuiCol_DragDropTarget] = AccentStrong(0.90f);
    c[ImGuiCol_NavCursor] = AccentReadable();
    c[ImGuiCol_NavWindowingHighlight] = AccentStrong(0.70f);
    c[ImGuiCol_NavWindowingDimBg] = dimBg;
    c[ImGuiCol_ModalWindowDimBg] = modalDimBg;

    _hdrTonemapApplied = false;
    MenuHdrCheck(ImGui::GetIO());
}

static double lastTime = 0.0;
static double lastFrameTime = 0.0;
static UINT64 uwpTargetFrame = 0;

void MenuCommon::Present()
{
    _frameCount++;

    auto now = Util::MillisecondsNow();

    if (lastTime > 0.0)
        lastFrameTime = now - lastTime;

    lastTime = now;

    if (_handle != nullptr)
        UpdateManualInput(_handle);
}

struct VersionCheckStatus
{
    bool completed = false;
    bool updateAvailable = false;
    std::string latestTag;
    std::string latestUrl;
    std::string error;
};

struct MenuCommon::RenderMenuContext
{
    State& state;
    decltype(Config::Instance()) config;
    ImGuiIO& io;
    IFeature* currentFeature = nullptr;

    double now = 0.0;
    double frameTime = 0.0;
    double frameRate = 0.0;
    float menuResScale = 1.0f;
    float fpsScale = 1.0f;
    float averageFrameTime = 0.0f;
    float averageUpscalerFT = 0.0f;

    bool frameTimesCalculated = false;
    bool newFrame = false;

    VersionCheckStatus versionStatus;
    std::string currentVersionText;

    // Cached when the menu is visible and shared by RenderMainMenuWindow section helpers.
    std::unique_ptr<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>> primaryGpu;
};

enum class D18Health
{
    Off,
    Paused,
    Unobserved,
    Waiting,
    Active,
    Error,
};

static ImVec4 D18HealthColor(D18Health health)
{
    switch (health)
    {
    case D18Health::Active:
        return toneMapColor(ImVec4(0.20f, 0.95f, 0.48f, 1.0f));
    case D18Health::Waiting:
    case D18Health::Paused:
        return toneMapColor(ImVec4(1.0f, 0.70f, 0.18f, 1.0f));
    case D18Health::Error:
        return toneMapColor(ImVec4(1.0f, 0.26f, 0.22f, 1.0f));
    default:
        return toneMapColor(ImVec4(0.48f, 0.52f, 0.58f, 1.0f));
    }
}

static const char* D18HealthName(D18Health health)
{
    switch (health)
    {
    case D18Health::Unobserved:
        return "UNOBSERVED";
    case D18Health::Paused:
        return "PAUSED";
    case D18Health::Active:
        return "ACTIVE";
    case D18Health::Waiting:
        return "WAITING";
    case D18Health::Error:
        return "ERROR";
    default:
        return "OFF";
    }
}

static void RenderD18Indicator(const char* id, const char* label, D18Health health,
                               const std::string& detail, float scale)
{
    ImGui::PushID(id);
    ImGui::BeginChild("##status", ImVec2(0.0f, 62.0f * scale), true,
                      ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec4 color = D18HealthColor(health);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float radius = 5.0f * scale;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cursor.x + radius, cursor.y + ImGui::GetTextLineHeight() * 0.5f),
                                                radius, ImGui::ColorConvertFloat4ToU32(color));
    ImGui::Dummy(ImVec2(radius * 2.0f + 3.0f * scale, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    D18Ui::TextUnformatted(label);
    ImGui::SameLine();
    D18Ui::TextColored(color, "%s", D18HealthName(health));
    ImGui::PushTextWrapPos(0.0f);
    D18Ui::TextDisabled("%s", detail.c_str());
    ImGui::PopTextWrapPos();

    ImGui::EndChild();
    ImGui::PopID();
}

static void RenderD18PipelineNode(const char* id, const char* label, D18Health health,
                                  unsigned long long frameCount, float scale)
{
    ImGui::PushID(id);
    ImGui::BeginChild("##pipeline", ImVec2(0.0f, 50.0f * scale), true,
                      ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec4 color = D18HealthColor(health);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float radius = 4.0f * scale;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cursor.x + radius, cursor.y + ImGui::GetTextLineHeight() * 0.5f),
                                                radius, ImGui::ColorConvertFloat4ToU32(color));
    ImGui::Dummy(ImVec2(radius * 2.0f + 2.0f * scale, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    D18Ui::TextUnformatted(label);
    D18Ui::TextColored(color, "%s", D18HealthName(health));
    if (frameCount > 0)
    {
        ImGui::SameLine();
        D18Ui::TextDisabled("#%llu", frameCount);
    }

    ImGui::EndChild();
    ImGui::PopID();
}

static const char* D18ApiName(API api)
{
    switch (api)
    {
    case API::DX11:
        return "D3D11";
    case API::DX12:
        return "D3D12";
    case API::Vulkan:
        return "Vulkan";
    default:
        return "Waiting";
    }
}

static const char* D18FgInputName(FGInput input)
{
    switch (input)
    {
    case FGInput::DLSSG:
        return "Streamline DLSSG";
    case FGInput::NvngxFG:
        return "NVNGX DLSSG";
    case FGInput::Upscaler:
        return "Upscaler";
    case FGInput::FSRFG:
        return "FSR FG";
    case FGInput::FSRFG30:
        return "FSR 3.0 FG";
    default:
        return "None / game native";
    }
}

static const char* D18FgOutputName(FGOutput output)
{
    switch (output)
    {
    case FGOutput::DLSSG:
        return "DLSSG";
    case FGOutput::FSRFG:
        return "FSR FG";
    case FGOutput::XeFG:
        return "XeFG";
    default:
        return "None / game native";
    }
}

static constexpr const char* splashMessage = "D18 - DLSS Neural Rendering";

void MenuCommon::UpdateRenderTiming(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& now = ctx.now;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;

    if (config->OverlayMenu.value_or_default())
    {
        _frameCount++;

        // FPS & frame time calculation
        if (lastTime > 0.0)
        {
            frameTime = now - lastTime;
            frameRate = 1000.0 / frameTime;
        }

        lastTime = now;

        if (_handle != nullptr)
            UpdateManualInput(_handle);
    }
    else
    {
        if (state.activeFgInput == FGInput::NoFG || state.activeFgOutput == FGOutput::NoFG)
            MenuCommon::Present();

        frameTime = lastFrameTime;
        frameRate = 1000.0 / frameTime;
    }

    state.frameTimes.pop_front();
    state.frameTimes.push_back(frameTime);
}

void MenuCommon::UpdateMenuInputMode(RenderMenuContext& ctx)
{
    auto& io = ctx.io;

    // Moved here to prevent gamepad key replay
    if (_isVisible)
    {
        if (hasGamepad)
            io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

        io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    }
    else
    {
        capturingKey = false;
        hasGamepad = (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;
    }
}

void MenuCommon::HandleMenuShortcuts(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;

    // Handle Inputs
    {
        if (inputFG)
        {
            inputFG = false;

            if (state.activeFgInput != FGInput::NoFG && state.activeFgOutput != FGOutput::NoFG &&
                (state.currentFGSwapchain != nullptr || state.activeFgInput == FGInput::NvngxFG))
            {
                config->FGEnabled = !config->FGEnabled.value_or_default();
                LOG_DEBUG("FG toggle key pressed, setting FGEnabled to {}", config->FGEnabled.value_or_default());

                if (config->FGEnabled.value_or_default())
                    state.fgChanged = true;
            }
        }

        if (inputFps)
        {
            inputFps = false;
            config->ShowFps = !config->ShowFps.value_or_default();
        }

        if (inputDlssNr)
        {
            inputDlssNr = false;
            config->DlssNrEnabled = !config->DlssNrEnabled.value_or_default();
            LOG_DEBUG("Neural Rendering toggle key pressed, setting DlssNrEnabled to {}",
                      config->DlssNrEnabled.value_or_default());

            ImGuiToast toast { ImGuiToastType::Info, 2000 };
            toast.setTitle("DLSS Neural Rendering");
            toast.setContent(config->DlssNrEnabled.value_or_default() ? "On" : "Off");
            ImGui::InsertNotification(toast);
        }

        if (inputFpsCycle && config->ShowFps.value_or_default())
            config->FpsOverlayType = (FpsOverlay) ((config->FpsOverlayType.value_or_default() + 1) % FpsOverlay_COUNT);

        if (inputMenu)
        {
            inputMenu = false;
            _isVisible = !_isVisible;

            LOG_DEBUG("Menu key pressed, {0}", _isVisible ? "opening ImGui" : "closing ImGui");

            if (_isVisible)
            {
                io.ClearEventsQueue();
                io.ClearInputKeys();
                io.ClearInputMouse();

                OptiInput::ResetMenuInputTransientState();

                ApplyThemeStyle();

                refreshRate = Util::GetActiveRefreshRate(_handle);

                auto optiPath = std::filesystem::path(Config::Instance()->MainDllPath.value());
                state.artursFgFileAvailable = enablerExists.Get(optiPath / L"dlss-enabler-headless.dll");
                state.nukemsFgFileAvailable = nukemsExists.Get(optiPath / L"dlssg_to_fsr3_amd_is_better.dll");

                if (State::Instance().currentFeature != nullptr)
                {
                    if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSSD)
                        comboPreset = config->DLSSDRenderPresetForAll.value_or_default();
                    else if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSS)
                        comboPreset = config->RenderPresetForAll.value_or_default();
                }
            }
            else
            {
                ImGui::CloseCurrentPopup();

                _showMipmapCalcWindow = false;
                _showHudlessWindow = false;
            }

            io.MouseDrawCursor = _isVisible;
            io.WantCaptureKeyboard = _isVisible;
            io.WantCaptureMouse = _isVisible;
        }

        inputFpsCycle = false;
    }
}

void MenuCommon::UpdateVersionAndStartupNotifications(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& now = ctx.now;
    auto& versionStatus = ctx.versionStatus;

    constexpr double splashTime = 7000.0;
    constexpr int updateNoticeTime = 10000;

    // Version check state is copied while locked, then consumed by the UI render pass.
    {
        std::scoped_lock lock(state.versionCheckMutex);
        versionStatus.completed = state.versionCheckCompleted;
        versionStatus.updateAvailable = state.updateAvailable;
        versionStatus.latestTag = state.latestVersionTag;
        versionStatus.latestUrl = state.latestVersionUrl;
        versionStatus.error = state.versionCheckError;
    }

    ctx.currentVersionText = VersionCheck::CurrentVersionString();

    if (versionStatus.completed && versionStatus.updateAvailable && !versionStatus.latestTag.empty())
    {
        if (updateNoticeTag != versionStatus.latestTag)
        {
            updateNoticeTag = versionStatus.latestTag;
            updateNoticeUrl = versionStatus.latestUrl;
            const auto notice = [&]()
            {
                ImGuiToast updateNotification { ImGuiToastType::Error, updateNoticeTime };
                updateNotification.setTitle("OptiScaler Update available");
                updateNotification.setContent(
                    "Press %s for more info",
                    Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str());
                ImGui::InsertNotification(updateNotification);
                return true;
            };
            static auto res = notice();
        }
    }

    // One-shot startup warning notifications.
    if (!state.postDone)
    {
        if (state.postCodes & PostCode::SlPluginsAlreadyInMemory)
        {
            auto filename = Util::DllPath().filename().string();
            to_lower_in_place(filename);
            auto executable = Util::ExePath().filename().string();
            to_lower_in_place(executable);
            const bool keepEndfieldProxy = executable == "endfield.exe" && filename == "d3d12.dll";

            ImGuiToast notification { ImGuiToastType::Warning, 10000 };
            if(config->SkipStreamlineHooks.value_or_default()) {
                notification.setTitle("Native Streamline path retained");
                notification.setContent("Streamline was already loaded; OptiScaler Streamline hooks are disabled.\nThis notice does not indicate an NR failure.");
            } else if (keepEndfieldProxy) {
                notification.setTitle("Streamline loaded before D18");
                notification.setContent(
                    "Keep d3d12.dll: this is the verified D18 proxy for Endfield.\n"
                    "Late loading may affect Streamline hooks; check SR/FG runtime status in the D18 menu.");
            } else {
                notification.setTitle("Late Streamline hook detected");
                notification.setContent(
                    "Streamline was loaded before D18; some hooks may be affected.\n"
                    "If SR/FG has issues, use a proxy name verified for this game (current: %s).",
                    filename.c_str());
            }
            ImGui::InsertNotification(notification);
        }

        if (state.postCodes & PostCode::TryingFsr4Fp8OnUnsupported)
        {
            ImGuiToast notification { ImGuiToastType::Warning, 10000 };
            notification.setTitle("Silly goose detected");
            notification.setContent("FSR 4 FP8 only works on AMD");
            ImGui::InsertNotification(notification);
        }

        state.postDone = true;
    }

    // Initialize splash timing once per process.
    if (splashLimit < 1.0f)
    {
        splashStart = now + 100.0;
        splashLimit = splashStart + splashTime;

    }
}

void MenuCommon::BeginMenuFrameIfNeeded(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& now = ctx.now;
    auto& newFrame = ctx.newFrame;

    // New frame check
    if ((!config->DisableSplash.value_or_default() && now > splashStart && now < splashLimit) ||
        config->ShowFps.value_or_default() || _isVisible || ImGui::notifications.size() > 0 ||
        (config->DlssNrCompare.value_or_default() != 0 && config->DlssNrCompareTags.value_or_default()))
    {
        if (!_isUWP)
        {
            OptiInput::NewFrameWin32();
        }
        else
        {
            ImVec2 displaySize { state.screenWidth, state.screenHeight };
            ImGui_ImplUwp_NewFrame(displaySize);
        }

        OptiInput::FeedImGui(_isVisible);

        MenuHdrCheck(io);
        ImGui::NewFrame();

        newFrame = true;
    }
}

void MenuCommon::RenderSplashWindow(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& now = ctx.now;

    constexpr double fadeTime = 1000.0;

    // Splash screen
    if (!config->DisableSplash.value_or_default())
    {
        if (now > splashStart && now < splashLimit)
        {

            ImGui::SetNextWindowSize({ 0.0f, 0.0f });
            ImGui::SetNextWindowBgAlpha(config->FpsOverlayAlpha.value_or_default());
            ImGui::SetNextWindowPos(splashPosition, ImGuiCond_Always);

            float windowAlpha = 1.0f;
            if (auto diff = now - splashStart; diff < fadeTime)
                windowAlpha = static_cast<float>(diff / fadeTime);
            else if (auto diff = splashLimit - now; diff < fadeTime)
                windowAlpha = static_cast<float>(diff / fadeTime);

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, windowAlpha);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));

            if (!config->OverlaysUseTheme.value_or_default())
            {
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
            }

            if (ImGui::Begin("Splash", nullptr,
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav))
            {
                float splashScale = 1.0f;
                float baseScaleHeight = 720.0f;

                if (io.DisplaySize.y > baseScaleHeight)
                    splashScale = io.DisplaySize.y / baseScaleHeight;

                if (config->UseHQFont.value_or_default())
                    ImGui::PushFontSize(std::round(splashScale * fontSize));
                else
                    ImGui::SetWindowFontScale(splashScale);

                ImGui::Text("OptiScaler - %s for menu",
                            Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str());
                ImGui::TextColored(toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 0.7f)), "%s", splashMessage);

                splashSize = ImGui::GetWindowSize();

                if (config->UseHQFont.value_or_default())
                    ImGui::PopFontSize();

                ImGui::End();

                splashPosition.x = 0.0f; // io.DisplaySize.x - splashWinSize.x;
                splashPosition.y = io.DisplaySize.y - splashSize.y;
            }

            if (!config->OverlaysUseTheme.value_or_default())
                ImGui::PopStyleColor(4);
            else
                ImGui::PopStyleColor(2);

            ImGui::PopStyleVar(2);
        }
    }
}

void MenuCommon::RenderNotifications(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& io = ctx.io;

    // Notifications
    bool tonemapRequired = State::Instance().isHdrActive ||
                           (!Config::Instance()->OverlayMenu.value_or_default() &&
                            State::Instance().currentFeature != nullptr && State::Instance().currentFeature->IsHdr());

    float screenHeight = State::Instance().screenHeight;
    if (io.DisplaySize.y != 0)
        screenHeight = io.DisplaySize.y;

    // Map resolution height to scale, 0.5 for 480p, 2.0 for 1440p
    constexpr float slope = (2.0f - 0.5f) / (1440.f - 480.f);
    float notificationScale = 0.5f + slope * (screenHeight - 480.f);
    notificationScale = std::clamp(notificationScale, 0.5f, 2.0f);

    if (config->UseHQFont.value_or_default())
        ImGui::PushFontSize(std::round(notificationScale * fontSize));

    // No fallback font, SetWindowFontScale needs to be called after Begin()

    ImGui::RenderNotifications(ImGuiToastPos::TopCenter, notificationScale, tonemapRequired);

    if (config->UseHQFont.value_or_default())
        ImGui::PopFontSize();
}

void MenuCommon::UpdateFrameTimeAverages(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& frameTimesCalculated = ctx.frameTimesCalculated;
    auto& menuResScale = ctx.menuResScale;
    auto& fpsScale = ctx.fpsScale;
    auto& averageFrameTime = ctx.averageFrameTime;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    // FPS Overlay font
    fpsScale = config->FpsScale.value_or(menuResScale);

    // Update frame time & upscaler time averages
    averageFrameTime = 0.0f;
    averageUpscalerFT = 0.0f;

    if (config->ShowFps.value_or_default() || _isVisible)
    {
        float frameCnt = 0;
        frameTime = 0;
        for (size_t i = 299; i > 199; i--)
        {
            if (state.frameTimes[i] > 0.0)
            {
                frameTime += state.frameTimes[i];
                frameCnt++;
            }
        }

        frameTime /= frameCnt;
        frameRate = 1000.0 / frameTime;
        frameTimesCalculated = true;

        float lastFT = static_cast<float>(state.frameTimes.empty() ? 0.0f : state.frameTimes.back());
        float lastUT = static_cast<float>(state.upscaleTimes.empty() ? 0.0f : state.upscaleTimes.back());
        gFrameTimes.Push(lastFT);
        gUpscalerTimes.Push(lastUT);

        averageFrameTime = gFrameTimes.Average();
        averageUpscalerFT = gUpscalerTimes.Average();
    }
}

// Labels for the comparison views.
//
// Drawn straight onto the foreground draw list, not as ImGui windows -- the last attempt made them
// draggable windows and the clamping fought the split. Here each label is clipped to its own side of
// the comparison, so in the wipe the moving split reveals and hides it exactly as it does the
// pictures, and there is nothing to drag. Both wipe labels sit in the same top-left corner, each
// clipped to its side, so whichever picture currently owns that corner is the one whose label shows.
void MenuCommon::RenderNrCompareTags()
{
    auto* config = Config::Instance();

    const uint32_t mode = config->DlssNrCompare.value_or_default();

    if (mode == 0 || !config->DlssNrCompareTags.value_or_default())
        return;

    const ImVec2 screen = ImGui::GetIO().DisplaySize;

    if (screen.x < 1.0f || screen.y < 1.0f)
        return;

    const bool swap = config->DlssNrCompareSwap.value_or_default();
    const float split = mode == 1 ? 0.5f
                                  : std::clamp(config->DlssNrCompareSplit.value_or_default(), 0.0f, 1.0f);
    const float splitX = split * screen.x;

    const float scale = std::clamp(config->DlssNrTagScale.value_or_default(), 0.5f, 5.0f);

    // The left side is the untouched frame unless swapped -- matching the shader's
    // showOriginal = (uv.x < split) != swap.
    const char* leftText = swap ? "DLSS NR : ON" : "DLSS NR : OFF";
    const char* rightText = swap ? "DLSS NR : OFF" : "DLSS NR : ON";

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * scale;
    const float margin = 10.0f * scale;

    // Both labels flank the divider along the top: the left picture's label is right-aligned just
    // left of the split, the right picture's is left-aligned just right of it. Each is clipped to its
    // own side, so in the wipe the split reveals and hides them along with the images.
    auto drawTag = [&](const char* text, float x, ImVec2 clipMin, ImVec2 clipMax)
    {
        const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);

        // Never let a label run off the visible frame as it grows.
        x = std::min(std::max(x, 0.0f), screen.x - size.x);
        float y = std::min(margin, screen.y - size.y - margin);
        y = std::max(y, 0.0f);

        dl->PushClipRect(clipMin, clipMax, true);
        dl->AddText(font, fontSize, ImVec2(x + 2.0f, y + 2.0f), IM_COL32(0, 0, 0, 210), text);
        dl->AddText(font, fontSize, ImVec2(x, y), IM_COL32(255, 255, 255, 255), text);
        dl->PopClipRect();
    };

    const ImVec2 leftSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, leftText);

    // Left picture's label: right edge a margin in from the split. Right picture's: left edge a margin
    // out from the split.
    drawTag(leftText, splitX - margin - leftSize.x, ImVec2(0.0f, 0.0f), ImVec2(splitX, screen.y));
    drawTag(rightText, splitX + margin, ImVec2(splitX, 0.0f), ImVec2(screen.x, screen.y));
}

void MenuCommon::RenderPerformanceOverlay(RenderMenuContext& ctx)
{
    RenderNrCompareTags();


    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& currentFeature = ctx.currentFeature;
    auto& now = ctx.now;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& menuResScale = ctx.menuResScale;
    auto& fpsScale = ctx.fpsScale;
    auto& averageFrameTime = ctx.averageFrameTime;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    // If Fps overlay is visible
    if (config->ShowFps.value_or_default())
    {
        bool stylePushed = false;

        const static auto defaultStyle = ImGuiStyle();

        // Rescale the fps overlay every frame because it shares style with the main menu
        if (config->FpsScale.has_value() && config->FpsScale.value() != menuResScale)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, defaultStyle.WindowPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, defaultStyle.FramePadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, defaultStyle.CellPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, defaultStyle.SeparatorTextPadding * fpsScale);

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, defaultStyle.ItemSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, defaultStyle.ItemInnerSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, defaultStyle.IndentSpacing * fpsScale);

            stylePushed = true;
        }

        // Set overlay position
        ImGui::SetNextWindowPos(overlayPosition, ImGuiCond_Always);

        // Set overlay window properties
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));  // Transparent border
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0)); // Transparent frame background

        if (!config->OverlaysUseTheme.value_or_default())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        }

        ImGui::SetNextWindowBgAlpha(config->FpsOverlayAlpha.value_or_default()); // Transparent background

        if (!config->OverlaysUseTheme.value_or_default())
        {
            ImVec4 green(0.0f, 1.0f, 0.0f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotLines, toneMapColor(green));
        }

        if (ImGui::Begin("Performance Overlay", nullptr,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav))
        {
            std::string api;
            if (IdentifyGpu::getPrimaryGpu().usesDxvk && state.api == DX11)
            {
                if (state.swapchainInteropApi == SwapchainInteropApi::None)
                    api = "DXVK";
                else
                    api = "DXVK w/Dx12";
            }
            else if (IdentifyGpu::getPrimaryGpu().usesVkd3dProton && state.api == DX12)
            {
                api = "VKD3D";
            }
            else
            {
                switch (state.swapchainApi)
                {
                case Vulkan:
                    api = "VLK";
                    break;

                case DX11:
                    api = "D3D11";
                    break;

                case DX12:
                    if (state.swapchainInteropApi == SwapchainInteropApi::Dx11wDx12)
                        api = "D3D11 w/DX12";
                    else
                        api = "D3D12";

                    break;

                default:
                    switch (state.api)
                    {
                    case Vulkan:
                        api = "VLK";
                        break;

                    case DX11:
                        api = "D3D11";
                        break;

                    case DX12:
                        api = "D3D12";
                        break;

                    default:
                        api = "???";
                        break;
                    }

                    break;
                }
            }

            if (config->UseHQFont.value_or_default())
                ImGui::PushFontSize(std::round(fpsScale * fontSize));
            else
                ImGui::SetWindowFontScale(fpsScale);

            std::string firstLine = "";
            std::string secondLine = "";
            std::string thirdLine = "";

            auto fg = state.currentFG;
            auto fgText = (fg != nullptr && fg->IsActive() && !fg->IsPaused()) ? (" (" + std::string(fg->Name()) + ")")
                                                                               : std::string();

            const int fakeFramesCount = state.dlssgDetectedInterpolationCount;
            auto formatFg = [&](std::string_view name, int maxFakeFrames)
            {
                if (fakeFramesCount > maxFakeFrames)
                    return std::format(" ({} Doesn't support more than {}x)", name, maxFakeFrames);

                else if (fakeFramesCount == 0)
                    return std::format(" ({} off)", name);

                return std::format(" ({} x{})", name, fakeFramesCount + 1);
            };

            const FGNvngxReplacement activeNvngxFg = state.activeFgNvngx;
            if (activeNvngxFg == FGNvngxReplacement::Arturs)
            {
                fgText = formatFg("Enabler", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::Nukems)
            {
                fgText = formatFg("Nukems", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::FFX)
            {
                fgText = formatFg("FFX", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::Combo)
            {
                fgText = formatFg("Combo", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (state.activeFgOutput == FGOutput::DLSSG && fg)
            {
                fgText = formatFg("DLSSG", fg->GetMaxInterpolationCount());
            }

            // D18 measured summary; no configured-multiplier inversion.
            static void* monitorDevice = nullptr;
            static std::string monitorAdapter;
            void* activeDevice = state.currentD3D12Device ? (void*)state.currentD3D12Device : (void*)state.currentD3D11Device;
            if (activeDevice != monitorDevice || monitorAdapter.empty())
            {
                monitorDevice = activeDevice; monitorAdapter.clear();
                IDXGIAdapter* adapter = nullptr;
                if (state.currentD3D11Device)
                {
                    IDXGIDevice* dxgi = nullptr;
                    if (SUCCEEDED(state.currentD3D11Device->QueryInterface(IID_PPV_ARGS(&dxgi))))
                    { dxgi->GetAdapter(&adapter); dxgi->Release(); }
                }
                else if (state.currentD3D12Device)
                {
                    IDXGIFactory4* factory = nullptr;
                    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
                    { factory->EnumAdapterByLuid(state.currentD3D12Device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)); factory->Release(); }
                }
                if (adapter)
                {
                    DXGI_ADAPTER_DESC desc {};
                    if (SUCCEEDED(adapter->GetDesc(&desc)))
                    {
                        char label[256] {};
                        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, label, sizeof(label), nullptr, nullptr);
                        monitorAdapter = label;
                    }
                    adapter->Release();
                }
            }
            const auto measured = D18Monitor::read(monitorAdapter);
            const auto metric = [](double value, const char* format)
            { return value >= 0 && std::isfinite(value) ? StrFmt(format, value) : std::string("N/A"); };
            firstLine = StrFmt("%s FPS (%s FPS)   1%% Low %s FPS   GPU %s   %s",
                metric(measured.fps, "%.0f").c_str(), metric(measured.base, "%.0f").c_str(),
                metric(measured.low, "%.0f").c_str(), metric(measured.gpu, "%.0f%%").c_str(),
                metric(measured.watts, "%.0f W").c_str());

            // Prepare Line 2
            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Detailed)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                secondLine = StrFmt("Frame Time: %7.2f ms, Avg: %7.2f ms", state.frameTimes.back(), averageFrameTime);
            }

            // Prepare Line 3
            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Full)
            {
                thirdLine =
                    StrFmt("Upscaler Time: %7.2f ms, Avg: %7.2f ms", state.upscaleTimes.back(), averageUpscalerFT);
            }

            ImVec2 plotSize;
            if (config->FpsOverlayHorizontal.value_or_default())
            {
                plotSize = { fpsScale * 150, fpsScale * 16 };
            }
            else
            {
                // Find the widest text width
                auto firstSize = ImGui::CalcTextSize(firstLine.c_str());
                auto secondSize = ImGui::CalcTextSize(secondLine.c_str());
                auto thirdSize = ImGui::CalcTextSize(thirdLine.c_str());
                auto textWidth = 0.0f;

                if (firstSize.x > secondSize.x)
                    textWidth = firstSize.x > thirdSize.x ? firstSize.x : thirdSize.x;
                else
                    textWidth = secondSize.x > thirdSize.x ? secondSize.x : thirdSize.x;

                auto minWidth = fpsScale * 300.0f;
                auto plotWidth = textWidth < minWidth ? minWidth : textWidth;

                plotSize = { plotWidth, fpsScale * 30 };
            }

            // Draw the overlay
            ImGui::TextUnformatted(firstLine.c_str());

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Detailed)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(secondLine.c_str());
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_DetailedGraph)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of frame times
                ImGui::PlotLines(
                    "##FrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gFrameTimes, plotWidth, 0, nullptr, 0.0f, 66.6f, plotSize);
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Full)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(thirdLine.c_str());
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_FullGraph)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of upscaler times
                ImGui::PlotLines(
                    "##UpscalerFrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gUpscalerTimes, plotWidth, 0, nullptr, 0.0f, 20.0f, plotSize);
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_ReflexTimings)
            {
                constexpr auto delayBetweenPollsMs = 500;
                static auto previousPoll = 0.0;
                static bool gotData = false;

#ifdef LOW_LATENCY_INPUTS
                static TimingData timingData {};

                if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
                {
                    gotData = InputCommon::get_timing_data(timingData);
                    previousPoll = now;
                }

                if (gotData && timingData.timeRange.has_value())
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    constexpr float offsetForText = 155;

                    const auto& rangeInNs = timingData.timeRange.value().length;

                    UINT64 localFrameCount = 0;

                    if (fg != nullptr)
                        localFrameCount = fg->FrameCount();

                    ImGui::Text("FGId: %llu, RfxId: %llu", localFrameCount, state.reflexFrameId);
                    ImGui::Text("Low latency timings, whole frame: %.1fms", rangeInNs / 1000.0);

                    const auto maxWidth =
                        config->FpsOverlayHorizontal.value_or_default() ? ImGui::GetWindowWidth() : plotSize.x;

                    const auto drawTiming = [&](const auto& timingOpt, const char* desc, ImVec4 color)
                    {
                        if (!timingOpt.has_value())
                            return;

                        auto toneMappedColor = State::Instance().isHdrActive ? toneMapColor(color) : color;

                        const auto& timing = timingOpt.value();
                        float duration = static_cast<float>(timing.length * rangeInNs / 1000.0);

                        ImGui::TextColored(toneMappedColor, "%-12s %4.1fms", desc, duration);

                        auto leftLimit = ImGui::GetItemRectMin().x + offsetForText * fpsScale;

                        auto start = static_cast<float>(leftLimit + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                        timing.position);

                        auto end = static_cast<float>(start + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                  timing.length);

                        auto pos = ImVec2(start, ImGui::GetItemRectMin().y);
                        auto size = ImVec2(end, ImGui::GetItemRectMax().y);

                        drawList->AddRectFilled(pos, size, ImGui::ColorConvertFloat4ToU32(toneMappedColor));
                    };

                    drawTiming(timingData.simulation, "Simulation", ImVec4(0.768f, 0.169f, 0.169f, 1.0f));
                    drawTiming(timingData.renderSubmit, "RenderSubmit", ImVec4(0.235f, 0.705f, 0.294f, 1.0f));
                    drawTiming(timingData.present, "Present", ImVec4(1.0f, 0.88f, 0.098f, 1.0f));
                    drawTiming(timingData.driver, "Driver", ImVec4(0.263f, 0.388f, 0.847f, 1.0f));
                    drawTiming(timingData.osRenderQueue, "RenderQueue", ImVec4(0.76f, 0.51f, 0.188f, 1.0f));
                    drawTiming(timingData.gpuRender, "GpuRender", ImVec4(0.569f, 0.117f, 0.705f, 1.0f));
                }
#else
                if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
                {
                    gotData = ReflexHooks::updateTimingData();
                    previousPoll = now;
                }

                auto& timingData = ReflexHooks::timingData;

                if (gotData && timingData[TimingType::TimeRange].has_value())
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    constexpr float offsetForText = 155;

                    const auto& rangeInNs = timingData[TimingType::TimeRange].value().length;

                    UINT64 localFrameCount = 0;

                    if (fg != nullptr)
                        localFrameCount = fg->FrameCount();

                    ImGui::Text("FGId: %llu, RfxId: %llu", localFrameCount, state.reflexFrameId);
                    ImGui::Text("Reflex timings, whole frame: %.1fms", rangeInNs / 1000.0);

                    const auto maxWidth =
                        config->FpsOverlayHorizontal.value_or_default() ? ImGui::GetWindowWidth() : plotSize.x;

                    const auto drawTiming = [&](TimingType type, const char* desc, ImVec4 color)
                    {
                        if (!timingData[type].has_value())
                            return;

                        auto toneMappedColor = toneMapColor(color);

                        auto& timing = timingData[type].value();
                        float duration = static_cast<float>(timing.length * rangeInNs / 1000.0);
                        ImGui::TextColored(toneMappedColor, "%-12s %4.1fms", desc, duration);
                        auto leftLimit = ImGui::GetItemRectMin().x + offsetForText * fpsScale;
                        auto start = static_cast<float>(leftLimit + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                        timing.position);
                        auto end = static_cast<float>(start + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                  timing.length);
                        auto pos = ImVec2(start, ImGui::GetItemRectMin().y);
                        auto size = ImVec2(end, ImGui::GetItemRectMax().y);
                        drawList->AddRectFilled(pos, size, ImGui::ColorConvertFloat4ToU32(toneMappedColor));
                    };

                    drawTiming(TimingType::Simulation, "Simulation", ImVec4(0.768f, 0.169f, 0.169f, 1.0f));
                    drawTiming(TimingType::RenderSubmit, "RenderSubmit", ImVec4(0.235f, 0.705f, 0.294f, 1.0f));
                    drawTiming(TimingType::Present, "Present", ImVec4(1.0f, 0.88f, 0.098f, 1.0f));
                    drawTiming(TimingType::Driver, "Driver", ImVec4(0.263f, 0.388f, 0.847f, 1.0f));
                    drawTiming(TimingType::OsRenderQueue, "RenderQueue", ImVec4(0.76f, 0.51f, 0.188f, 1.0f));
                    drawTiming(TimingType::GpuRender, "GpuRender", ImVec4(0.569f, 0.117f, 0.705f, 1.0f));
                }
#endif
            }
        }

        // Restore the style
        if (!config->OverlaysUseTheme.value_or_default())
            ImGui::PopStyleColor(5);
        else
            ImGui::PopStyleColor(2);

        // Get size for postioning
        overlaySize = ImGui::GetWindowSize();

        if (config->UseHQFont.value_or_default())
            ImGui::PopFontSize();

        ImGui::End();

        if (stylePushed)
            ImGui::PopStyleVar(7);

        // Left / Right
        if (config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopLeft ||
            config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_BottomLeft)
        {
            overlayPosition.x = 0;
        }
        else
        {
            overlayPosition.x = io.DisplaySize.x - overlaySize.x;
        }

        // Top / Bottom
        if (config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopLeft ||
            config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopRight)
        {
            overlayPosition.y = 0;
        }
        else
        {
            // Prevent overlapping with splash message
            if (!config->DisableSplash.value_or_default() && now > splashStart && now < splashLimit)
                overlayPosition.y = io.DisplaySize.y - overlaySize.y - splashSize.y;
            else
                overlayPosition.y = io.DisplaySize.y - overlaySize.y;
        }
    }
}

void MenuCommon::RenderD18StatusDashboard(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto* feature = ctx.currentFeature;
    const bool nativeRoute=ctx.config->NgxOnlyMode.value_or_default() &&
        DlssNr::ReProfile::Known(state.gameExe.c_str());
    const auto nativeSr=DlssNr::NativeSr::Read();
    const bool nativeLive=nativeRoute && nativeSr.Live(GetTickCount64());

    // Presentation and SR input are separate observations, including cross-API bridges.
    const API presentationApi = state.swapchainApi;
    const bool srInputObserved = feature != nullptr || (nativeRoute && nativeSr.successfulFrames > 0);
    const API srInputApi = srInputObserved ? state.api : API::NotSelected;
    const bool featureReady = feature != nullptr && feature->IsInited() && feature->FrameCount() > 0;
    const bool featureLive = featureReady && !feature->IsFrozen();
    const bool isDlssSr = featureLive && feature->GetUpscalerType() == Upscaler::DLSS;
    const bool isDlssD = featureLive && feature->GetUpscalerType() == Upscaler::DLSSD;

    const bool srEnabled = ctx.config->DLSSEnabled.value_or_default();
    D18Health srHealth = srEnabled ? D18Health::Waiting : D18Health::Off;
    std::string srDetail = srEnabled ? "Waiting for SR input; enter gameplay. Missing observations do not prove API incompatibility." : "DLSS backend disabled";
    if (nativeRoute)
    {
        srHealth=nativeLive?D18Health::Active:D18Health::Waiting;
        srDetail=nativeLive?D18Ui::Format("Native DLSS %s passthrough | successful frame %llu",nativeSr.rayReconstruction?"RR":"SR",nativeSr.successfulFrames):
            "Native DLSS: no recent successful evaluation";
    }
    else if (isDlssSr)
    {
        srHealth = D18Health::Active;
        srDetail = D18Ui::Format("%s %u.%u.%u | frame %ld", feature->ShortName().c_str(), feature->Version().major,
                          feature->Version().minor, feature->Version().patch, feature->FrameCount());
    }
    else if (isDlssD)
    {
        srDetail = D18Ui::Format("Ray Reconstruction is active (%s %u.%u.%u)", feature->ShortName().c_str(),
                          feature->Version().major, feature->Version().minor, feature->Version().patch);
    }
    else if (featureLive)
    {
        srHealth = D18Health::Error;
        srDetail = D18Ui::Format("Non-DLSS backend active: %s", feature->ShortName().c_str());
    }
    else if (featureReady)
    {
        srDetail = "Feature exists but is idle/frozen";
    }

    const bool dlssFgPath = state.activeFgInput == FGInput::DLSSG || state.activeFgInput == FGInput::NvngxFG ||
                            state.activeFgOutput == FGOutput::DLSSG || state.dlssgDetectedInterpolationCount > 0;
    const bool dlssgFresh = state.dlssgLastFrame > 0 &&
                            (state.fgLastFrame < state.dlssgLastFrame ||
                             state.fgLastFrame - state.dlssgLastFrame < 3);
    const bool fgObjectActive = state.currentFG != nullptr && state.currentFG->IsActive() &&
                                !state.currentFG->IsPaused();
    const bool fgLive = dlssFgPath && (state.dlssgDetectedInterpolationCount > 0 ||
                                      (fgObjectActive && (dlssgFresh || state.activeFgOutput == FGOutput::DLSSG)));

    D18Health fgHealth = D18Health::Unobserved;
    std::string fgDetail = "No FG input observed; a supported game integration is required.";
    if (fgLive)
    {
        fgHealth = D18Health::Active;
        if (state.activeFgInput == FGInput::Upscaler && state.activeFgOutput == FGOutput::DLSSG)
            fgDetail = D18Ui::Format("FG active | requested %ux; actual multiplier unobserved",
                                    ctx.config->FGDLSSGInterpolationCount.value_or_default() + 1);
        else if (state.dlssgDetectedInterpolationCount > 0)
            fgDetail = D18Ui::Format("DLSSG x%d | FG frame %llu", state.dlssgDetectedInterpolationCount + 1, state.fgLastFrame);
        else
            fgDetail = D18Ui::Tr("FG active; actual multiplier unobserved");
    }
    else if (dlssFgPath)
    {
        fgHealth = D18Health::Waiting;
        fgDetail = "DLSSG path detected; no generated frame is active";
    }

    if (state.swapchainApi == API::Vulkan && !dlssFgPath)
    {
        fgHealth = D18Health::Unobserved;
        fgDetail = "Vulkan FG injection is not supported by this route; game-native FG is detected separately.";
    }

    const auto nativeFg=DlssNr::NativeFg::Read();
    if(presentationApi==API::Vulkan && nativeFg.tick && state.activeFgInput!=FGInput::Upscaler) {
        fgHealth=!nativeFg.Fresh()?D18Health::Unobserved:!nativeFg.ok?D18Health::Error:
                 nativeFg.presented>1?D18Health::Active:D18Health::Off;
        fgDetail=!nativeFg.Fresh()?"No recent native FG status":!nativeFg.ok?"Native FG runtime reported an error":
                 nativeFg.presented>1?D18Ui::Format("Game-native DLSSG x%u",nativeFg.presented):"Native FG: no generated frames reported";
        if (nativeFg.Fresh() && nativeFg.ok && nativeFg.menuPaused && MenuOverlayBase::IsVisible()) {
            fgHealth=D18Health::Paused;
            fgDetail="Paused while this menu is open (OptiScaler Vulkan policy); close menu to resume";
        }
    }
    if(nativeRoute && ctx.config->SkipStreamlineHooks.value_or_default()) {
        fgHealth=D18Health::Unobserved;
        fgDetail="Game-controlled FG | live on/off telemetry unavailable";
    }
    const auto nrSnapshot = DlssNr::ReadUiSnapshot();
    const bool nrVulkan = state.api == API::Vulkan;
    const bool nrRunning = nrVulkan ? DlssNr::IsRunningVk() : nrSnapshot.running;
    const auto& nrRuntime = nrSnapshot.runtime;
    const char* nrFailure = nrVulkan ? DlssNr::FailureReasonVk() : nrSnapshot.failure.data();

    const bool nrEnabled = ctx.config->DlssNrEnabled.value_or_default();
    D18Health nrHealth = D18Health::Off;
    std::string nrDetail = "Disabled";
    if (!nrEnabled)
    {
        nrHealth = D18Health::Off;
    }
    else if (nrVulkan && DlssNr::NativeControl::conversion)
    {
        nrHealth = nrFailure[0] ? D18Health::Error : D18Health::Waiting;
        nrDetail = nrFailure[0] ? nrFailure : "Conversion only; NR model bypassed";
    }
    else if (!nrVulkan && nrRuntime.rebuildRequiresRestart)
    {
        nrHealth = D18Health::Waiting;
        nrDetail = "Feature 18 active; requested rebuild will apply after restart";
    }
    else if (nrRunning && (nrVulkan ? DlssNr::FramesVk() > 0 : nrRuntime.successfulFrames > 0))
    {
        nrHealth = D18Health::Active;
        const auto ms = nrVulkan ? DlssNr::LastGpuTimeVk() : nrSnapshot.gpuTime;
        nrDetail = ms.has_value() ? D18Ui::Format("Feature 18 | %.2f ms", ms.value()) : D18Ui::Format("Feature 18 | %s", nrVulkan ? DlssNr::GpuTimingStatusVk() : "timing pending");
    }
    else if (nrFailure[0] != 0)
    {
        nrHealth = D18Health::Error;
        nrDetail = nrFailure;
    }
    else
    {
        nrHealth = D18Health::Waiting;
        nrDetail = "Enabled; waiting for a valid DLSS frame";
    }

    const bool nrDx11=state.api==API::DX11;
    if(nrDx11){
        const auto s=DlssNr::NativeControl::Read();
        const bool fresh=s.tick && GetTickCount64()-s.tick<1500;
        nrHealth=!nrEnabled?D18Health::Off:s.failed?D18Health::Error:fresh&&s.result==1&&s.mode==2?D18Health::Active:D18Health::Waiting;
        nrDetail=!nrEnabled?"NR switch is disabled":s.result<0?DlssNr::NativeControl::Reason(s.result):!fresh?"Waiting for DX11 SR output and NR guides; API version alone does not provide these inputs.":s.mode==1?"Conversion only; model bypassed":D18Ui::Format("DX11 | %u frames since NR reset",s.frames);
    }
    D18Ui::SeparatorText("D18 Runtime Status");
    D18Ui::Text("Presentation API: %s", D18Ui::Tr(D18ApiName(presentationApi)));
    D18Ui::Text("SR input API: %s", srInputObserved ? D18Ui::Tr(D18ApiName(srInputApi)) : D18Ui::Tr("Not observed"));
    if (!srInputObserved)
        D18Ui::TextWrapped("SR input has not been observed or integrated. Enabling SR or Apply SR does not add game input integration.");
    D18Ui::TextWrapped("Game device contract is preserved. SR / FG / NR are evaluated independently.");
    if (ImGui::BeginTable("##d18_status", 3, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();
        RenderD18Indicator("sr", "DLSS SR", srHealth, srDetail, ctx.menuResScale);
        ImGui::TableNextColumn();
        RenderD18Indicator("fg", "DLSS FG", fgHealth, fgDetail, ctx.menuResScale);
        ImGui::TableNextColumn();
        RenderD18Indicator("nr", "DLSS NR", nrHealth, nrDetail, ctx.menuResScale);
        ImGui::EndTable();
    }

    // Do not show a fictional DX12 pipeline when no SR input exists.
    if (!srInputObserved)
        return;
    if(nrDx11 || nrVulkan){
        D18Ui::TextDisabled("Native %s NR: see backend status and controls below. DX12 pipeline counters do not apply.",nrDx11?"DX11":"Vulkan");
        return;
    }
    D18Ui::SeparatorText("SR -> NR Frame Path");
    if(nativeRoute && nrEnabled && !nrRunning) {
        const auto blocked=DlssNr::Submission::LastBlock();
        if(!blocked.empty()) D18Ui::TextWrapped("NR paused: %s",blocked.c_str());
    }

    const unsigned long long featureFrame = nativeRoute ? nativeSr.successfulFrames : featureReady
                                                ? static_cast<unsigned long long>(std::max(0L, feature->FrameCount()))
                                                : 0;
    const unsigned long long nowTick = GetTickCount64();
    const bool pathFresh = nrRuntime.lastUpdateTickMs > 0 && nowTick >= nrRuntime.lastUpdateTickMs &&
                           nowTick - nrRuntime.lastUpdateTickMs <= 1500;
    const auto reached = [&](DlssNr::PipelineStage stage)
    {
        return pathFresh && static_cast<unsigned int>(nrRuntime.lastStage) >= static_cast<unsigned int>(stage);
    };
    // A new frame returns lastStage to SrHandoff. Health instead reflects recent
    // successful recording; no progress for 1.5 seconds returns to Waiting.
    const auto recentlyRecorded = [&](unsigned long long tick)
    {
        return tick != 0 && nowTick >= tick && nowTick - tick <= 1500;
    };

    const D18Health gameNode = nativeRoute ? (nativeLive?D18Health::Active:D18Health::Waiting) : featureLive ? D18Health::Active
                                           : (featureReady ? D18Health::Waiting : D18Health::Unobserved);
    D18Health srNode = srHealth;
    D18Health inputsNode = nrEnabled ? D18Health::Waiting : D18Health::Off;
    D18Health modelNode = nrEnabled ? D18Health::Waiting : D18Health::Off;
    D18Health composeNode = nrEnabled ? D18Health::Waiting : D18Health::Off;

    if (nrEnabled && !nrVulkan)
    {
        srNode = reached(DlssNr::PipelineStage::SrHandoff) ? D18Health::Active : srHealth;

        if (reached(DlssNr::PipelineStage::InputsReady))
            inputsNode = D18Health::Active;
        else if (pathFresh && nrRuntime.lastStage == DlssNr::PipelineStage::SrHandoff &&
                 (!nrRuntime.lastHadOutput || !nrRuntime.lastHadDepth || !nrRuntime.lastHadMotion))
            inputsNode = D18Health::Error;

        if (nrFailure[0] != 0)
            modelNode = D18Health::Error;
        else if (recentlyRecorded(nrRuntime.lastSuccessTickMs))
            modelNode = D18Health::Active;

        if (nrFailure[0] != 0)
            composeNode = D18Health::Error;
        else if (recentlyRecorded(nrRuntime.lastComposeTickMs))
            composeNode = D18Health::Active;
    }

    if (ImGui::BeginTable("##d18_pipeline", 5,
                          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableNextColumn();
        RenderD18PipelineNode("game", "SR input", gameNode, featureFrame, ctx.menuResScale);
        ImGui::TableNextColumn();
        RenderD18PipelineNode("sr_output", "SR output", srNode, nrRuntime.srHandoffFrames, ctx.menuResScale);
        ImGui::TableNextColumn();
        RenderD18PipelineNode("nr_inputs", "NR inputs", inputsNode, nrRuntime.inputReadyFrames, ctx.menuResScale);
        ImGui::TableNextColumn();
        RenderD18PipelineNode("feature18", "Feature 18", modelNode, nrRuntime.successfulFrames, ctx.menuResScale);
        ImGui::TableNextColumn();
        RenderD18PipelineNode("nr_frame", "NR frame", composeNode, nrRuntime.composedFrames, ctx.menuResScale);
        ImGui::EndTable();
    }

    if (nrVulkan)
    {
        D18Ui::TextDisabled("Detailed per-stage tracing is currently available on the D3D12 path.");
    }
    else
    {
        D18Ui::TextDisabled("SR handoff %llu  |  inputs %llu  |  dispatch %llu  |  model %llu  |  composed %llu",
                            nrRuntime.srHandoffFrames, nrRuntime.inputReadyFrames, nrRuntime.attemptedFrames,
                            nrRuntime.successfulFrames, nrRuntime.composedFrames);

        if (pathFresh && nrRuntime.lastStage == DlssNr::PipelineStage::SrHandoff &&
            (!nrRuntime.lastHadOutput || !nrRuntime.lastHadDepth || !nrRuntime.lastHadMotion))
        {
            std::string missing;
            if (!nrRuntime.lastHadOutput)
                missing += "output ";
            if (!nrRuntime.lastHadDepth)
                missing += "depth ";
            if (!nrRuntime.lastHadMotion)
                missing += "motion ";
            D18Ui::TextColored(D18HealthColor(D18Health::Error), "Blocked after SR: missing %s", missing.c_str());
        }
    }

    RenderD18Diagnostics(ctx);
}

void MenuCommon::RenderD18Diagnostics(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto* feature = ctx.currentFeature;
    const auto nr = DlssNr::GetRuntimeStatus();

    if (!D18Ui::TreeNodeEx("Live diagnostics", ImGuiTreeNodeFlags_SpanAvailWidth))
        return;

    if (ImGui::BeginTable("##d18_diag", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                                               ImGuiTableFlags_BordersInnerH))
    {
        auto row = [](const char* name, const std::string& value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            D18Ui::TextDisabled("%s", name);
            ImGui::TableSetColumnIndex(1);
            D18Ui::TextUnformatted(value.c_str());
        };

        row("API / input", D18Ui::Format("%s / %s", D18ApiName(state.api),
                                   ApiUpscalerInputName(state.currentInputApiName).c_str()));
        const auto input = OptiInput::GetDebugState();
        row("UI input", D18Ui::Format("%s | mouse %ld,%ld | L:%u | wheel %s", input.PollingOnly ? "poll" : "messages",
            input.MouseClientPos.x, input.MouseClientPos.y, unsigned(input.MouseLeftDown),
            input.PollingOnly ? (input.WheelObserverReady ? (input.WheelUsesRaw ? "raw" : "queue") : "unobserved") : "messages/raw"));

        if (feature != nullptr && feature->IsInited())
        {
            row("SR resolution", D18Ui::Format("%ux%u -> %ux%u | display %ux%u", feature->RenderWidth(),
                                         feature->RenderHeight(), feature->TargetWidth(), feature->TargetHeight(),
                                         feature->DisplayWidth(), feature->DisplayHeight()));
            row("SR guides", D18Ui::Format("Depth %s | MV %s | HDR %s | exposure %s", feature->DepthInverted() ? "inverted" : "normal",
                                    feature->LowResMV() ? "low-res" : "display-res", feature->IsHdr() ? "yes" : "no",
                                    feature->HasExposure() ? "yes" : "no"));
        }
        else
        {
            row("SR resolution", "No live feature contract");
        }

        if (state.api == API::Vulkan)
        {
            const auto vkTime = DlssNr::LastGpuTimeVk();
            row("NR Vulkan", D18Ui::Format("%s | exposure %s", DlssNr::IsRunningVk() ? "running" : "idle",
                                     DlssNr::ExposureOfferedVk() ? "offered" : "absent"));
            row("NR frames / timing", vkTime.has_value()
                                            ? D18Ui::Format("%llu composed | %.2f ms", DlssNr::FramesVk(), vkTime.value())
                                            : D18Ui::Format("%llu composed | %s", DlssNr::FramesVk(), DlssNr::GpuTimingStatusVk()));
        }
        else if (nr.outputWidth > 0)
        {
            row("NR output / network", D18Ui::Format("%ux%u / %ux%u (%.3fx)", nr.outputWidth, nr.outputHeight,
                                               nr.networkWidth, nr.networkHeight, nr.internalRatio));
            row("NR guides", D18Ui::Format("%ux%u | MV scale %.1f x %.1f | depth %s", nr.guideWidth, nr.guideHeight,
                                    nr.mvScaleX, nr.mvScaleY, nr.depthInverted ? "inverted" : "normal"));
            row("NR frames / filter", D18Ui::Format("%llu composed / %llu model ok / %llu attempted | Mitchell %s",
                                              nr.composedFrames, nr.successfulFrames, nr.attemptedFrames,
                                              nr.customColorFilter ? "on" : "off"));
            if (nr.rebuildRequiresRestart)
                row("NR retirement", D18Ui::Format("RESTART REQUIRED | %s", DlssNr::RebuildFallbackReason()));
            else
                row("NR retirement", D18Ui::Format("GPU fence %s | %u pending / %llu completed",
                                             nr.fenceRetirementActive ? "active" : "ready on first rebuild",
                                             nr.retiredBatches, nr.completedRetirements));
        }
        else
        {
            row("NR contract", "Waiting for Feature 18");
        }

        const std::string fgDetected = state.dlssgDetectedInterpolationCount > 0
                                           ? D18Ui::Format("%dx", state.dlssgDetectedInterpolationCount + 1)
                                           : "off";
        row("FG route", D18Ui::Format("%s -> %s | detected %s | Reflex %s", D18FgInputName(state.activeFgInput),
                                D18FgOutputName(state.activeFgOutput), fgDetected.c_str(),
                                ReflexHooks::isReflexHooked() ? "hooked" : "not hooked"));
        row("Frame timing", D18Ui::Format("%.2f ms | %.1f fps", ctx.frameTime, ctx.frameRate));

        ImGui::EndTable();
    }

    ImGui::TreePop();
}

void MenuCommon::RenderMainMenuHeaderMessages(RenderMenuContext& ctx)
{
    D18Ui::SetLanguage(ctx.config->D18Language.value_or_default());
    int language = ctx.config->D18Language.value_or_default() == 1 ? 1 : 0;
    const char* languages[] = { "English", "\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87" };
    ImGui::SetNextItemWidth(170.0f * ctx.menuResScale);
    ImGui::BeginDisabled(!D18Ui::chineseFontAvailable);
    if (D18Ui::Combo(D18Ui::chineseFontAvailable ? "Language / \xe8\xaf\xad\xe8\xa8\x80" : "Language", &language, languages, 2)) {
        ctx.config->D18Language = static_cast<uint32_t>(language);
        D18Ui::SetLanguage(static_cast<unsigned>(language));
    }
    ImGui::EndDisabled();
    if (!D18Ui::chineseFontAvailable) ImGui::TextDisabled("Chinese font unavailable; using English.");
    else ShowHelpMarker("Use Save Settings to keep the language for this game.");
    RenderD18StatusDashboard(ctx);
#if 0
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& versionStatus = ctx.versionStatus;
    auto& currentVersionText = ctx.currentVersionText;
    auto& primaryGpu = *ctx.primaryGpu;

    if (!_showMipmapCalcWindow && !_showHudlessWindow && !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
        ImGui::SetWindowFocus();

    if (config->MenuScale.has_value())
    {
        _selectedScale = ((int) (menuResScale * 10.0f)) - 4;
    }
    else
    {
        _selectedScale = 0;
    }

    if (versionStatus.completed)
    {
        if (versionStatus.updateAvailable && !versionStatus.latestTag.empty())
        {
            ImGui::Spacing();
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), "Update available: %s (current %s)",
                               versionStatus.latestTag.c_str(), currentVersionText.c_str());

            if (!versionStatus.latestUrl.empty())
            {
                ImGui::SameLine();
                ImGui::TextLinkOpenURL("Open release page", versionStatus.latestUrl.c_str());
            }

            ImGui::Spacing();
        }
        else if (!versionStatus.error.empty())
        {
            LOG_ERROR("Version check failed: {0}", versionStatus.error);
            versionStatus.error.clear();
        }
        // Disabled error message
        // else if (!versionStatus.error.empty())
        //{
        //    ImGui::Spacing();
        //    ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.4f, 0.f, 1.f)), "%s", versionStatus.error.c_str());
        //    ImGui::Spacing();
        //}
    }

    // No active upscaler message
    if (currentFeature == nullptr || !currentFeature->IsInited())
    {
        ImGui::Spacing();

        if (config->UseHQFont.value_or_default())
            ImGui::PushFontSize(std::round(fontSize * menuResScale * 2.5f));
        else
            ImGui::SetWindowFontScale(menuResScale * 2.5f);

        if (state.nvngxExists || state.nvngxReplacement.has_value() ||
            (state.libxessExists || XeSSProxy::Module() != nullptr))
        {
            ImGui::Spacing();

            std::vector<std::string> upscalers;

            if (state.fsrHooks)
                upscalers.push_back("FSR");

            if (state.nvngxExists || state.nvngxReplacement.has_value() || primaryGpu.dlssCapable)
                upscalers.push_back("DLSS");

            if (state.libxessExists || XeSSProxy::Module() != nullptr)
                upscalers.push_back("XeSS");

            auto joined = upscalers | std::views::join_with(std::string { " or " });

            std::string joinedUpscalers(joined.begin(), joined.end());

            ImGui::Text("Please select %s as upscaler from game\noptions and load a save game "
                        "to enable Opti settings.\nUpscalers don't always work in menus.",
                        joinedUpscalers.c_str());

            if (config->UseHQFont.value_or_default())
                ImGui::PopFontSize();
            else
                ImGui::SetWindowFontScale(menuResScale);

            ImGui::Spacing();

            if (primaryGpu.dlssCapable)
            {
                ImGui::Text("nvngx_dlss : %s", state.NVNGX_DLSS_Path.has_value() ? "Exists" : "Doesn't Exist");
                ImGui::SameLine(0.0f, 16.0f);
                ImGui::Text("nvngx_dlssd : %s", state.NVNGX_DLSSD_Path.has_value() ? "Exists" : "Doesn't Exist");
            }
            else
            {
                ImGui::Text("nvngx.dll: %s", state.nvngxExists ? "Exists" : "Doesn't Exist");
                ImGui::SameLine(0.0f, 16.0f);
                ImGui::Text("nvngx replacement: %s", state.nvngxReplacement.has_value() ? "Exists" : "Doesn't Exist");
            }

            ImGui::Text("libxess: %s",
                        (state.libxessExists || XeSSProxy::Module() != nullptr) ? "Exists" : "Doesn't Exist");

            ImGui::Text("FSR Hooks: %s", state.fsrHooks ? "Exist" : "Don't Exist");
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Text("FSR 3.1: %s", FfxApiProxy::Dx12Module() != nullptr ? "Exists" : "Doesn't Exist");
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Text("FSR 3.1 SR: %s", FfxApiProxy::Dx12Module_SR() != nullptr ? "Exists" : "Doesn't Exist");
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::Text("FSR 3.1 FG: %s", FfxApiProxy::Dx12Module_FG() != nullptr ? "Exists" : "Doesn't Exist");

            ImGui::Spacing();
        }
        else
        {
            ImGui::Spacing();
            ImGui::Text("Can't find nvngx.dll and libxess.dll and FSR inputs\nUpscaling support will NOT work.");
            ImGui::Spacing();

            if (config->UseHQFont.value_or_default())
                ImGui::PopFont();
            else
                ImGui::SetWindowFontScale(menuResScale);
        }
    }
    else if (currentFeature->IsFrozen())
    {
        ImGui::Spacing();

        if (config->UseHQFont.value_or_default())
            ImGui::PushFontSize(std::round(fontSize * menuResScale * 3.0f));
        else
            ImGui::SetWindowFontScale(menuResScale * 3.0f);

        ImGui::Text("%s is active, but not currently used by the game\nPlease enter the game",
                    currentFeature->Name().c_str());

        if (config->UseHQFont.value_or_default())
            ImGui::PopFont();
        else
            ImGui::SetWindowFontScale(menuResScale);
    }
#endif
}


void MenuCommon::RenderFrameGenerationSelection(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    /// FG INPUTS

    static std::vector<MenuOption<FGInput>> inputOptions;
    inputOptions.clear();

    // clang-format off

    inputOptions = {
        { FGInput::NoFG, "None" },
        { FGInput::Upscaler, "OptiFG (Upscaler)",
            "Upscaler must be enabled\n\nCan be used with any FG Output, but might be imperfect with some\nTo prevent UI glitching, HUDfix required" },
        { FGInput::DLSSG, "DLSSG via Streamline",
            "Can be used with any FG Output\n\nRequires enabling DLSS-FG in game settings\nSupports HUDless out of the box\n\nLimited to games that use Streamline" },
        { FGInput::NvngxFG, "DLSSG via Nvngx",
            "Limited to variants of FSR FG\n\nRequires enabling DLSS-FG in game settings\nSupports HUDless out of the box\nUses Streamline swapchain for pacing" },
        { FGInput::FSRFG, "FSR 3.1 FG",
            "Can be used with any FG Output\n\nRequires enabling FSR-FG in game settings\nSupports HUDless out of the box" },
        { FGInput::FSRFG30, "FSR 3.0 FG",
            "Can be used with any FG Output\n\nRequires enabling FSR-FG in game settings\nSupports HUDless out of the box" },
        { FGInput::XeFG, "XeFG" }
    };

    // clang-format on

    auto constexpr nvngxInputIndex = (uint32_t) FGInput::NvngxFG;

    // XeFG input requirements
    auto constexpr xefgInputIndex = (uint32_t) FGInput::XeFG;
    inputOptions[xefgInputIndex].set_disabled(true, "Support not implemented, they meant FG Output");

    // OptiFG requirements
    auto constexpr optiFgIndex = (uint32_t) FGInput::Upscaler;
    inputOptions[optiFgIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");

    if (!inputOptions[optiFgIndex].disabled && state.activeFgOutput == FGOutput::FSRFG && !FfxApiProxy::IsFGReady() &&
        !ffxInitTried)
    {
        ffxInitTried = true;
        FfxApiProxy::InitFfxDx12();
        inputOptions[optiFgIndex].set_disabled(!FfxApiProxy::IsFGReady(), "amd_fidelityfx_dx12.dll is missing");
    }
    else if (!inputOptions[optiFgIndex].disabled && state.activeFgOutput == FGOutput::XeFG && !xefgInitTried &&
             XeFGProxy::Module() == nullptr)
    {
        xefgInitTried = true;
        XeFGProxy::InitXeFG();
        inputOptions[optiFgIndex].set_disabled(XeFGProxy::Module() == nullptr, "libxess_fg.dll is missing");
    }

    // DLSSG inputs requirements
    auto constexpr dlssgInputIndex = (uint32_t) FGInput::DLSSG;
    // inputOptions[dlssgInputIndex].set_disabled(state.streamlineVersion.major == 0, "Game doesn't use streamline");
    inputOptions[dlssgInputIndex].set_disabled(state.swapchainApi == API::DX11, "Unsupported API");

    // FSRFG inputs requirements
    auto constexpr fsrfgInputIndex = (uint32_t) FGInput::FSRFG;
    inputOptions[fsrfgInputIndex].set_disabled(state.swapchainApi != API::DX12, "Unsupported API");

    // FSRFG30 inputs requirements
    auto constexpr fsrfg30InputIndex = (uint32_t) FGInput::FSRFG30;
    inputOptions[fsrfg30InputIndex].set_disabled(state.swapchainApi != API::DX12, "Unsupported API");

    if (!config->FGInput.has_value())
        config->FGInput = config->FGInput.value_or_default(); // need to have a value before combo

    /// FG OUTPUTS

    static std::vector<MenuOption<FGOutput>> outputOptions;
    outputOptions.clear();

    // clang-format off

    outputOptions = {
        { FGOutput::NoFG, "None" },
        { FGOutput::FSRFG, "FSR FG", "FSR3/4-FG, RDNA4 autoupgrades to FSR4-FG\n\nFSR4-FG sometimes better/worse than XeFG" },
        { FGOutput::DLSSG, "DLSSG", "DLSSG output\ncan be used in conjuction with Nukem's for example" },
        { FGOutput::XeFG, "XeFG", "XeFG - heaviest, but best universal FG\n\nXeFG 3 overall deals best with HUD\n\nEnable UI Composition if HUD ghosting" },
    };

    // clang-format on

    // DLSSG output requirements
    auto constexpr dlssgOutputIndex = (uint32_t) FGOutput::DLSSG;
    const bool supportsDlssg = primaryGpu.nvidiaArchInfo.architecture_id >= NV_GPU_ARCHITECTURE_AD100;
    const bool hasDlssgReplacement =
        state.nukemsFgFileAvailable || state.artursFgFileAvailable || FfxApiProxy::IsFGReady(false);

    if (!supportsDlssg && hasDlssgReplacement)
    {
        outputOptions[dlssgOutputIndex].tooltip =
            "No real DLSSG, unsupported hardware\nOnly Nvngx FG replacements available";
    }

    outputOptions[dlssgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");
    outputOptions[dlssgOutputIndex].set_disabled(!supportsDlssg && !hasDlssgReplacement,
                                                 "Unsupported hardware and no replacements");

    // For that one case of DX11 DLSSG
    const auto streamlineVersion = state.streamlineVersion;
    const bool nukemsUnsupportedApi =
        state.swapchainApi == API::DX11 &&
        (streamlineVersion == feature_version { 0, 0, 0 } || streamlineVersion > feature_version { 2, 0, 1 });
    inputOptions[nvngxInputIndex].set_disabled(nukemsUnsupportedApi, "Unsupported API");

    // FSR FG output requirements
    auto constexpr fsrfgOutputIndex = (uint32_t) FGOutput::FSRFG;
    outputOptions[fsrfgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");

    // XeFG output requirements
    auto constexpr xefgOutputIndex = (uint32_t) FGOutput::XeFG;
    outputOptions[xefgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");
    // Unsupported FG input selected
    const auto currentInputIndex = (uint32_t) state.activeFgInput;
    if (config->FGInput != FGInput::NoFG && inputOptions.size() > currentInputIndex &&
        inputOptions[currentInputIndex].disabled && state.activeFgInput == config->FGInput)
    {
        LOG_WARN("Resetting FGInput to NoFG: {}", inputOptions[currentInputIndex].label);
        config->FGInput = FGInput::NoFG;

        // Changing active can be dangerous but we are talking about an unsupported mode
        // which shouldn't even actually have taken affect
        state.activeFgInput = FGInput::NoFG;
    }

    // Unsupported FG output selected
    const auto currentOutputIndex = (uint32_t) state.activeFgOutput;
    if (config->FGOutput != FGOutput::NoFG && outputOptions.size() > currentOutputIndex &&
        outputOptions[currentOutputIndex].disabled && state.activeFgOutput == config->FGOutput)
    {
        LOG_WARN("Resetting FGOutput to NoFG: {}", outputOptions[currentOutputIndex].label);
        config->FGOutput = FGOutput::NoFG;
        state.activeFgOutput = FGOutput::NoFG;
    }

    if (!config->FGOutput.has_value())
        config->FGOutput = config->FGOutput.value_or_default(); // need to have a value before combo

    /// FG NVNGX REPLACEMENT

    static std::vector<MenuOption<FGNvngxReplacement>> nvngxOptions;
    nvngxOptions.clear();

    // clang-format off

    nvngxOptions = {
        { FGNvngxReplacement::None, "None (Real DLSSG)", "Real DLSSG, For RTX 40xx and above"},
        { FGNvngxReplacement::Nukems, "Nukem's", "FSR 3 FG" },
        { FGNvngxReplacement::Arturs, "Enabler", "FSR 3 MFG" },
        { FGNvngxReplacement::FFX, "FSR 3/4 FG", "FSR 3/4 FG using the FFX" },
        { FGNvngxReplacement::Combo, "FFX + Enabler", "FFX for the middle fake frame, Enabler for the rest\n\n"
                                                      "2x - FFX\n3x - Enabler\n4x - FFX + Enabler\n5x - Enabler\n6x - FFX + Enabler" },
    };

    // clang-format on

    bool replaceFgOutputWithNvngx = false;
    bool showNvngxFgDowndown = false;

    if (config->FGInput == FGInput::NvngxFG)
    {
        config->FGOutput = FGOutput::NoFG;
        replaceFgOutputWithNvngx = true;
    }
    else if (config->FGOutput == FGOutput::DLSSG)
    {
        showNvngxFgDowndown = true;
    }

    auto constexpr fgNvngxNoneIndex = (uint32_t) FGNvngxReplacement::None;
    nvngxOptions[fgNvngxNoneIndex].set_disabled(!supportsDlssg, "Unsupported hardware");

    if (replaceFgOutputWithNvngx)
    {
        nvngxOptions[fgNvngxNoneIndex].label = "None";
        nvngxOptions[fgNvngxNoneIndex].set_hidden(true);
    }

    auto constexpr fgNvngxNukemsIndex = (uint32_t) FGNvngxReplacement::Nukems;
    nvngxOptions[fgNvngxNukemsIndex].set_disabled(!state.nukemsFgFileAvailable,
                                                  "Missing dlssg_to_fsr3_amd_is_better.dll");

    auto constexpr fgNvngxArtursIndex = (uint32_t) FGNvngxReplacement::Arturs;
    nvngxOptions[fgNvngxArtursIndex].set_disabled(!state.artursFgFileAvailable, "Missing dlss-enabler-headless.dll");

    auto constexpr fgNvngxFfxIndex = (uint32_t) FGNvngxReplacement::FFX;
    nvngxOptions[fgNvngxFfxIndex].set_disabled(!FfxApiProxy::IsFGReady(false),
                                               "Missing amd_fidelityfx_framegeneration_dx12.dll");

    auto constexpr fgNvngxComboIndex = (uint32_t) FGNvngxReplacement::Combo;
    nvngxOptions[fgNvngxComboIndex].set_disabled(
        !FfxApiProxy::IsFGReady(false) || !state.artursFgFileAvailable,
        "Missing amd_fidelityfx_framegeneration_dx12.dll\nor missing dlss-enabler-headless.dll");

    // TODO: Automatically switch to any other option

    if (!config->FGNvngxReplacement.has_value())
        config->FGNvngxReplacement = config->FGNvngxReplacement.value_or_default(); // need to have a value before combo

    if (state.activeFgInput != FGInput::ForceXeLL)
    {
        D18Ui::SeparatorText("Frame Generation");

        if (ImGui::BeginTable("fgSelection", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();

            PopulateCombo("FG Input", config->FGInput, inputOptions);
            ShowTooltip("The data source to be used for FG\n"
                        "The native FG which the game supports");

            ImGui::TableNextColumn();

            if (replaceFgOutputWithNvngx)
            {
                // Disable None?
                PopulateCombo("FG Nvngx", config->FGNvngxReplacement, nvngxOptions);
                ShowTooltip("What backend to use instead of the real DLSSG");
            }
            else
            {
                PopulateCombo("FG Output", config->FGOutput, outputOptions);
                ShowTooltip("The FG that you will actually be using");
            }

            ImGui::EndTable();
        }

        // Should be on a new line
        if (showNvngxFgDowndown)
        {
            PopulateCombo("FG Nvngx Replacement", config->FGNvngxReplacement, nvngxOptions);
            ShowTooltip("What backend to use instead of the real DLSSG");
        }

        // Try to avoid having None selected when the gpu doesn't support DLSSG + some fallbacks
        if (!supportsDlssg && (replaceFgOutputWithNvngx || showNvngxFgDowndown) &&
            config->FGNvngxReplacement.value_or_default() == FGNvngxReplacement::None)
        {
            if (state.nukemsFgFileAvailable)
                config->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::Nukems);

            else if (state.artursFgFileAvailable)
                config->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::Arturs);

            else if (FfxApiProxy::IsFGReady(false))
                config->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::FFX);
        }

        const bool nvngxFgChanged = (replaceFgOutputWithNvngx || showNvngxFgDowndown) &&
                                    state.activeFgNvngx != config->FGNvngxReplacement.value_or_default();
        state.fgSettingsChanged = state.activeFgOutput != config->FGOutput.value_or_default() ||
                                  state.activeFgInput != config->FGInput.value_or_default() || nvngxFgChanged;

        if (state.fgSettingsChanged)
        {
            ImGui::Spacing();
            D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.0f, 1.f)),
                               "Finish choosing input, output and enabled state, then save settings and restart once.");
            ImGui::Spacing();
        }

        const bool dlssgInputOrOutput =
            state.activeFgOutput == FGOutput::DLSSG || state.activeFgInput == FGInput::DLSSG;

        ImGui::BeginDisabled(state.dlssgGameDMFGSupported && config->FGDLSSGOverrideForceDMFG.value_or_default());
        if (state.dlssgMfgMax.has_value() && state.dlssgMfgMax.value() >= 1 && !dlssgInputOrOutput)
        {
            auto maxInterpolationCount = state.dlssgMfgMax.value();

            if (maxInterpolationCount >= 1)
            {
                const char* intModes[] = { "Default", "Off", "2X", "3X", "4X", "5X", "6X" };

                // Map config value to UI index
                int currentSet = 0;
                if (config->FGDLSSGOverrideInterpolationCount.has_value())
                {
                    currentSet = config->FGDLSSGOverrideInterpolationCount.value() + 1;
                }

                const char* currentIntCount = intModes[currentSet];

                ImGui::PushItemWidth(95.0f * menuResScale);

                if (D18Ui::BeginCombo("Override DLSSG Ratio", currentIntCount))
                {
                    for (int i = 0; i <= maxInterpolationCount + 1; i++)
                    {
                        if (D18Ui::Selectable(intModes[i], (currentSet == i)))
                        {
                            if (i == 0)
                            {
                                // Default, no override
                                config->FGDLSSGOverrideInterpolationCount.reset();
                            }
                            else
                            {
                                // UI index, store value
                                int framesToGenerate = i - 1;

                                LOG_DEBUG("DLSSG Interpolation Count set to: {}", framesToGenerate);
                                config->FGDLSSGOverrideInterpolationCount = framesToGenerate;
                            }

                            StreamlineHooks::updateDlssgOptions();
                        }
                    }

                    ImGui::EndCombo();
                }

                ImGui::PopItemWidth();
            }
        }

        ImGui::EndDisabled();

        if (state.dlssgGameDMFGSupported && !dlssgInputOrOutput)
        {
            ImGui::SameLine(0.0f, 16.0f);

            if (bool dynamicMFG = config->FGDLSSGOverrideForceDMFG.value_or_default();
                D18Ui::Checkbox("Force Dynamic MFG", &dynamicMFG))
            {
                config->FGDLSSGOverrideForceDMFG = dynamicMFG;
                StreamlineHooks::updateDlssgOptions();
            }

            ImGui::BeginDisabled(state.dlssgLastSetMode != sl::DLSSGMode::eDynamic);
            static float fpsTarget = config->FGDLSSGFramerateTargetDMFG.value_or_default();
            D18Ui::SliderFloat("DMFG FPS Target", &fpsTarget, 0, 200, "%.0f");

            ShowHelpMarker("An active limit of 0 means auto-detect the display refresh rate");

            if (D18Ui::Button("Apply Target"))
            {
                config->FGDLSSGFramerateTargetDMFG = fpsTarget;
                StreamlineHooks::updateDlssgOptions();
            }

            ImGui::SameLine(0.0f, 16.0f);

            if (D18Ui::Button("Reset Target"))
            {
                fpsTarget = 0.0f;
                config->FGDLSSGFramerateTargetDMFG.reset();
            }

            ImGui::EndDisabled();
        }

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
        if (((state.activeFgOutput == FGOutput::FSRFG || state.activeFgOutput == FGOutput::XeFG ||
              state.activeFgOutput == FGOutput::DLSSG) &&
             state.activeFgInput != FGInput::NoFG && state.activeFgInput != FGInput::NvngxFG) &&
            fgOutput)
        {
            D18Ui::Checkbox("Show Detected UI", &state.fgHudlessCompare);
            ShowHelpMarker("Needs HUDless texture to compare with final image.\n"
                           "UI elements and ONLY UI elements should have a pink tint!");

            const auto isUsingUIAny = fgOutput->IsUsingUIAny();

            ImGui::BeginDisabled(!isUsingUIAny);

            if (bool drawUIOverFG = config->FGDrawUIOverFG.value_or_default();
                D18Ui::Checkbox("Draw UI over", &drawUIOverFG))
            {
                config->FGDrawUIOverFG = drawUIOverFG;
            }
            ShowHelpMarker("Draws UI resource over the final image\n"
                           "If no UI visible, enable this!");

            ImGui::EndDisabled();

            ImGui::SameLine(0.0f, 16.0f);

            ImGui::BeginDisabled(!isUsingUIAny || !config->FGDrawUIOverFG.value_or_default());

            if (bool uiPremultipliedAlpha = config->FGUIPremultipliedAlpha.value_or_default();
                D18Ui::Checkbox("UI Premult. alpha", &uiPremultipliedAlpha))
            {
                config->FGUIPremultipliedAlpha = uiPremultipliedAlpha;
            }
            ShowHelpMarker("If UI is too faint, disable this option");

            ImGui::EndDisabled();
        }

        const bool showOutputSpecificFGSettings = state.activeFgInput == FGInput::DLSSG ||
                                                  state.activeFgInput == FGInput::FSRFG ||
                                                  state.activeFgInput == FGInput::FSRFG30;

        const bool showHudCutoff = state.activeFgInput == FGInput::NvngxFG || state.activeFgOutput == FGOutput::FSRFG;

        if (showOutputSpecificFGSettings || showHudCutoff)
        {
            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader("Advanced FG Settings"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                if (showOutputSpecificFGSettings)
                {
                    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
                    if (fgOutput)
                    {
                        ImGui::BeginDisabled(!fgOutput->IsActive());

                        const auto isUsingUIAny = fgOutput->IsUsingUIAny();
                        const auto isUsingHudlessAny = fgOutput->IsUsingHudlessAny();

                        bool disableUI = config->FGDisableUI.value_or_default();
                        ImGui::BeginDisabled(!isUsingUIAny && !disableUI);

                        if (D18Ui::Checkbox("Disable UI texture", &disableUI))
                        {
                            config->FGDisableUI = disableUI;
                            fgOutput->UpdateTarget();
                        }

                        ShowHelpMarker("For when the game sends a UI texture, but you want to disable it");

                        ImGui::EndDisabled();

                        ImGui::SameLine(0.0f, 16.0f);

                        bool disableHudless = config->FGDisableHudless.value_or_default();
                        ImGui::BeginDisabled(!isUsingHudlessAny && !disableHudless);

                        if (D18Ui::Checkbox("Disable HUDless", &disableHudless))
                        {
                            config->FGDisableHudless = disableHudless;
                        }

                        ShowHelpMarker("For when the game sends HUDless, but you want to disable it");

                        ImGui::EndDisabled();

                        bool depthValidNow = config->FGDepthValidNow.value_or_default();
                        if (D18Ui::Checkbox("Depth as ValidNow", &depthValidNow))
                            config->FGDepthValidNow = depthValidNow;

                        ShowHelpMarker("Will use more VRAM, but Uniscaler needs this\n"
                                       "Maybe some other games might need too");

                        ImGui::SameLine(0.0f, 16.0f);

                        bool velocityValidNow = config->FGVelocityValidNow.value_or_default();
                        if (D18Ui::Checkbox("Velocity as ValidNow", &velocityValidNow))
                            config->FGVelocityValidNow = velocityValidNow;

                        ShowHelpMarker("Will use more VRAM, but Uniscaler needs this\n"
                                       "Maybe some other games might need too");

                        bool hudlessValidNow = config->FGHudlessValidNow.value_or_default();
                        if (D18Ui::Checkbox("HUDless as ValidNow", &hudlessValidNow))
                            config->FGHudlessValidNow = hudlessValidNow;

                        ShowHelpMarker("Will use more VRAM, but some games might need this");

                        ImGui::SameLine(0.0f, 16.0f);

                        bool firstHudless = config->FGOnlyAcceptFirstHudless.value_or_default();
                        if (D18Ui::Checkbox("Accept First HUDless", &firstHudless))
                            config->FGOnlyAcceptFirstHudless = firstHudless;

                        ShowHelpMarker("If source tags more than one HUDless, only use the first one");

                        if (bool skipReset = config->FGSkipReset.value_or_default();
                            D18Ui::Checkbox("Skip Reset", &skipReset))
                        {
                            config->FGSkipReset = skipReset;
                        }

                        ShowHelpMarker("Don't use reset signals from FG Inputs");

                        ImGui::EndDisabled();

                        ImGui::PushItemWidth(80.0f * menuResScale);

                        auto frameAhead = config->FGAllowedFrameAhead.value_or_default();
                        if (D18Ui::InputInt("Frame Ahead", &frameAhead, 1, 1) && frameAhead > 0 && frameAhead < 4)
                        {
                            config->FGAllowedFrameAhead = frameAhead;
                        }

                        ShowHelpMarker("Number of frames the FG is allowed to be ahead of the game\n"
                                       "Might prevent FG on/off switching, but also might cause issues");

                        ImGui::PopItemWidth();

                        ImGui::SameLine(0.0f, 16.0f);

                        const char* ftSources[] = { "Input", "Opti", "Zero" };
                        const char* ftSourceInfos[] = { "Uses frametimes provided by\nDLSSG or FSR-FG ",
                                                        "Uses frametimes calculated by Opti",
                                                        "Let XeFG to handle frametimes" };

                        auto currentSet = (int) config->FTInput.value_or_default();
                        auto currentSourceCount = state.activeFgOutput == FGOutput::XeFG ? 3 : 2;

                        ImGui::PushItemWidth(95.0f * menuResScale);

                        if (D18Ui::BeginCombo("FT Input", ftSources[currentSet]))
                        {
                            for (size_t i = 0; i < currentSourceCount; i++)
                            {

                                if (D18Ui::Selectable(ftSources[i], currentSet == i))
                                {
                                    LOG_DEBUG("FTInput has changed {} -> {}", ftSources[currentSet], ftSources[i]);
                                    config->FTInput = (FrameTimeSource) i;
                                }

                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                    ImGui::SetTooltip(ftSourceInfos[i]);
                            }

                            ImGui::EndCombo();
                        }

                        ImGui::PopItemWidth();

                        ShowHelpMarker("Select source for frametime\n"
                                       "Might help frame pacing and stutter issues");
                    }
                }

                if (showHudCutoff)
                {
                    float fgHudCutoff = config->FGHudCutoff.value_or_default();
                    if (D18Ui::SliderFloat("Hud Cutoff", &fgHudCutoff, 0.00f, 1.0f, "%.2f"))
                        config->FGHudCutoff = fgHudCutoff;

                    ShowHelpMarker("Cutoffs transparency from UI to help with interpolation\n"
                                   "You can use Show Detected UI to see the difference\n0.0 is auto");
                }
            }
        }
    }
}

void MenuCommon::RenderFrameGenerationRuntimeSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;
    auto fgOutput = state.currentFG;

    // FSR FG controls
    if (state.activeFgOutput == FGOutput::FSRFG && state.activeFgInput != FGInput::NoFG &&
        state.currentFGSwapchain != nullptr)
    {
        if (state.activeFgInput != FGInput::Upscaler ||
            (currentFeature != nullptr && !currentFeature->IsFrozen()) && FfxApiProxy::IsFGReady())
        {
            D18Ui::SeparatorText("Frame Generation (FSR FG)");

            if (_ffxFGIndex < 0)
                _ffxFGIndex = config->FfxFGIndex.value_or_default();

            if (state.ffxFGVersionNames.size() > 0)
            {
                ImGui::PushItemWidth(135.0f * menuResScale);

                auto currentName = D18Ui::Format("FSR %s", state.ffxFGVersionNames[_ffxFGIndex]);
                if (D18Ui::BeginCombo("FFX FG", currentName.c_str()))
                {
                    for (int n = 0; n < state.ffxFGVersionIds.size(); n++)
                    {
                        auto name = D18Ui::Format("FSR %s", state.ffxFGVersionNames[n]);
                        if (D18Ui::Selectable(name.c_str(), config->FfxFGIndex.value_or_default() == n))
                            _ffxFGIndex = n;
                    }

                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ShowHelpMarker("List of FGs reported by FFX SDK");

                ImGui::SameLine(0.0f, 6.0f);

                if (D18Ui::Button("Change FG") && _ffxFGIndex != config->FfxFGIndex.value_or_default())
                {
                    config->FfxFGIndex = _ffxFGIndex;
                    state.fgChanged = true;
                    state.scChanged = true;
                }
            }

            bool fgActive = config->FGEnabled.value_or_default();
            if (D18Ui::Checkbox("Active##2", &fgActive))
            {
                config->FGEnabled = fgActive;
                LOG_DEBUG("FGEnabled set FGEnabled: {}", fgActive);

                if (config->FGEnabled.value_or_default())
                    state.fgChanged = true;
            }
            ShowHelpMarker("Enable Frame Generation");

            bool fgAsync = config->FGAsync.value_or_default();
            if (D18Ui::Checkbox("Allow Async", &fgAsync))
            {
                config->FGAsync = fgAsync;

                if (config->FGEnabled.value_or_default())
                {
                    state.fgChanged = true;
                    state.scChanged = true;
                    LOG_DEBUG("Async set FGChanged");
                }
            }
            ShowHelpMarker("Enable Async for better FG performance\nMight cause crashes, especially with HUD Fix!");

            ImGui::SameLine(0.0f, 16.0f);

            bool fgDV = config->FGDebugView.value_or_default();
            if (D18Ui::Checkbox("Debug View##2", &fgDV))
            {
                config->FGDebugView = fgDV;

                if (config->FGEnabled.value_or_default())
                {
                    state.fgChanged = true;
                    LOG_DEBUG("DebugView set FGChanged");
                }
            }
            ShowHelpMarker("Enable FSR3.1-FG Debug view\n\n"
                           "Top left: Game Motion Vectors\n"
                           "Top middle: GMV Depth\n"
                           "Top right: Optical Flow MV\n"
                           "Middle: Interpolated frame only\n"
                           "Bottom left: Disocclusion mask\n"
                           "Bottom middle: Interpolation source (w/o UI)\n"
                           "Bottom right: HUDless resource");

            ImGui::SameLine(0.0f, 16.0f);

            if (state.currentFG && state.currentFG->Version().major > 3)
            {
                if (bool fgwm = config->FSRFGEnableWatermark.value_or_default();
                    D18Ui::Checkbox("Enable Watermark", &fgwm))
                {
                    LOG_DEBUG("FSRFGEnableWatermark set FGWatermark: {}", fgwm);
                    config->FSRFGEnableWatermark = fgwm;
                }

                ShowHelpMarker("After changing this option, please Save Settings\n"
                               "It will be applied on next launch.");
            }

            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader("Extended FSR FG Settings"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                D18Ui::Checkbox("FG Only Generated", &state.fgOnlyGenerated);
                ShowHelpMarker("Display only FSR 3.1 Generated frames");

                ImGui::SameLine(0.0f, 16.0f);
                auto debugResetLines = config->FGDebugResetLines.value_or_default();
                if (D18Ui::Checkbox("Debug Reset Lines", &debugResetLines))
                {
                    config->FGDebugResetLines = debugResetLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugResetLines);
                }
                ShowHelpMarker("Enables drawing of Interpolation skip lines");

                auto debugTearLines = config->FGDebugTearLines.value_or_default();
                if (D18Ui::Checkbox("Debug Tear Lines", &debugTearLines))
                {
                    config->FGDebugTearLines = debugTearLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugTearLines);
                }
                ShowHelpMarker("Enables drawing of Tear and Interpolation skip lines");

                ImGui::SameLine(0.0f, 16.0f);
                auto debugPacingLines = config->FGDebugPacingLines.value_or_default();
                if (D18Ui::Checkbox("Debug Pacing Lines", &debugPacingLines))
                {
                    config->FGDebugPacingLines = debugPacingLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugPacingLines);
                }
                ShowHelpMarker("Enables drawing of Pacing lines");

                ImGui::Spacing();
                if (D18Ui::TreeNode("FG Rectangle Settings"))
                {
                    ImGui::PushItemWidth(95.0f * menuResScale);
                    int rectLeft = config->FGRectLeft.value_or(0);
                    if (D18Ui::InputInt("Rect Left", &rectLeft))
                        config->FGRectLeft = rectLeft;

                    ImGui::SameLine(0.0f, 16.0f);
                    int rectTop = config->FGRectTop.value_or(0);
                    if (D18Ui::InputInt("Rect Top", &rectTop))
                        config->FGRectTop = rectTop;

                    int rectWidth = config->FGRectWidth.value_or(0);
                    if (D18Ui::InputInt("Rect Width", &rectWidth))
                        config->FGRectWidth = rectWidth;

                    ImGui::SameLine(0.0f, 16.0f);
                    int rectHeight = config->FGRectHeight.value_or(0);
                    if (D18Ui::InputInt("Rect Height", &rectHeight))
                        config->FGRectHeight = rectHeight;

                    ImGui::PopItemWidth();
                    ShowHelpMarker("Frame generation rectangle, adjust for letterboxed content");

                    ImGui::BeginDisabled(!config->FGRectLeft.has_value() && !config->FGRectTop.has_value() &&
                                         !config->FGRectWidth.has_value() && !config->FGRectHeight.has_value());

                    if (D18Ui::Button("Reset FG Rect"))
                    {
                        config->FGRectLeft.reset();
                        config->FGRectTop.reset();
                        config->FGRectWidth.reset();
                        config->FGRectHeight.reset();
                    }

                    ShowHelpMarker("Resets Frame generation rectangle");

                    ImGui::EndDisabled();
                    ImGui::TreePop();
                }

                auto fg = state.currentFG;
                if (fg != nullptr && strcmp(fg->Name(), "FSR-FG") == 0 &&
                    FfxApiProxy::VersionDx12_FG() >= feature_version { 3, 1, 3 })
                {
                    ImGui::Spacing();

                    if (D18Ui::TreeNode("Frame Pacing Tuning"))
                    {
                        auto fptEnabled = config->FGFramePacingTuning.value_or_default();
                        if (D18Ui::Checkbox("Enable Tuning", &fptEnabled))
                        {
                            config->FGFramePacingTuning = fptEnabled;
                            state.fsrfgFramePaceTuningChanged = true;
                        }

                        ImGui::BeginDisabled(!config->FGFramePacingTuning.value_or_default());

                        ImGui::PushItemWidth(115.0f * menuResScale);
                        auto fptSafetyMargin = config->FGFPTSafetyMarginInMs.value_or_default();
                        if (D18Ui::InputFloat("Safety Margins in ms", &fptSafetyMargin, 0.01f, 0.1f, "%.2f"))
                            config->FGFPTSafetyMarginInMs = fptSafetyMargin;
                        ShowHelpMarker("Safety margins in millisecons\n"
                                       "FSR default value: 0.1ms\n"
                                       "Opti default value: 0.01ms");

                        auto fptVarianceFactor = config->FGFPTVarianceFactor.value_or_default();
                        if (D18Ui::SliderFloat("Variance Factor", &fptVarianceFactor, 0.0f, 1.0f, "%.2f"))
                            config->FGFPTVarianceFactor = fptVarianceFactor;
                        ShowHelpMarker("Variance factor\n"
                                       "FSR default value: 0.1\n"
                                       "Opti default value: 0.3");
                        ImGui::PopItemWidth();

                        auto fpHybridSpin = config->FGFPTAllowHybridSpin.value_or_default();
                        if (D18Ui::Checkbox("Enable Hybrid Spin", &fpHybridSpin))
                            config->FGFPTAllowHybridSpin = fpHybridSpin;
                        ShowHelpMarker("Allows pacing spinlock to sleep, should reduce CPU usage\n"
                                       "Might cause slow ramp up of FPS");

                        ImGui::PushItemWidth(115.0f * menuResScale);
                        auto fptHybridSpinTime = config->FGFPTHybridSpinTime.value_or_default();
                        if (D18Ui::SliderInt("Hybrid Spin Time", &fptHybridSpinTime, 0, 100))
                            config->FGFPTHybridSpinTime = fptHybridSpinTime;
                        ShowHelpMarker("How long to spin if FPTHybridSpin is true. Measured in timer "
                                       "resolution units.\n"
                                       "Not recommended to go below 2. Will result in frequent overshoots");
                        ImGui::PopItemWidth();

                        auto fpWaitForSingleObjectOnFence =
                            config->FGFPTAllowWaitForSingleObjectOnFence.value_or_default();
                        if (D18Ui::Checkbox("Enable WaitForSingleObjectOnFence", &fpWaitForSingleObjectOnFence))
                        {
                            config->FGFPTAllowWaitForSingleObjectOnFence = fpWaitForSingleObjectOnFence;
                        }
                        ShowHelpMarker("Allows WaitForSingleObject instead of spinning for fence value");

                        if (D18Ui::Button("Apply Timing Changes"))
                            state.fsrfgFramePaceTuningChanged = true;

                        ImGui::EndDisabled();
                        ImGui::TreePop();
                    }
                }

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }

    // XeFG controls
    if (state.activeFgOutput == FGOutput::XeFG && state.activeFgInput != FGInput::NoFG &&
        state.activeFgInput != FGInput::ForceXeLL && state.currentFGSwapchain != nullptr && XeFGProxy::InitXeFG() &&
        fgOutput)
    {
        D18Ui::SeparatorText("Frame Generation (XeFG)");

        bool ignoreChecks = config->FGXeFGIgnoreInitChecks.value_or_default();

        bool nativeAA = false;
        if (state.activeFgInput == FGInput::Upscaler && currentFeature != nullptr)
            nativeAA = currentFeature->RenderWidth() == currentFeature->DisplayWidth();

        const bool correctMVs = fgOutput->IsLowResMV() || nativeAA ||
                                (State::Instance().gameQuirks & GameQuirk::ForceFGRenderSizeMVs) || ignoreChecks;

        if (!correctMVs || state.realExclusiveFullscreen)
        {
            config->FGEnabled.reset();
            config->FGXeFGDebugView.reset();
        }

        const bool restartNeeded = config->FGXeFGDepthInverted.value_or_default() != fgOutput->IsInvertedDepth() ||
                                   config->FGXeFGJitteredMV.value_or_default() != fgOutput->IsJitteredMVs() ||
                                   config->FGXeFGHighResMV.value_or_default() == fgOutput->IsLowResMV();

        bool cantActivate = false;
        if (restartNeeded)
        {
            D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                               "Restart the game to apply correct XeFG settings!");
        }
        else
        {
            if (!correctMVs)
                D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                                   "Requires disabling dilated motion vectors");

            if (!ignoreChecks && state.realExclusiveFullscreen)
            {
                cantActivate = true;
                D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "Borderless display mode required!");
            }

            if (!ignoreChecks && state.isHdrActive)
            {
                if (state.currentSwapchainDesc.BufferDesc.Format >= DXGI_FORMAT_R32G32B32A32_TYPELESS &&
                    state.currentSwapchainDesc.BufferDesc.Format <= DXGI_FORMAT_R16G16B16A16_SINT)
                {
                    cantActivate = true;
                    D18Ui::TextColored(toneMapColor(ImVec4(1.0f, 0.0f, 0.0f, 1.f)), "XeFG only supports HDR10");
                }
            }
        }

        if (!correctMVs || cantActivate || ignoreChecks)
        {
            if (D18Ui::Checkbox("Ignore Init Checks", &ignoreChecks))
                config->FGXeFGIgnoreInitChecks = ignoreChecks;

            ShowHelpMarker("Ignores all prechecks for XeFG\n"
                           "Don't use this option to skip MV size warning for UE games!\n"
                           "It might cause crashes and bad IQ!");
        }

        ImGui::BeginDisabled(!correctMVs || cantActivate);

        bool fgActive = config->FGEnabled.value_or_default();
        if (D18Ui::Checkbox("Active##3", &fgActive))
        {
            config->FGEnabled = fgActive;
            LOG_DEBUG("Enabled set FGEnabled: {}", fgActive);

            if (config->FGEnabled.value_or_default())
                state.fgChanged = true;
        }

        ShowHelpMarker("Enable Frame Generation");

        auto maxInterpolationCount = fgOutput->GetMaxInterpolationCount();

        if (maxInterpolationCount > 1)
        {
            ImGui::SameLine(0.0f, 16.0f);

            const char* intModes[] = { "2X", "3X", "4X", "5X", "6X" };
            auto currentSet = fgOutput->GetInterpolatedFrameCount() - 1;
            auto currentIntCount = intModes[currentSet];

            ImGui::PushItemWidth(95.0f * menuResScale);

            if (D18Ui::BeginCombo("MFG", currentIntCount))
            {
                for (int i = 0; i < maxInterpolationCount; i++)
                {
                    if (D18Ui::Selectable(intModes[i], (currentSet == i)))
                    {
                        LOG_DEBUG("XeFG Interpolation Count set to: {}", i + 1);
                        state.fgChanged = true;
                        config->FGXeFGInterpolationCount = i + 1;
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::PopItemWidth();

            ShowHelpMarker("Set XeFG interpolation count");
        }

        ImGui::SameLine(0.0f, 16.0f);
        ImGui::BeginDisabled(!fgOutput->IsUsingHudlessAny() || XeFGProxy::SetUiCompositionState() == nullptr);
        bool fgCompositeUI = config->FGXeFGUIComposition.value_or_default();
        if (D18Ui::Checkbox("UI Composition", &fgCompositeUI))
            config->FGXeFGUIComposition = fgCompositeUI;

        ShowHelpMarker("Disable HUD/UI interpolation\n"
                       "Reverts back to previous XeFG 2 behaviour\n\n"
                       "Fixes artifacting transparent HUD/UI");
        ImGui::EndDisabled();

        bool fgDV = config->FGXeFGDebugView.value_or_default();
        if (D18Ui::Checkbox("Debug View##2", &fgDV))
        {
            config->FGXeFGDebugView = fgDV;

            if (config->FGXeFGDebugView.value_or_default())
            {
                state.fgChanged = true;
                LOG_DEBUG("DebugView set FGChanged");
            }
        }
        ShowHelpMarker("Enable XeFG Debug view");

        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 16.0f);
        bool fgBorderless = config->FGXeFGForceBorderless.value_or_default();
        if (D18Ui::Checkbox("Force Borderless", &fgBorderless))
            config->FGXeFGForceBorderless = fgBorderless;

        ShowHelpMarker("Forces Borderless display mode\n\n"
                       "For best results, set fullscreen \n"
                       "resolution to your display resolution\n"
                       "Might cause some instability issues.\n\n"
                       "NEEDS GAME RESTART TO BE ACTIVE!");

        // Disable this for now
        // ImGui::SameLine(0.0f, 16.0f);
        // D18Ui::Checkbox("Only Generated##2", &state.fgOnlyGenerated);
        // ShowHelpMarker("Display only XeFG generated frames");

        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("Extended XeFG Settings"); ch.IsHeaderOpen())
        {
            ImGui::Spacing();
            if (D18Ui::TreeNode("Rectangle Settings"))
            {
                ImGui::PushItemWidth(95.0f * menuResScale);
                int rectLeft = config->FGRectLeft.value_or(0);
                if (D18Ui::InputInt("Rect Left##2", &rectLeft))
                    config->FGRectLeft = rectLeft;

                ImGui::SameLine(0.0f, 16.0f);
                int rectTop = config->FGRectTop.value_or(0);
                if (D18Ui::InputInt("Rect Top##2", &rectTop))
                    config->FGRectTop = rectTop;

                int rectWidth = config->FGRectWidth.value_or(0);
                if (D18Ui::InputInt("Rect Width##2", &rectWidth))
                    config->FGRectWidth = rectWidth;

                ImGui::SameLine(0.0f, 16.0f);
                int rectHeight = config->FGRectHeight.value_or(0);
                if (D18Ui::InputInt("Rect Height##2", &rectHeight))
                    config->FGRectHeight = rectHeight;

                ImGui::PopItemWidth();
                ShowHelpMarker("Frame generation rectangle, adjust for letterboxed content##2");

                ImGui::BeginDisabled(!config->FGRectLeft.has_value() && !config->FGRectTop.has_value() &&
                                     !config->FGRectWidth.has_value() && !config->FGRectHeight.has_value());

                if (D18Ui::Button("Reset FG Rect##2"))
                {
                    config->FGRectLeft.reset();
                    config->FGRectTop.reset();
                    config->FGRectWidth.reset();
                    config->FGRectHeight.reset();
                }

                ShowHelpMarker("Resets Frame generation rectangle##2");

                ImGui::EndDisabled();
                ImGui::TreePop();
            }

            ImGui::Spacing();
            ImGui::Spacing();
        }
    }

    // DLSSG controls
    if (state.activeFgOutput == FGOutput::DLSSG && state.activeFgInput != FGInput::NoFG &&
        state.currentFGSwapchain != nullptr && StreamlineProxy::LoadStreamline() && fgOutput)
    {
        D18Ui::SeparatorText("Frame Generation (DLSSG)");

        if (state.activeFgNvngx == FGNvngxReplacement::None && state.isHdrActive)
        {
            if (state.currentSwapchainDesc.BufferDesc.Format >= DXGI_FORMAT_R32G32B32A32_TYPELESS &&
                state.currentSwapchainDesc.BufferDesc.Format <= DXGI_FORMAT_R16G16B16A16_SINT)
            {
                D18Ui::TextColored(toneMapColor(ImVec4(1.0f, 0.0f, 0.0f, 1.f)), "DLSSG only supports HDR10");
            }
        }

        D18Ui::Text("Current DLSSG state:");
        ImGui::SameLine();
        if (auto count = state.dlssgDetectedInterpolationCount; count > 0)
        {
            D18Ui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)), std::format("ON {}x", count + 1).c_str());
        }
        else
        {
            D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "OFF");
        }

        bool fgActive = config->FGEnabled.value_or_default();
        if (D18Ui::Checkbox("Active##4", &fgActive))
        {
            config->FGEnabled = fgActive;
            LOG_DEBUG("Enabled set FGEnabled: {}", fgActive);

            if (config->FGEnabled.value_or_default())
                state.fgChanged = true;
        }

        ShowHelpMarker("Enable Frame Generation");

        auto maxInterpolationCount = fgOutput->GetMaxInterpolationCount();

        if (maxInterpolationCount > 1)
        {
            ImGui::SameLine(0.0f, 16.0f);

            ImGui::BeginDisabled(config->FGDLSSGForceDMFG.value_or_default());

            const char* intModes[] = { "2X", "3X", "4X", "5X", "6X" };
            auto currentSet = fgOutput->GetInterpolatedFrameCount() - 1;
            auto currentIntCount = intModes[currentSet];

            ImGui::PushItemWidth(95.0f * menuResScale);

            if (D18Ui::BeginCombo("MFG", currentIntCount))
            {
                for (int i = 0; i < maxInterpolationCount; i++)
                {
                    if (D18Ui::Selectable(intModes[i], (currentSet == i)))
                    {
                        LOG_DEBUG("DLSSG Interpolation Count set to: {}", i + 1);
                        config->FGDLSSGInterpolationCount = i + 1;
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::PopItemWidth();

            ShowHelpMarker("Set DLSSG interpolation count");

            ImGui::EndDisabled();

            if (fgOutput->GetDMFGSupport())
            {
                ImGui::SameLine(0.0f, 16.0f);

                if (bool dynamicMFG = config->FGDLSSGForceDMFG.value_or_default();
                    D18Ui::Checkbox("Force Dynamic MFG", &dynamicMFG))
                {
                    config->FGDLSSGForceDMFG = dynamicMFG;
                }

                ImGui::BeginDisabled(!config->FGDLSSGForceDMFG.value_or_default());
                static float fpsTarget = config->FGDLSSGFramerateTargetDMFG.value_or_default();
                D18Ui::SliderFloat("DMFG FPS Target", &fpsTarget, 0, 200, "%.0f");

                ShowHelpMarker("An active limit of 0 means auto-detect the display refresh rate");

                if (D18Ui::Button("Apply Target"))
                {
                    config->FGDLSSGFramerateTargetDMFG = fpsTarget;
                }

                ImGui::SameLine(0.0f, 16.0f);

                if (D18Ui::Button("Reset Target"))
                {
                    fpsTarget = 0.0f;
                    config->FGDLSSGFramerateTargetDMFG.reset();
                }

                ImGui::EndDisabled();
            }
        }

        bool useGamesMarkers = config->FGDLSSGUseGamesReflexMarkers.value_or_default();
        ImGui::BeginDisabled(!ReflexHooks::gameIsSendingMarkers());
        if (D18Ui::Checkbox("Use Game's Reflex Markers", &useGamesMarkers))
        {
            config->FGDLSSGUseGamesReflexMarkers = useGamesMarkers;
            LOG_DEBUG("Changed set FGDLSSGUseGamesReflexMarkers: {}", useGamesMarkers);
        }
        ImGui::EndDisabled();
    }

    // OptiFG
    if (state.api != API::Vulkan && state.currentFGSwapchain != nullptr && state.activeFgInput == FGInput::Upscaler)
    {
        SeparatorWithHelpMarker("Frame Generation (OptiFG)", "Using upscaler data for FG");

        if (currentFeature != nullptr && !currentFeature->IsFrozen() &&
            ((state.activeFgOutput == FGOutput::FSRFG && FfxApiProxy::IsFGReady()) ||
             (state.activeFgOutput == FGOutput::XeFG && XeFGProxy::Module() != nullptr) ||
             (state.activeFgOutput == FGOutput::DLSSG && StreamlineProxy::Module() != nullptr)))
        {
            if (!Config::Instance()->FGDisableHUDFix.value_or_default() &&
                state.swapchainInteropApi == SwapchainInteropApi::None)
            {
                bool fgHudfix = config->FGHUDFix.value_or_default();

                if (D18Ui::Checkbox("HUDFix", &fgHudfix))
                {
                    config->FGHUDFix = fgHudfix;
                    LOG_DEBUG("Enabled set FGHUDFix: {}", fgHudfix);
                    state.clearCapturedHudlesses = true;
                    state.fgChanged = true;
                }

                ShowHelpMarker("Enable HUD stability fix, might cause crashes!");

                ImGui::BeginDisabled(!config->FGHUDFix.value_or_default());

                ImGui::SameLine(0.0f, 16.0f);
                ImGui::PushItemWidth(95.0f * menuResScale);
                int hudFixLimit = config->FGHUDLimit.value_or_default();
                if (D18Ui::InputInt("Limit", &hudFixLimit))
                {
                    if (hudFixLimit < 1)
                        hudFixLimit = 1;
                    else if (hudFixLimit > 999)
                        hudFixLimit = 999;

                    config->FGHUDLimit = hudFixLimit;
                    LOG_DEBUG("Enabled set FGHUDLimit: {}", hudFixLimit);
                }
                ShowHelpMarker("Delay HUDless capture, high values might cause crash!");

                ImGui::SameLine(0.0f, 16.0f);
                if (D18Ui::Button("Res##2"))
                    _showHudlessWindow = !_showHudlessWindow;

                ImGui::EndDisabled();

                auto hudExtended = config->FGHUDFixExtended.value_or_default();
                if (D18Ui::Checkbox("Extended", &hudExtended))
                {
                    LOG_DEBUG("Enabled set FGHUDFixExtended: {}", hudExtended);
                    config->FGHUDFixExtended = hudExtended;
                }
                ShowHelpMarker("Extended format checks for possible HUDless\nMight cause crashes and slowdowns!");
                ImGui::SameLine(0.0f, 16.0f);

                ImGui::BeginDisabled(!config->FGHUDFix.value_or_default());

                auto immediate = config->FGImmediateCapture.value_or_default();
                if (D18Ui::Checkbox("Immediate Capture", &immediate))
                {
                    LOG_DEBUG("Enabled set FGImmediateCapture: {}", immediate);
                    config->FGImmediateCapture = immediate;
                }
                ShowHelpMarker("Enables capturing of resources before shader execution.\nIncrease HUDless "
                               "capture chances, but might cause capturing of unnecessary resources.");

                ImGui::PopItemWidth();

                ImGui::EndDisabled();
            }

            bool depthScale = config->FGEnableDepthScale.value_or_default();
            if (D18Ui::Checkbox("Scale Depth to fix DLSS RR", &depthScale))
                config->FGEnableDepthScale = depthScale;
            ShowHelpMarker("Fix for DLSS-D wrong depth inputs");

            bool resourceFlip = config->FGResourceFlip.value_or_default();
            if (D18Ui::Checkbox("Flip (Unity)", &resourceFlip))
                config->FGResourceFlip = resourceFlip;
            ShowHelpMarker("Flip Velocity & Depth resources of Unity games");

            ImGui::SameLine(0.0f, 16.0f);

            bool resourceFlipOffset = config->FGResourceFlipOffset.value_or_default();
            if (D18Ui::Checkbox("Flip Use Offset", &resourceFlipOffset))
                config->FGResourceFlipOffset = resourceFlipOffset;
            ShowHelpMarker("Use height difference as offset");

            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader("Advanced OptiFG Settings"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};

                if (!Config::Instance()->FGDisableHUDFix.value_or_default() &&
                    state.swapchainInteropApi == SwapchainInteropApi::None)
                {
                    ImGui::Spacing();

                    auto rb = config->FGResourceBlocking.value_or_default();
                    if (D18Ui::Checkbox("Resource Blocking", &rb))
                    {
                        config->FGResourceBlocking = rb;
                        LOG_DEBUG("Enabled set FGResourceBlocking: {}", rb);
                    }
                    ShowHelpMarker("Block rarely used resources from using as HUDless \n"
                                   "to prevent flickers and other issues\n\n"
                                   "HUDfix enable/disable will reset the block list!");

                    ImGui::SameLine(0.0f, 16.0f);

                    auto rrc = config->FGRelaxedResolutionCheck.value_or_default();
                    if (D18Ui::Checkbox("Relaxed Resource Check", &rrc))
                    {
                        config->FGRelaxedResolutionCheck = rrc;
                        LOG_DEBUG("Enabled set FGRelaxedResolutionCheck: {}", rrc);
                    }
                    ShowHelpMarker("Relax resolution checks for HUDless by 32 pixels \n"
                                   "Helps games which use black borders for some \n"
                                   "resolutions and screen ratios (e.g. Witcher 3)");

                    ImGui::BeginDisabled(state.fgResetCapturedResources);
                    ImGui::PushItemWidth(95.0f * menuResScale);
                    if (D18Ui::Checkbox("FG Create List", &state.fgCaptureResources))
                    {
                        if (!state.fgCaptureResources)
                            config->FGHUDLimit = 1;
                        else
                            state.fgOnlyUseCapturedResources = false;
                    }

                    ImGui::SameLine(0.0f, 16.0f);
                    if (D18Ui::Checkbox("FG Use List", &state.fgOnlyUseCapturedResources))
                    {
                        if (state.fgCaptureResources)
                        {
                            state.fgCaptureResources = false;
                            config->FGHUDLimit = 1;
                        }
                    }

                    ImGui::SameLine(0.0f, 8.0f);
                    D18Ui::Text("(%d)", state.fgCapturedResourceCount);

                    ImGui::PopItemWidth();

                    ImGui::SameLine(0.0f, 16.0f);

                    if (D18Ui::Button("Reset List"))
                    {
                        LOG_DEBUG("Resetting captured resource list");

                        state.fgResetCapturedResources = true;
                        state.fgOnlyUseCapturedResources = false;
                    }

                    ImGui::EndDisabled();

                    ImGui::Spacing();
                    ImGui::Spacing();
                    if (D18Ui::TreeNode("Tracking Settings"))
                    {
                        auto ath = config->FGAlwaysTrackHeaps.value_or_default();
                        if (D18Ui::Checkbox("Always Track Heaps", &ath))
                        {
                            config->FGAlwaysTrackHeaps = ath;
                            LOG_DEBUG("Enabled set FGAlwaysTrackHeaps: {}", ath);
                        }
                        ShowHelpMarker("Always track resources, might cause performance issues\n, but also might "
                                       "fix HUDFix related crashes!");

                        auto disableRTV = config->FGHudfixDisableRTV.value_or_default();
                        if (D18Ui::Checkbox("Disable RTV Tracking", &disableRTV))
                            config->FGHudfixDisableRTV = disableRTV;
                        ShowHelpMarker("Disable tracking of CreateRenderTargetView\n"
                                       "This might help filtering of wrong HUDless resources");

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableSRV = config->FGHudfixDisableSRV.value_or_default();
                        if (D18Ui::Checkbox("Disable SRV Tracking", &disableSRV))
                            config->FGHudfixDisableSRV = disableSRV;
                        ShowHelpMarker("Disable tracking of CreateShaderResourceView\n"
                                       "This might help filtering of wrong HUDless resources");

                        auto disableUAV = config->FGHudfixDisableUAV.value_or_default();
                        if (D18Ui::Checkbox("Disable UAV Tracking", &disableUAV))
                            config->FGHudfixDisableUAV = disableUAV;
                        ShowHelpMarker("Disable tracking of CreateUnorderedAccessView\n"
                                       "This might help filtering of wrong HUDless resources");

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableOM = config->FGHudfixDisableOM.value_or_default();
                        if (D18Ui::Checkbox("Disable OM Tracking", &disableOM))
                            config->FGHudfixDisableOM = disableOM;
                        ShowHelpMarker("Disable tracking of OMSetRenderTargets\n"
                                       "This might help filtering of wrong HUDless resources");

                        auto disableSCR = config->FGHudfixDisableSCR.value_or_default();
                        if (D18Ui::Checkbox("Disable SCR Tracking", &disableSCR))
                            config->FGHudfixDisableSCR = disableSCR;
                        ShowHelpMarker("Disable tracking of SetComputeRootDescriptorTable\n"
                                       "This might help filtering of wrong HUDless resources");

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableSGR = config->FGHudfixDisableSGR.value_or_default();
                        if (D18Ui::Checkbox("Disable SGR Tracking", &disableSGR))
                            config->FGHudfixDisableSGR = disableSGR;
                        ShowHelpMarker("Disable tracking of SetGraphicsRootDescriptorTable\n"
                                       "This might help filtering of wrong HUDless resources");

                        ImGui::Spacing();

                        auto disableDI = config->FGHudfixDisableDI.value_or_default();
                        if (D18Ui::Checkbox("Disable DI Tracking", &disableDI))
                            config->FGHudfixDisableDI = disableDI;
                        ShowHelpMarker("Disable tracking of DrawInstanced\n"
                                       "This might help filtering of wrong HUDless resources");

                        ImGui::SameLine(0.0f, 16.0f);

                        auto disableDII = config->FGHudfixDisableDII.value_or_default();
                        if (D18Ui::Checkbox("Disable DII Tracking", &disableDII))
                            config->FGHudfixDisableDII = disableDII;
                        ShowHelpMarker("Disable tracking of DrawIndexedInstanced\n"
                                       "This might help filtering of wrong HUDless resources");

                        auto disableDispatch = config->FGHudfixDisableDispatch.value_or_default();
                        if (D18Ui::Checkbox("Disable Dispatch Tracking", &disableDispatch))
                            config->FGHudfixDisableDispatch = disableDispatch;
                        ShowHelpMarker("Disable tracking of Dispatch\n"
                                       "This might help filtering of wrong HUDless resources");

                        ImGui::TreePop();
                    }
                }

                ImGui::Spacing();
                if (D18Ui::TreeNode("Resource Settings"))
                {
                    bool makeMVCopies = config->FGMakeMVCopy.value_or_default();
                    if (D18Ui::Checkbox("FG Make MV Copies", &makeMVCopies))
                        config->FGMakeMVCopy = makeMVCopies;
                    ShowHelpMarker("Make a copy of motion vectors to use with OptiFG\n"
                                   "For preventing corruptions that might happen");

                    bool makeDepthCopies = config->FGMakeDepthCopy.value_or_default();
                    if (D18Ui::Checkbox("FG Make Depth Copies", &makeDepthCopies))
                        config->FGMakeDepthCopy = makeDepthCopies;
                    ShowHelpMarker("Make a copy of depth to use with OptiFG\n"
                                   "For preventing corruptions that might happen");

                    ImGui::PushItemWidth(115.0f * menuResScale);
                    float depthScaleMax = config->FGDepthScaleMax.value_or_default();
                    if (D18Ui::InputFloat("FG Scale Depth Max", &depthScaleMax, 10.0f, 100.0f, "%.1f"))
                        config->FGDepthScaleMax = depthScaleMax;
                    ShowHelpMarker("Depth values will be divided to this value");
                    ImGui::PopItemWidth();

                    ImGui::TreePop();
                }

                ImGui::Spacing();
                if (D18Ui::TreeNode("Syncing Settings"))
                {
                    bool useMutexForPresent = config->FGUseMutexForSwapchain.value_or_default();
                    if (D18Ui::Checkbox("FG Use Mutex for Present", &useMutexForPresent))
                        config->FGUseMutexForSwapchain = useMutexForPresent;
                    ShowHelpMarker("Use mutex to prevent desync of FG and crashes\n"
                                   "Disabling might improve the perf but decrease stability");

                    ImGui::TreePop();
                }

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
        else if (currentFeature == nullptr || currentFeature->IsFrozen())
        {
            D18Ui::Text("Upscaler is not active"); // Probably never will be visible
        }
        else if (state.activeFgOutput == FGOutput::FSRFG && !FfxApiProxy::IsFGReady())
        {
            D18Ui::TextColored(toneMapColor({ 1.0f, 0.0f, 0.0f, 1.0f }),
                               "amd_fidelityfx_dx12.dll is missing!"); // Probably never will be visible
        }
        else if (state.activeFgOutput == FGOutput::XeFG && XeFGProxy::Module() == nullptr)
        {
            D18Ui::TextColored(toneMapColor({ 1.0f, 0.0f, 0.0f, 1.0f }),
                               "libxess_fg.dll is missing!"); // Probably never will be visible
        }
    }

    const FGNvngxReplacement activeNvngxFg = state.activeFgNvngx;
    if (activeNvngxFg != FGNvngxReplacement::None)
    {
        if (activeNvngxFg == FGNvngxReplacement::Nukems)
        {
            SeparatorWithHelpMarker("Frame Generation (FSR3-FG via Nukem's DLSSG)",
                                    "Requires Nukem's dlssg_to_fsr3 dll");

            if (!state.nukemsFgFileAvailable)
            {
                D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                                   "Please put dlssg_to_fsr3_amd_is_better.dll into OptiScaler folder");
            }
        }
        else if (activeNvngxFg == FGNvngxReplacement::Arturs)
        {
            SeparatorWithHelpMarker("Frame Generation (FSR3-MFG via DLSS Enabler)",
                                    "DLSS Enabler as dlss-enabler-headless.dll");

            if (!state.artursFgFileAvailable)
            {
                D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                                   "Please put dlss-enabler-headless.dll into OptiScaler folder");
            }

            D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                               "Using a subset of features from DLSS Enabler");
        }
        else if (activeNvngxFg == FGNvngxReplacement::FFX)
        {
            SeparatorWithHelpMarker("Frame Generation (FSRFG via FFX)", "FFX using the DLSSG swapchain");
        }
        else if (activeNvngxFg == FGNvngxReplacement::Combo)
        {
            SeparatorWithHelpMarker("Frame Generation (Enabler + FFX)",
                                    "FFX for middle fake frames, and Enabler for the rest\n\n2x - FFX\n"
                                    "3x - Enabler\n4x - FFX + Enabler\n5x - Enabler\n6x - FFX + Enabler");
        }

        if (state.activeFgInput == FGInput::NvngxFG)
        {

            bool dmfgActive = state.dlssgGameDMFGSupported && config->FGDLSSGOverrideForceDMFG.value_or_default();

            if (!ReflexHooks::isReflexHooked())
            {
                D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "Reflex not hooked");
                D18Ui::Text("If you are using an AMD/Intel GPU, then make sure you have Fakenvapi");
            }
            else if (ReflexHooks::dlssgFrameCountToGenerate() == 0 && !dmfgActive)
            {
                D18Ui::Text("Please select DLSS Frame Generation in the game options\n"
                            "You might need to select DLSS first");
            }

            if (state.swapchainApi == DX12)
            {
                D18Ui::Text("Current DLSSG state:");
                ImGui::SameLine();
                if (auto count = state.dlssgDetectedInterpolationCount; count > 0)
                {
                    D18Ui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)),
                                       std::format("ON {}x", count + 1).c_str());
                }
                else
                {
                    D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "OFF");
                }

                // Issue mostly shows up on AMD on Windows on pre-RDNA3 in some non-UE games
                // Hide to reduce confusion, config is still read
                const bool isUnrealEngine = State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
                                            State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine;
                const bool isDllProxyNvngxType =
                    activeNvngxFg == FGNvngxReplacement::Nukems || activeNvngxFg == FGNvngxReplacement::Arturs;
                if (isDllProxyNvngxType && !primaryGpu.dlssCapable && primaryGpu.fsr4Support == FSR4Support::None &&
                    !primaryGpu.usesVkd3dProton && !isUnrealEngine)
                {
                    if (bool makeDepthCopy = config->NvngxFGMakeDepthCopy.value_or_default();
                        D18Ui::Checkbox("Fix broken visuals", &makeDepthCopy))
                    {
                        config->NvngxFGMakeDepthCopy = makeDepthCopy;
                    }
                    ShowHelpMarker("Makes a copy of the depth buffer\nCan fix broken visuals in some games on AMD "
                                   "GPUs under Windows\nCan cause stutters, so best to use only when necessary");
                }
            }
            else if (state.swapchainApi == Vulkan)
            {
                D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)),
                                   "DLSSG is purposefully disabled when this menu is visible");
                ImGui::Spacing();
            }
        }

        bool isLoaded = false;
        if (state.swapchainApi == Vulkan)
            isLoaded = Nvngx_FG::isVulkanAvailable();
        if (state.swapchainApi == DX12)
            isLoaded = Nvngx_FG::isDx12Available();

        if (isLoaded)
        {
            if (activeNvngxFg == FGNvngxReplacement::Arturs || activeNvngxFg == FGNvngxReplacement::Combo)
            {
                auto featureVer = Nvngx_FG::version();
                auto antighostingVer = Nvngx_FG::extraVersion();
                D18Ui::Text("DE Ver: %d.%d.%d.%d   GB Ver: %d.%d", featureVer.major, featureVer.minor, featureVer.patch,
                            featureVer.reserved, antighostingVer.major, antighostingVer.minor);

                static std::vector<FlagDefinition> common_flags = {
                    { "Antighosting (GB)", 0x00100000, "Enable anti-ghosting correction" },
                    { "Temporal HUD pin", 0x04000000, "Enable temporal HUD pinning (present-backbuffer stability)" }
                };

                static std::vector<FlagDefinition> uncommon_flags = {
                    //{ "Hudless UI mask", 0x02000000, "Use HUD-less as UI mask (DL2 inverted semantics)" },
                    { "HUD interpolation", 0x08000000, "HUD OF interpolation (0=legacy pin-present, 1=OF warp)" },
                    { "Ignore UI texture", 0x10000000, "Ignore dedicated DLSSG.UI texture (force legacy HUD path)" },
                    //{ "Dp4a active", 0x20000000, "OF pipeline using dp4a-accelerated SSD (SM 6.4+)" },
                    { "Pin backbuffer", 0x40000000, "Pin DLSSG.Backbuffer to subframe-1 snapshot across MFG frame" }
                };

                static std::vector<FlagDefinition> debug_flags = {
                    { "Antighosting red tint", 0x00200000, "Debug: red tint on corrected pixels" },
                    { "Antighosting split screen", 0x00400000, "Debug: split screen comparison" },
                    { "Frame index line", 0x00010000, "" },
                    { "HUD detection", 0x00020000, "" },
                    { "Disocclusion tint", 0x00040000, "" },
                    { "Artifacts detection", 0x00080000, "" },
                    { "Camera MV debug", 0x00800000, "Debug: blue tint where camera MV fallback is used" },
                    { "Generic visualization", 0x01000000, "Debug: trapezoid zone visualization" }
                };

                uint32_t temp_flags = config->NvngxFGDispatchFlags.value_or_default();
                bool changed = false;

                D18Ui::Text("Raw DispatchFlags:");
                changed |= ImGui::InputScalar("##RawFlags", ImGuiDataType_U32, &temp_flags, NULL, NULL, "%08X",
                                              ImGuiInputTextFlags_CharsHexadecimal);

                ImGui::SameLine(0.0f, 20.0f * menuResScale);
                if (bool showDebug = config->NvngxFGShowDebug.value_or_default();
                    D18Ui::Checkbox("Show Debug", &showDebug))
                {
                    config->NvngxFGShowDebug = showDebug;
                }
                ShowHelpMarker("Required for Debug flags to work correctly");

                ImGui::Spacing();

                if (auto ch = ScopedCollapsingHeader("Active DispatchFlags"); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};

                    auto render_flags = [&](const std::vector<FlagDefinition>& flags)
                    {
                        for (const auto& flag : flags)
                        {
                            changed |= ImGui::CheckboxFlags(flag.name.c_str(), &temp_flags, flag.mask);

                            if (ImGui::IsItemHovered() && !flag.description.empty())
                            {
                                ImGui::SetTooltip("%s", flag.description.c_str());
                            }
                        }
                    };

                    D18Ui::TextDisabled("Common");
                    render_flags(common_flags);

                    ImGui::Spacing();
                    D18Ui::TextDisabled("Uncommon");
                    render_flags(uncommon_flags);

                    if (config->NvngxFGShowDebug.value_or_default())
                    {
                        ImGui::Spacing();
                        D18Ui::TextDisabled("Debug");
                        render_flags(debug_flags);
                    }
                }

                if (changed)
                {
                    config->NvngxFGDispatchFlags = temp_flags;
                }
            }

            if (activeNvngxFg == FGNvngxReplacement::Nukems)
            {
                if (D18Ui::Checkbox("Enable Debug View", &state.dlssgDebugView))
                {
                    Nvngx_FG::setDebugView(state.dlssgDebugView);
                }
                if (D18Ui::Checkbox("Interpolated frames only", &state.dlssgInterpolatedOnly))
                {
                    Nvngx_FG::setInterpolatedOnly(state.dlssgInterpolatedOnly);
                }
            }

            if (activeNvngxFg == FGNvngxReplacement::FFX || activeNvngxFg == FGNvngxReplacement::Combo)
            {
                if (_ffxFGIndex < 0)
                    _ffxFGIndex = config->FfxFGIndex.value_or_default();

                if (state.ffxFGVersionNames.size() > 0)
                {
                    ImGui::PushItemWidth(135.0f * menuResScale);

                    auto currentName = D18Ui::Format("FSR %s", state.ffxFGVersionNames[_ffxFGIndex]);
                    if (D18Ui::BeginCombo("FFX FG", currentName.c_str()))
                    {
                        for (int n = 0; n < state.ffxFGVersionIds.size(); n++)
                        {
                            auto name = D18Ui::Format("FSR %s", state.ffxFGVersionNames[n]);
                            if (D18Ui::Selectable(name.c_str(), config->FfxFGIndex.value_or_default() == n))
                                _ffxFGIndex = n;
                        }

                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    ShowHelpMarker("List of FGs reported by FFX SDK");

                    ImGui::SameLine(0.0f, 6.0f);

                    if (D18Ui::Button("Change FG") && _ffxFGIndex != config->FfxFGIndex.value_or_default())
                    {
                        config->FfxFGIndex = _ffxFGIndex;
                        state.fgChanged = true;
                    }
                }

                bool fgAsync = config->FGAsync.value_or_default();
                if (D18Ui::Checkbox("Allow Async##2", &fgAsync))
                {
                    config->FGAsync = fgAsync;

                    if (config->FGEnabled.value_or_default())
                    {
                        state.fgChanged = true;
                        LOG_DEBUG("Async set FGChanged");
                    }
                }
                ShowHelpMarker("Enable Async for better FG performance\nMight cause crashes, especially with HUD Fix!");

                ImGui::SameLine(0.0f, 20.0f * menuResScale);
                bool fgDV = config->FGDebugView.value_or_default();
                if (D18Ui::Checkbox("Debug View##3", &fgDV))
                {
                    config->FGDebugView = fgDV;

                    if (config->FGEnabled.value_or_default())
                    {
                        state.fgChanged = true;
                        LOG_DEBUG("DebugView set FGChanged");
                    }
                }
                ShowHelpMarker("Enable FSR3.1-FG Debug view\n\n"
                               "Top left: Game Motion Vectors\n"
                               "Top middle: GMV Depth\n"
                               "Top right: Optical Flow MV\n"
                               "Middle: Interpolated frame only\n"
                               "Bottom left: Disocclusion mask\n"
                               "Bottom middle: Interpolation source (w/o UI)\n"
                               "Bottom right: HUDless resource");

                if (Nvngx_FG::version().major > 3)
                {
                    ImGui::SameLine(0.0f, 20.0f * menuResScale);
                    if (bool fgwm = config->FSRFGEnableWatermark.value_or_default();
                        D18Ui::Checkbox("Enable Watermark", &fgwm))
                    {
                        LOG_DEBUG("FSRFGEnableWatermark set FGWatermark: {}", fgwm);
                        config->FSRFGEnableWatermark = fgwm;
                    }

                    ShowHelpMarker("After changing this option, please Save Settings\n"
                                   "It will be applied on next launch.");
                }
            }

            if (bool disableHudless = config->NvngxFGDisableHudless.value_or_default();
                D18Ui::Checkbox("Disable HUDless", &disableHudless))
            {
                config->NvngxFGDisableHudless = disableHudless;
            }
            ShowHelpMarker("Might be required for some sets of DispatchFlags");
        }
    }

    // FSR-FG Inputs
    if (state.currentFGSwapchain != nullptr &&
        (state.activeFgInput == FGInput::FSRFG || state.activeFgInput == FGInput::FSRFG30))
    {
        SeparatorWithHelpMarker("Frame Generation (FSR-FG Inputs)", "Select FSR-FG in-game");

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
        if (fgOutput != nullptr)
        {
            D18Ui::Text("Current FSR-FG state:");
            ImGui::SameLine();
            if (state.fsrfgInputActive)
            {
                if (fgOutput->IsActive())
                    D18Ui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)), "ON");
                else
                    D18Ui::TextColored(toneMapColor(ImVec4(1.0f, 0.647f, 0.0f, 1.f)), "ACTIVATE FG");
            }
            else
            {
                D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "OFF");
                D18Ui::Text("Please select FSR Frame Generation in the game options\n"
                            "You might need to select FSR first");
            }
        }

        bool skipConfig = config->FSRFGSkipConfigForHudless.value_or_default();
        if (D18Ui::Checkbox("Skip Config for HUDless", &skipConfig))
            config->FSRFGSkipConfigForHudless = skipConfig;

        ShowHelpMarker("Do not use HUDless set at ffxConfig");

        ImGui::SameLine(0.0f, 6.0f);

        bool skipDispatch = config->FSRFGSkipDispatchForHudless.value_or_default();
        if (D18Ui::Checkbox("Skip Dispatch for HUDless", &skipDispatch))
            config->FSRFGSkipDispatchForHudless = skipDispatch;

        ShowHelpMarker("Do not use HUDless set at ffxDispatch");
    }

    // Streamline FG Inputs
    if (state.currentFGSwapchain != nullptr && state.activeFgInput == FGInput::DLSSG)
    {
        SeparatorWithHelpMarker("Frame Generation (Streamline FG Inputs)", "Select DLSS-FG in-game");

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);

        if (!ReflexHooks::isReflexHooked())
        {
            D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "Reflex not hooked");
            D18Ui::Text("If you are using an AMD/Intel GPU, then make sure you have fakenvapi");
        }
        else if (fgOutput != nullptr)
        {
            D18Ui::Text("Current Streamline FG state:");
            ImGui::SameLine();
            if ((state.fgLastFrame - state.dlssgLastFrame) < 3)
            {
                if (fgOutput->IsActive())
                    D18Ui::TextColored(toneMapColor(ImVec4(0.f, 1.f, 0.25f, 1.f)), "ON");
                else
                    D18Ui::TextColored(toneMapColor(ImVec4(1.0f, 0.647f, 0.0f, 1.f)), "ACTIVATE FG");
            }
            else
            {
                D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)), "OFF");
                D18Ui::Text("Please select DLSS Frame Generation in the game options\n"
                            "You might need to select DLSS first");
            }
        }
    }
}




template <typename T> std::string GetMenuOptionLabel(const std::vector<MenuOption<T>>& options, T targetValue)
{
    auto it = std::find_if(options.begin(), options.end(),
                           [targetValue](const MenuOption<T>& option) { return option.value == targetValue; });

    if (it != options.end())
    {
        return it->label;
    }

    return "Unknown";
}












void MenuCommon::RenderD18DlssSrSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto* config = ctx.config;
    auto* feature = ctx.currentFeature;
    if(config->NgxOnlyMode.value_or_default() && DlssNr::ReProfile::Known(state.gameExe.c_str())) {
        const auto observed=DlssNr::NativeSr::Read();
        D18Ui::SeparatorText(observed.rayReconstruction?"DLSS RR (native passthrough)":"DLSS SR (native passthrough)");
        D18Ui::Text("%s | successful frames %llu",observed.Live(GetTickCount64())?"Active":"Waiting",observed.successfulFrames);
        D18Ui::TextWrapped("DLSS is owned by the game. Use game settings; OptiScaler preset/apply controls do not apply to this path.");
        D18Ui::TextWrapped("DLSS SR runtime is not bundled. Supply nvngx_dlss.dll beside the game executable if needed; keep the game's existing DLSS files.");
        return;
    }

    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("DLSS SR", ImGuiTreeNodeFlags_DefaultOpen); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        bool enabled = config->DLSSEnabled.value_or_default();
        if (D18Ui::Checkbox("Use native NVIDIA DLSS SR", &enabled))
            config->DLSSEnabled = enabled;
        ShowHelpMarker("This controls OptiScaler's native NVIDIA DLSS provider; it is not read-only. The game still supplies the quality mode, resolutions and frame inputs unless an override below is enabled.");
        D18Ui::TextWrapped("DLSS SR runtime is not bundled. Supply nvngx_dlss.dll beside the game executable if needed; keep the game's existing DLSS files.");

        if (feature != nullptr && feature->IsInited() && !feature->IsFrozen())
        {
            if (feature->GetUpscalerType() == Upscaler::DLSS)
            {
                D18Ui::Text("%s %u.%u.%u", feature->ShortName().c_str(), feature->Version().major,
                            feature->Version().minor, feature->Version().patch);
                D18Ui::TextDisabled("%ux%u -> %ux%u | frame %ld", feature->RenderWidth(), feature->RenderHeight(),
                                    feature->TargetWidth(), feature->TargetHeight(), feature->FrameCount());
            }
            else if (feature->GetUpscalerType() == Upscaler::DLSSD)
            {
                D18Ui::TextColored(D18HealthColor(D18Health::Waiting), "Ray Reconstruction is active; SR preset control is idle.");
            }
            else
            {
                D18Ui::TextColored(D18HealthColor(D18Health::Error), "Active backend is %s, not DLSS.",
                                   feature->ShortName().c_str());
            }
        }
        else
        {
            D18Ui::TextDisabled("Waiting for DLSS SR to run.");
        }

        bool overridePreset = config->RenderPresetOverride.value_or_default();
        if (D18Ui::Checkbox("Override render preset", &overridePreset))
            config->RenderPresetOverride = overridePreset;

        ImGui::BeginDisabled(!overridePreset);
        ImGui::PushItemWidth(150.0f * ctx.menuResScale);
        AddDLSSRenderPreset("Preset", &config->RenderPresetForAll);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        D18Ui::TextWrapped("SR settings require an existing game input integration; applying settings does not create one.");
        if (D18Ui::Button("Apply SR"))
        {
            LOG_INFO("D18 UI applying DLSS SR preset {}", config->RenderPresetForAll.value_or_default());
            state.newBackend = Upscaler::DLSS;
            MARK_ALL_BACKENDS_CHANGED();
        }
        ImGui::EndDisabled();

        if (state.dlssPresetsOverriddenExternally)
            D18Ui::TextColored(D18HealthColor(D18Health::Waiting), "An NVIDIA App / Inspector preset override was detected.");
    }
}

void MenuCommon::RenderD18DlssFgSettings(RenderMenuContext& ctx)
{
    if(ctx.state.api==API::DX11){
        RenderFrameGenerationSelection(ctx);
        if(D18Ui::TreeNode("FG runtime settings")){
            RenderFrameGenerationRuntimeSettings(ctx);
            ImGui::TreePop();
        }
    }
    auto& state = ctx.state;
    auto* config = ctx.config;
    auto* fg = state.currentFG;
    const bool nativeUnobserved = config->NgxOnlyMode.value_or_default() &&
        config->SkipStreamlineHooks.value_or_default() &&
        DlssNr::ReProfile::Known(state.gameExe.c_str());

    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("DLSS FG", ImGuiTreeNodeFlags_DefaultOpen); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        const bool dlssPath = state.activeFgInput == FGInput::DLSSG || state.activeFgInput == FGInput::NvngxFG ||
                              state.activeFgOutput == FGOutput::DLSSG || state.dlssgDetectedInterpolationCount > 0;
        D18Ui::TextWrapped("OptiScaler DLSS FG requires user-supplied runtime files in the game's streamline folder. Game-native FG uses the game's own files.");
        if (D18Ui::TreeNode("Required DLSS FG files and location"))
        {
            D18Ui::TextWrapped("Beside the game executable, create a streamline folder containing these six files from one matching package:");
            D18Ui::TextUnformatted("sl.interposer.dll\nsl.common.dll\nsl.dlss_g.dll\nsl.reflex.dll\nsl.pcl.dll\nnvngx_dlssg.dll");
            D18Ui::TextWrapped("Restart after adding files. Files alone do not enable FG: select a supported FG route and enable it. Preserve the game's existing root DLLs; these files are for the DLSS FG output, not every FG provider.");
            ImGui::TreePop();
        }
        const bool ownedVulkan=state.api==API::Vulkan && StreamlineProxy::IsVulkanInited();
        const bool optiRoute=state.activeFgInput==FGInput::Upscaler;
        const bool active = (dlssPath || optiRoute) && ((fg != nullptr && fg->IsActive() && !fg->IsPaused()) ||
                                         state.dlssgDetectedInterpolationCount > 0);

        const auto nativeFg=DlssNr::NativeFg::Read();
        if(ownedVulkan)
        {
            const auto presented=VulkanFg::Frame::actual.load();
            const auto fgStatus=VulkanFg::Frame::status.load();
            // Keep the last completed result visible across per-frame recording
            // stages. Off/failure clears actual, so real gates remain visible.
            const auto fgReason=VulkanFg::Frame::Enabled() && presented>1 && fgStatus==0
                ? "Generated frames observed" : VulkanFg::Frame::reason.load();
            D18Ui::TextWrapped("Experimental Vulkan FG: %s",fgReason);
            D18Ui::TextDisabled("Runtime presented: %u | status: %u",presented,fgStatus);
            D18Ui::TextWrapped("Camera projection uses configured approximations when the game supplies no camera data. Test motion and UI quality.");
        }
        else if(state.api==API::Vulkan && nativeFg.tick && !optiRoute)
            D18Ui::TextWrapped("%s",!nativeFg.Fresh()?"Native FG status is stale":!nativeFg.ok?"Native FG runtime reported an error":
                nativeFg.menuPaused && MenuOverlayBase::IsVisible()?"FG paused while this menu is open; close menu to resume":
                nativeFg.presented>1?D18Ui::Format("Game-native Frame Generation active (%ux)",nativeFg.presented).c_str():"Native FG: no generated frames reported");
        else if (nativeUnobserved)
            D18Ui::TextDisabled("Game-controlled FG: live on/off state is not observed. Use game settings.");
        else if (active && optiRoute && state.activeFgOutput == FGOutput::DLSSG)
            D18Ui::TextColored(D18HealthColor(D18Health::Active),
                               "FG active | requested %ux; actual multiplier unobserved",
                               config->FGDLSSGInterpolationCount.value_or_default() + 1);
        else if (active && state.dlssgDetectedInterpolationCount > 0)
            D18Ui::TextColored(D18HealthColor(D18Health::Active), "Frame Generation is active (%dx)",
                               state.dlssgDetectedInterpolationCount + 1);
        else if (active)
            D18Ui::TextColored(D18HealthColor(D18Health::Active), "FG active; actual multiplier unobserved");
        else if (optiRoute)
            D18Ui::TextDisabled("OptiFG selected; waiting for FG context and upscaler inputs.");
        else if (dlssPath)
            D18Ui::TextColored(D18HealthColor(D18Health::Waiting), "DLSSG detected but not producing generated frames.");
        else
            D18Ui::TextDisabled("No active FG route. Select a supported input/output or use game-native FG.");

        D18Ui::TextDisabled("%s -> %s | Reflex %s", D18FgInputName(state.activeFgInput),
                            D18FgOutputName(state.activeFgOutput),
                            ReflexHooks::isReflexHooked() ? "hooked" : "not hooked");

        const bool dx11DlssgRoute = state.api == API::DX11 && optiRoute &&
                                   state.activeFgOutput == FGOutput::DLSSG;
        // The next-startup enable control must not depend on a live FG context.
        // Input/output combos already validate API/hardware availability.
        const bool selectedDx11DlssgRoute = state.api == API::DX11 &&
            config->FGInput.value_or_default() == FGInput::Upscaler &&
            config->FGOutput.value_or_default() == FGOutput::DLSSG;
        const bool configureNextStartup = selectedDx11DlssgRoute &&
            (state.fgSettingsChanged || !dx11DlssgRoute ||
             state.swapchainInteropApi != SwapchainInteropApi::Dx11wDx12);
        if (dx11DlssgRoute && state.swapchainInteropApi != SwapchainInteropApi::Dx11wDx12)
            D18Ui::TextDisabled(config->FGEnabled.value_or_default()
                                    ? "FG enabled for next startup. Save settings and restart the game."
                                    : "FG off: using the native DX11 presentation path.");
        else if (dx11DlssgRoute && !config->FGEnabled.value_or_default())
            D18Ui::TextDisabled("FG off. Save settings and restart to release the FG presentation path.");

        if (configureNextStartup)
            D18Ui::TextWrapped("Selected for next startup: OptiFG -> DLSSG. Set the enable switch below before saving; no intermediate restart is needed.");
        const bool fgControlUnavailable = !ownedVulkan && (nativeUnobserved || (!selectedDx11DlssgRoute &&
            ((fg == nullptr && !dx11DlssgRoute) || (!dlssPath && !optiRoute))));
        if (fgControlUnavailable)
            D18Ui::TextWrapped("%s", nativeUnobserved || (state.api==API::Vulkan && !optiRoute)
                ? "Game-native FG: enable/disable and multiplier are controlled in game settings. This switch only controls an OptiScaler FG route."
                : "OptiScaler FG control requires a supported route and an initialized FG context.");
        ImGui::BeginDisabled(fgControlUnavailable);
        bool fgEnabled = config->FGEnabled.value_or_default();
        if (D18Ui::Checkbox("Enable OptiScaler FG route", &fgEnabled))
        {
            config->FGEnabled = fgEnabled;
            LOG_INFO("D18 UI set FG enabled to {}", fgEnabled);
            if (fgEnabled && !configureNextStartup && !ownedVulkan)
                state.fgChanged = true;
        }

        if(ownedVulkan)
        {
            const char* modes[]={"2X","3X","4X","5X","6X"};
            const unsigned queried=VulkanFg::Frame::maximum.load();
            const int count=queried?int(queried):5;
            int selected=std::clamp(int(config->FGDLSSGInterpolationCount.value_or_default())-1,0,count-1);
            if(D18Ui::Combo("MFG##vulkan_owned",&selected,modes,count))config->FGDLSSGInterpolationCount=selected+1;
        }
        else if (fg != nullptr && fg->GetMaxInterpolationCount() > 1)
        {
            const int maxCount = std::min(fg->GetMaxInterpolationCount(), 5);
            const char* modes[] = { "2X", "3X", "4X", "5X", "6X" };
            int interpolation = std::clamp(static_cast<int>(fg->GetInterpolatedFrameCount()), 1, maxCount);
            int selected = interpolation - 1;
            ImGui::PushItemWidth(95.0f * ctx.menuResScale);
            if (D18Ui::Combo("MFG", &selected, modes, maxCount))
                config->FGDLSSGInterpolationCount = selected + 1;
            ImGui::PopItemWidth();
        }
        ImGui::EndDisabled();

        if (selectedDx11DlssgRoute)
        {
            D18Ui::TextWrapped("Choose input, output and enabled state together. Save all settings below, then restart once if requested.");
            if (D18Ui::Button("Save Settings##d18_fg_setup"))
                config->SaveIni();
        }

        const bool parameterOverlaySupported = state.activeFgNvngx == FGNvngxReplacement::Arturs ||
                                               state.activeFgNvngx == FGNvngxReplacement::Combo;
        if (parameterOverlaySupported)
        {
            bool showDebug = config->NvngxFGShowDebug.value_or_default();
            if (D18Ui::Checkbox("Provider debug overlay", &showDebug))
                config->NvngxFGShowDebug = showDebug;
            ShowHelpMarker("Passed to the active DLSSG replacement provider on every dispatch.");
        }
        else if (state.activeFgNvngx == FGNvngxReplacement::Nukems)
        {
            if (D18Ui::Checkbox("Nukem debug view", &state.dlssgDebugView))
                Nvngx_FG::setDebugView(state.dlssgDebugView);
        }
        else
        {
            D18Ui::TextDisabled("Debug overlay unavailable for the game-native DLSSG route.");
            ShowHelpMarker("The ShowDebug parameter is implemented by compatible replacement providers, not by this game's native DLSSG path.");
        }
    }
}

void MenuCommon::RenderD18SharpnessSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto* config = ctx.config;
    auto* feature = ctx.currentFeature;

    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("Sharpness##d18_sharpness_panel", ImGuiTreeNodeFlags_DefaultOpen);
        ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        bool overrideSharpness = config->OverrideSharpness.value_or_default();
        if (D18Ui::Checkbox("Override game sharpness##d18_override", &overrideSharpness))
        {
            config->OverrideSharpness = overrideSharpness;

            if (feature != nullptr && feature->GetUpscalerType() == Upscaler::DLSS && feature->Version().major < 3)
            {
                state.newBackend = Upscaler::DLSS;
                MARK_ALL_BACKENDS_CHANGED();
            }
        }
        ShowHelpMarker("Ignore the sharpness value sent by the game and use the value below.");

        ImGui::BeginDisabled(!overrideSharpness);
        float sharpness = config->Sharpness.value_or_default();
        ImGui::PushItemWidth(220.0f * ctx.menuResScale);
        if (D18Ui::SliderFloat("Sharpness##d18_value", &sharpness, 0.0f, 1.0f, "%.3f"))
            config->Sharpness = sharpness;
        ImGui::PopItemWidth();
        ImGui::EndDisabled();

        if (feature != nullptr && feature->IsInited())
            D18Ui::TextDisabled("Current game / feature value: %.3f", feature->Sharpness());

        constexpr feature_version requiredDlssVersion = { 2, 5, 1 };
        const bool rcasDefault = feature != nullptr &&
                                 (feature->GetUpscalerType() == Upscaler::XeSS ||
                                  (feature->GetUpscalerType() == Upscaler::DLSS &&
                                   feature->Version() >= requiredDlssVersion));
        bool rcasEnabled = config->RcasEnabled.value_or(rcasDefault);
        if (D18Ui::Checkbox("Enable OptiScaler sharpening (RCAS/DA)##d18_rcas", &rcasEnabled))
            config->RcasEnabled = rcasEnabled;
        ShowHelpMarker("Runs OptiScaler's post-upscale sharpener. Override game sharpness above to set its strength manually.");

        ImGui::BeginDisabled(!rcasEnabled);
        D18Ui::SeparatorText("Sharpening method");

        int sharpnessShader = static_cast<int>(config->SharpnessShader.value_or_default());
        if (D18Ui::RadioButton("RCAS##d18_method_rcas", &sharpnessShader, static_cast<int>(SharpenShader::RCAS)))
            config->SharpnessShader = SharpenShader::RCAS;
        ShowHelpMarker("AMD RCAS with OptiScaler contrast and motion-adaptive extensions.");

        if (D18Ui::RadioButton("Depth Aware (RCAS)##d18_method_da_rcas", &sharpnessShader,
                               static_cast<int>(SharpenShader::DepthAware)))
            config->SharpnessShader = SharpenShader::DepthAware;
        ShowHelpMarker("Depth-aware RCAS: reduces cross-object halos and sharpens distant detail more strongly.");

        if (D18Ui::RadioButton("Depth Aware (DAS)##d18_method_das", &sharpnessShader,
                               static_cast<int>(SharpenShader::LocalContrastDepthAware)))
            config->SharpnessShader = SharpenShader::LocalContrastDepthAware;
        ShowHelpMarker("Depth-aware directional adaptive luma sharpening. Heavier, with stronger local-contrast control.");

        const auto selectedShader = config->SharpnessShader.value_or_default();
        ImGui::Spacing();

        bool motionEnabled = config->MotionSharpnessEnabled.value_or_default();
        if (D18Ui::Checkbox("Enable Motion Adaptive Sharpness##d18_motion_enable", &motionEnabled))
            config->MotionSharpnessEnabled = motionEnabled;
        ShowHelpMarker("Adjusts sharpening according to motion. Negative motion sharpness reduces shimmer while moving.");

        if (selectedShader == SharpenShader::RCAS)
        {
            bool contrastEnabled = config->ContrastEnabled.value_or_default();
            if (D18Ui::Checkbox("Contrast control##d18_contrast_enable", &contrastEnabled))
                config->ContrastEnabled = contrastEnabled;

            ImGui::BeginDisabled(!contrastEnabled);
            float contrast = config->Contrast.value_or_default();
            if (D18Ui::SliderFloat("Contrast##d18_contrast", &contrast, -2.0f, 2.0f, "%.2f"))
                config->Contrast = contrast;
            ImGui::EndDisabled();
        }
        else
        {
            bool daDebug = config->MotionSharpnessDebug.value_or_default();
            if (D18Ui::Checkbox("DA + MAS debug view##d18_da_debug", &daDebug))
                config->MotionSharpnessDebug = daDebug;

            if (auto advanced = ScopedCollapsingHeader("Advanced DA parameters##d18_da_advanced");
                advanced.IsHeaderOpen())
            {
                ScopedIndent advancedIndent {};
                bool clamp = config->DAClampOutput.value_or(false);
                if (D18Ui::Checkbox("Clamp output##d18_da_clamp", &clamp))
                {
                    if (clamp)
                        config->DAClampOutput = true;
                    else
                        config->DAClampOutput.reset();
                }

                const bool linearDepth = feature != nullptr && feature->DepthLinear();
                float depthBias = config->DADepthBias.value_or(linearDepth ? 0.0015f : 0.001f);
                const float biasMin = linearDepth ? 0.005f : 0.0001f;
                const float biasMax = linearDepth ? 0.03f : 0.003f;
                if (D18Ui::SliderFloat("Depth bias##d18_da_bias", &depthBias, biasMin, biasMax, "%.4f"))
                    config->DADepthBias = depthBias;

                float depthScale = config->DADepthScale.value_or(linearDepth ? 250.0f : 35.0f);
                const float scaleMin = linearDepth ? 100.0f : 25.0f;
                const float scaleMax = linearDepth ? 600.0f : 400.0f;
                if (D18Ui::SliderFloat("Depth scale##d18_da_scale", &depthScale, scaleMin, scaleMax, "%.1f"))
                    config->DADepthScale = depthScale;

                if (D18Ui::Button("Reset depth values##d18_da_reset"))
                {
                    config->DADepthBias.reset();
                    config->DADepthScale.reset();
                }
            }
        }

        if (auto motion = ScopedCollapsingHeader("Motion Adaptive Sharpness##d18_motion_panel");
            motion.IsHeaderOpen())
        {
            ScopedIndent motionIndent {};
            ImGui::BeginDisabled(!motionEnabled);

            if (selectedShader == SharpenShader::RCAS)
            {
                bool masDebug = config->MotionSharpnessDebug.value_or_default();
                if (D18Ui::Checkbox("MAS debug view##d18_mas_debug", &masDebug))
                    config->MotionSharpnessDebug = masDebug;
            }

            float motionSharpness = config->MotionSharpness.value_or_default();
            if (D18Ui::SliderFloat("Motion sharpness##d18_motion_strength", &motionSharpness, -1.0f, 1.0f, "%.3f"))
                config->MotionSharpness = motionSharpness;
            ShowHelpMarker("Maximum sharpness added or removed by motion. Negative values reduce sharpening in motion.");

            float motionThreshold = config->MotionThreshold.value_or_default();
            if (D18Ui::SliderFloat("Motion threshold##d18_motion_threshold", &motionThreshold, 0.0f, 100.0f,
                                   "%.2f"))
                config->MotionThreshold = motionThreshold;

            float motionRange = config->MotionScaleLimit.value_or_default();
            if (D18Ui::SliderFloat("Motion range##d18_motion_range", &motionRange, 0.01f, 100.0f, "%.2f"))
                config->MotionScaleLimit = motionRange;

            ImGui::EndDisabled();
        }

        ImGui::EndDisabled();
    }
}

void MenuCommon::RenderMainMenuTable(RenderMenuContext& ctx)
{
    if (ImGui::BeginTable("main", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_Resizable))
    {
        ImGui::TableNextColumn();

        RenderD18DlssSrSettings(ctx);
        RenderD18DlssFgSettings(ctx);
        RenderD18SharpnessSettings(ctx);

        if (auto hotkeys = ScopedCollapsingHeader("Hotkeys", ImGuiTreeNodeFlags_DefaultOpen);
            hotkeys.IsHeaderOpen())
        {
            ScopedIndent indent {};
            D18Ui::TextWrapped("Click an action, then press a key. Escape cancels; Backspace unbinds; R restores the default.");
            D18Ui::TextWrapped("Single keys only. Changes apply immediately; use Save Settings to keep them.");
            static auto menuHotkey = Keybind("UI hotkey", 110);
            static auto nrHotkey = Keybind("NR hotkey", 111);
            menuHotkey.Render(ctx.config->ShortcutKey);
            nrHotkey.Render(ctx.config->DlssNrToggleKey);
            const int uiKey = ctx.config->ShortcutKey.value_or_default();
            const int nrKey = ctx.config->DlssNrToggleKey.value_or_default();
            if (uiKey == UnboundKey)
                D18Ui::TextWrapped("UI hotkey is unbound. Restore it before closing this menu.");
            if (uiKey > 0 && uiKey == nrKey)
                D18Ui::TextWrapped("UI and NR share a key: both actions will trigger. Choose different keys.");
        }

        ImGui::TableNextColumn();

        DlssNr::RenderD18Menu(ctx.config, ctx.menuResScale);

        ImGui::EndTable();
    }
}


void MenuCommon::RenderMainMenuBottomBar(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& currentFeature = ctx.currentFeature;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (currentFeature != nullptr && currentFeature->IsInited())
    {
        D18Ui::TextDisabled("SR frame %ld", currentFeature->FrameCount());
        ImGui::SameLine(0.0f, 12.0f);
    }

    const auto nr = DlssNr::GetRuntimeStatus();
    D18Ui::TextDisabled("NR %llu/%llu", nr.successfulFrames, nr.attemptedFrames);

    const ImVec2 currentWindowSize = ImGui::GetWindowSize();
    D18Ui::TextDisabled("Window %.0f x %.0f - drag the lower-right corner to resize", currentWindowSize.x,
                        currentWindowSize.y);

    float uiScale = ctx.menuResScale;
    ImGui::PushItemWidth(120.0f * ctx.menuResScale);
    if (D18Ui::SliderFloat("UI scale", &uiScale, 0.5f, 2.0f, "%.1fx"))
        config->MenuScale = std::round(uiScale * 10.0f) / 10.0f;
    ImGui::PopItemWidth();

    ImGui::SameLine(0.0f, 6.0f);
    if (D18Ui::Button("Auto scale"))
        config->MenuScale.reset();

    ImGui::SameLine(0.0f, 16.0f);

    if (D18Ui::Button("Save Settings"))
        config->SaveIni();

    ImGui::SameLine(0.0f, 6.0f);
    if (D18Ui::Button("Close"))
    {
        _isVisible = false;
        hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
        io.BackendFlags &= 30;
        io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;
        io.MouseDrawCursor = false;
        io.WantCaptureKeyboard = false;
        io.WantCaptureMouse = false;
    }

    if (state.nvngxIniDetected)
    {
        ImGui::Spacing();
        D18Ui::TextColored(D18HealthColor(D18Health::Waiting),
                           "Legacy nvngx.ini detected; migrate its values to OptiScaler.ini.");
    }

    const auto winSize = ImGui::GetWindowSize();
    const auto winPos = ImGui::GetWindowPos();
    if (lastPosition.x < -900.0f || (lastPosition.x >= winPos.x - 1.0f && lastPosition.y >= winPos.y - 1.0f &&
                                     lastPosition.x <= winPos.x + 1.0f && lastPosition.y <= winPos.y + 1.0f))
    {
        float posX = ((float) io.DisplaySize.x - winSize.x) / 2.0f;
        float posY = ((float) io.DisplaySize.y - winSize.y) / 2.0f;
        if (posX < 0.0f || posY < 0.0f)
        {
            posX = 50.0f;
            posY = 50.0f;
        }
        ImGui::SetWindowPos(ImVec2 { posX, posY });
        lastPosition = ImVec2 { posX, posY };
    }

#if 0
    auto& menuResScale = ctx.menuResScale;

    // BOTTOM LINE ---------------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        D18Ui::Text("%dx%d -> %dx%d (%.1f) [%dx%d (%.1f)]", currentFeature->RenderWidth(),
                    currentFeature->RenderHeight(), currentFeature->TargetWidth(), currentFeature->TargetHeight(),
                    (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth(),
                    currentFeature->DisplayWidth(), currentFeature->DisplayHeight(),
                    (float) currentFeature->DisplayWidth() / (float) currentFeature->RenderWidth());

        ImGui::SameLine(0.0f, 4.0f);

        D18Ui::Text("%d", currentFeature->FrameCount());

        ImGui::SameLine(0.0f, 10.0f);
    }

    ImGui::PushItemWidth(100.0f * menuResScale);

    auto autoText = config->MenuScale.has_value() ? "Auto" : D18Ui::Format("Auto (%3.1f)", menuResScale);
    // clang-format off
    const char* uiScales[] = { autoText.c_str(), "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1",
                               "1.2", "1.3", "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0" };
    // clang-format on

    const char* selectedScaleName = uiScales[_selectedScale];

    if (D18Ui::BeginCombo("Menu Scale", selectedScaleName))
    {
        for (int n = 0; n < std::size(uiScales); n++)
        {
            if (D18Ui::Selectable(uiScales[n], (_selectedScale == n)))
            {
                _selectedScale = n;

                if (n == 0)
                    config->MenuScale.reset();
                else
                    config->MenuScale = 0.4f + (float) n / 10.0f;
            }
        }

        ImGui::EndCombo();
    }

    ImGui::PopItemWidth();

    ImGui::SameLine(0.0f, 15.0f);

    if (D18Ui::Button("Save Settings"))
        config->SaveIni();

    ImGui::SameLine(0.0f, 6.0f);

    if (D18Ui::Button("Close"))
    {
        _isVisible = false;
        hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
        io.BackendFlags &= 30;
        io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

        _showMipmapCalcWindow = false;
        _showHudlessWindow = false;
        io.MouseDrawCursor = false;
        io.WantCaptureKeyboard = false;
        io.WantCaptureMouse = false;
    }

    auto winSize = ImGui::GetWindowSize();
    auto winPos = ImGui::GetWindowPos();

    ImGui::SameLine();

    auto textSize = D18Ui::CalcTextSize("Open Wiki (?)");
    auto& style = ImGui::GetStyle();
    textSize.x += style.FramePadding.x * 2.0f;
    textSize.x += style.ItemSpacing.x;

    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - textSize.x);

    // Make button text underline
    if (D18Ui::Button("Open Wiki"))
    {
        auto pIO = &ImGui::GetPlatformIO();
        auto ctx = ImGui::GetCurrentContext();
        pIO->Platform_OpenInShellFn(ctx, "https://github.com/optiscaler/OptiScaler/wiki");
    }
    ShowHelpMarker("Click to open the OptiScaler Wiki page\nin your default browser\n\n"
                   "Compatibility list with known game issues\nand workarounds, FG options explained\n"
                   "and other useful info");

    ImGui::Spacing();
    ImGui::Separator();

    if (state.nvngxIniDetected)
    {
        ImGui::Spacing();
        D18Ui::TextColored(toneMapColor(ImVec4(1.f, 0.f, 0.f, 1.f)),
                           "nvngx.ini detected, please move over to using OptiScaler.ini and delete the old config");
        ImGui::Spacing();
    }

    if (lastPosition.x < -900.0f || (lastPosition.x >= winPos.x - 1.0f && lastPosition.y >= winPos.y - 1.0f &&
                                     lastPosition.x <= winPos.x + 1.0f && lastPosition.y <= winPos.y + 1.0f))
    {
        float posX;
        float posY;

        posX = ((float) io.DisplaySize.x - winSize.x) / 2.0f;
        posY = ((float) io.DisplaySize.y - winSize.y) / 2.0f;

        // don't position menu outside of screen
        if (posX < 0.0 || posY < 0.0)
        {
            posX = 50;
            posY = 50;
        }

        ImGui::SetWindowPos(ImVec2 { posX, posY });
        lastPosition.x = posX;
        lastPosition.y = posY;
    }
#endif
}

void MenuCommon::RenderMipmapBiasWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags)
{
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& currentFeature = ctx.currentFeature;

    // Metrics window (for debug)
    // ImGui::ShowMetricsWindow();

    // Mipmap calculation window
    if (_showMipmapCalcWindow && currentFeature != nullptr && !currentFeature->IsFrozen() && currentFeature->IsInited())
    {
        auto posX = (io.DisplaySize.x - 450.0f) / 2.0f;
        auto posY = (io.DisplaySize.y - 200.0f) / 2.0f;

        ImGui::SetNextWindowPos(ImVec2 { posX, posY }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2 { 450.0f, 200.0f }, ImGuiCond_FirstUseEver);

        if (_displayWidth == 0)
        {
            if (config->OutputScalingEnabled.value_or_default())
            {
                _displayWidth = static_cast<uint32_t>(currentFeature->DisplayWidth() *
                                                      config->OutputScalingMultiplier.value_or_default());
            }
            else
            {
                _displayWidth = currentFeature->DisplayWidth();
            }

            _renderWidth = static_cast<uint32_t>(_displayWidth / 3.0f);
            _mipmapUpscalerQuality = 0;
            _mipmapUpscalerRatio = 3.0f;
            _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
        }

        if (ImGui::Begin("Mipmap Bias", nullptr, flags))
        {
            if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetWindowFocus();

            if (ImGui::InputScalar("Display Width", ImGuiDataType_U32, &_displayWidth, NULL, NULL, "%u"))
            {
                if (_displayWidth <= 0)
                {
                    if (config->OutputScalingEnabled.value_or_default())
                    {
                        _displayWidth = static_cast<uint32_t>(currentFeature->DisplayWidth() *
                                                              config->OutputScalingMultiplier.value_or_default());
                    }
                    else
                    {
                        _displayWidth = currentFeature->DisplayWidth();
                    }
                }

                _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
            }

            const char* q[] = { "Ultra Performance", "Performance", "Balanced", "Quality", "Ultra Quality", "DLAA" };
            float fr[] = { 3.0f, 2.0f, 1.7f, 1.5f, 1.3f, 1.0f };
            auto configQ = _mipmapUpscalerQuality;

            const char* selectedQ = q[configQ];

            ImGui::BeginDisabled(config->UpscaleRatioOverrideEnabled.value_or_default());

            if (ImGui::BeginCombo("Upscaler Quality", selectedQ))
            {
                for (int n = 0; n < 6; n++)
                {
                    if (ImGui::Selectable(q[n], (_mipmapUpscalerQuality == n)))
                    {
                        _mipmapUpscalerQuality = n;

                        float ov = -1.0f;

                        if (config->QualityRatioOverrideEnabled.value_or_default())
                        {
                            switch (n)
                            {
                            case 0:
                                ov = config->QualityRatio_UltraPerformance.value_or(-1.0f);
                                break;

                            case 1:
                                ov = config->QualityRatio_Performance.value_or(-1.0f);
                                break;

                            case 2:
                                ov = config->QualityRatio_Balanced.value_or(-1.0f);
                                break;

                            case 3:
                                ov = config->QualityRatio_Quality.value_or(-1.0f);
                                break;

                            case 4:
                                ov = config->QualityRatio_UltraQuality.value_or(-1.0f);
                                break;
                            }
                        }

                        if (ov > 0.0f)
                            _mipmapUpscalerRatio = ov;
                        else
                            _mipmapUpscalerRatio = fr[n];

                        _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                        _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::EndDisabled();

            auto minLimit = config->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
            auto maxLimit = config->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;
            if (ImGui::SliderFloat("Upscaler Ratio", &_mipmapUpscalerRatio, minLimit, maxLimit, "%.2f"))
            {
                _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
            }

            if (ImGui::InputScalar("Render Width", ImGuiDataType_U32, &_renderWidth, NULL, NULL, "%u"))
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);

            ImGui::SliderFloat("Mipmap Bias", &_mipBiasCalculated, -15.0f, 0.0f, "%.6f");

            // BOTTOM LINE
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::SameLine();
            ImGui::Spacing();

            constexpr float spacing = 6.0f;
            auto textSize = ImGui::CalcTextSize("Use Value");
            textSize += ImGui::CalcTextSize("Close");
            textSize.x += ImGui::GetStyle().FramePadding.x * 5.0f + spacing; // 2 sides * 2 buttons + 1

            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - textSize.x);

            if (ImGui::Button("Use Value"))
            {
                _mipBias = _mipBiasCalculated;
                _showMipmapCalcWindow = false;
            }

            ImGui::SameLine(0.0f, spacing);

            if (ImGui::Button("Close"))
                _showMipmapCalcWindow = false;

            ImGui::Spacing();
            ImGui::Separator();

            ImGui::End();
        }
    }
}

void MenuCommon::RenderHudlessResourcesWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;

    auto fg = state.currentFG;
    if (_showHudlessWindow && config->FGHUDFix.value_or_default() && fg != nullptr && fg->IsActive())
    {
        auto posX = (io.DisplaySize.x - 400.0f) / 2.0f;
        auto posY = (io.DisplaySize.y - 300.0f) / 2.0f;

        ImGui::SetNextWindowPos(ImVec2 { posX, posY }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2 { 400.0f, 300.0f });

        if (ImGui::Begin("HUDless Resources", nullptr, flags))
        {
            if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetWindowFocus();

            int btnCount = 100;

            if (ImGui::BeginTable("HUDlessTable", 2, ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("##1", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##2", ImGuiTableColumnFlags_WidthFixed);

                ankerl::unordered_dense::map<void*, CapturedHudlessInfo>::iterator it;

                for (it = state.capturedHudlesses.begin(); it != state.capturedHudlesses.end(); it++)
                {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::Text("%08x, %s->%s, Count: %llu, %s", (size_t) it->first,
                                GetSourceString(it->second.captureInfo & 0xFF).c_str(),
                                GetDispatchString(it->second.captureInfo & 0xFF00).c_str(), it->second.usageCount,
                                it->second.enabled ? "Active" : "Passive");

                    ImGui::TableSetColumnIndex(1);

                    btnCount++;
                    std::string text;

                    if (it->second.enabled)
                        text = StrFmt("Disable##%d", btnCount);
                    else
                        text = StrFmt("Enable##%d", btnCount);

                    if (ImGui::Button(text.c_str()))
                    {
                        LOG_DEBUG("HUDless {:X}: {}", (size_t) it->first,
                                  it->second.enabled ? "Disabling" : "Enabling");
                        it->second.enabled = !it->second.enabled;
                    }
                }

                ImGui::EndTable();
            }

            if (ImGui::Button("Clear##4"))
            {
                LOG_DEBUG("Clearing captured HUDless resources");
                state.clearCapturedHudlesses = true;
            }

            ImGui::SameLine(0.0f, 8.0f);

            if (ImGui::Button("Close##4"))
                _showHudlessWindow = false;

            ImGui::End();
        }
    }
}

void MenuCommon::RenderMainMenuWindow(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& frameTimesCalculated = ctx.frameTimesCalculated;
    auto& menuResScale = ctx.menuResScale;

    if (!_isVisible)
        return;

    // Check for GPU support once and reuse the result in all menu sections.
    // DXVK might call Vulkan device creation, which would destroy our objects.
    State::Instance().vulkanSkipHooks = true;
    ctx.primaryGpu =
        std::make_unique<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>>(IdentifyGpu::getPrimaryGpu());
    State::Instance().vulkanSkipHooks = false;

    // Overlay font
    if (config->UseHQFont.value_or_default())
        ImGui::PushFontSize(std::round(menuResScale * fontSize));

    // If overlay is not visible frame needs to be inited
    if (!frameTimesCalculated)
    {
        float frameCnt = 0;
        frameTime = 0;
        for (size_t i = 299; i > 199; i--)
        {
            if (state.frameTimes[i] > 0.0)
            {
                frameTime += state.frameTimes[i];
                frameCnt++;
            }
        }

        frameTime /= frameCnt;
        frameRate = 1000.0 / frameTime;
    }

    ImGuiWindowFlags flags = 0;
    flags |= ImGuiWindowFlags_NoSavedSettings;
    flags |= ImGuiWindowFlags_NoCollapse;

    if (lastMenuScale != menuResScale)
    {
        lastMenuScale = menuResScale;

        // if UI scale is changed rescale the style
        ImGuiStyle& style = ImGui::GetStyle();
        ImGuiStyle styleold = style; // Backup colors
        style = ImGuiStyle();        // IMPORTANT: ScaleAllSizes will change the original size,
                                     // so we should reset all style config

        ApplyThemeStyle();

        style.ScaleAllSizes(menuResScale);
        style.MouseCursorScale = 1.0f;
        CopyMemory(style.Colors, styleold.Colors, sizeof(style.Colors)); // Restore colors

    }

    const ImVec2 minWindowSize { 460.0f * menuResScale, 360.0f * menuResScale };
    const ImVec2 maxWindowSize {
        ctx.io.DisplaySize.x > 0.0f ? std::max(minWindowSize.x, ctx.io.DisplaySize.x - 24.0f) : FLT_MAX,
        ctx.io.DisplaySize.y > 0.0f ? std::max(minWindowSize.y, ctx.io.DisplaySize.y - 24.0f) : FLT_MAX
    };
    ImGui::SetNextWindowSizeConstraints(minWindowSize, maxWindowSize);

    if (!d18WindowSizeInitialized)
    {
        ImVec2 initialSize { config->MenuWidth.value_or(500.0f * menuResScale),
                             config->MenuHeight.value_or(600.0f * menuResScale) };
        initialSize.x = std::clamp(initialSize.x, minWindowSize.x, maxWindowSize.x);
        initialSize.y = std::clamp(initialSize.y, minWindowSize.y, maxWindowSize.y);
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_Always);
        d18WindowSizeInitialized = true;
    }

    // Main menu window
    if (windowTitle.empty())
    {
        windowTitle = StrFmt("DLSSNR D18 - %s %s %s %s", state.gameExe.c_str(),
                             state.gameName.empty() ? "" : StrFmt("- %s", state.gameName.c_str()).c_str(),
                             (state.detectedQuirks.size() > 0) ? "(Q)" : "", state.isOptiPatcherSucceed ? "(OP)" : "");
    }

    if (ImGui::Begin(windowTitle.c_str(), NULL, flags))
    {
        const ImVec2 liveWindowSize = ImGui::GetWindowSize();
        config->MenuWidth = liveWindowSize.x;
        config->MenuHeight = liveWindowSize.y;

        // Header/status messages shown above the two-column settings table.
        RenderMainMenuHeaderMessages(ctx);

        // Main two-column settings content.
        RenderMainMenuTable(ctx);

        // Compact diagnostics live above the four D18 panels; keep only the action footer here.
        RenderMainMenuBottomBar(ctx);

        // UI evidence uses the common bounded ring for DX11, DX12 and Vulkan.
        // Sample at most 4 Hz; Off never queries input or writes a record here.
        const auto diagnosticMode = static_cast<DlssNr::Diagnostics::Mode>(std::min(config->DlssNrDiagnostics.value_or_default(), 2u));
        static double lastUiDiagnostic = -1.0;
        if (diagnosticMode != DlssNr::Diagnostics::Mode::Off &&
            (ImGui::GetTime() < lastUiDiagnostic || ImGui::GetTime() - lastUiDiagnostic >= 0.25))
        {
            lastUiDiagnostic = ImGui::GetTime();
            const auto input = OptiInput::GetDebugState();
            auto* window = ImGui::GetCurrentWindow();
            const auto* hovered = GImGui->HoveredWindow;
            const auto& io = ImGui::GetIO();
            const std::string route = D18Ui::Format("%s/%s/%s", D18ApiName(state.api),
                input.PollingOnly ? "poll" : "messages", input.PollingOnly ? (input.WheelUsesRaw ? "raw" : "queue") : "normal");
            DlssNr::Diagnostics::Event event {};
            event.type = "ui_scroll";
            event.reason = route.c_str();
            event.frame = ImGui::GetFrameCount();
            event.width = static_cast<uint32_t>(window->Size.x);
            event.height = static_cast<uint32_t>(window->Size.y);
            event.result = GImGui->ActiveId;
            event.ratio = window->Scroll.y;
            event.exposure = window->ScrollMax.y;
            event.whitePoint = io.MouseWheel;
            event.mvScaleX = io.MousePos.x;
            event.mvScaleY = io.MousePos.y;
            event.flags = (input.Focused ? 1u : 0u) | (input.MouseLeftDown ? 2u : 0u) |
                (io.MouseDown[0] ? 4u : 0u) | (hovered == window ? 8u : 0u) |
                (input.WheelObserverReady ? 16u : 0u) | (window->ScrollbarY ? 32u : 0u) |
                (GImGui->ActiveId == ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y) ? 64u : 0u);
            DlssNr::Diagnostics::Record(diagnosticMode, event);
        }
        ImGui::End();
    }

    // Detached utility windows owned by the main menu.
    RenderMipmapBiasWindow(ctx, flags);
    RenderHudlessResourcesWindow(ctx, flags);

    if (config->UseHQFont.value_or_default())
        ImGui::PopFontSize();
}

void KeyUp(UINT vKey)
{
    inputMenu = vKey == Config::Instance()->ShortcutKey.value_or_default();
    inputFps = vKey == Config::Instance()->FpsShortcutKey.value_or_default();
    inputFG = vKey == Config::Instance()->FGShortcutKey.value_or_default();
    inputFpsCycle = vKey == Config::Instance()->FpsCycleShortcutKey.value_or_default();
}

bool MenuCommon::RenderMenu()
{
    if (!_isInited)
        return false;

    RenderMenuContext ctx { State::Instance(), Config::Instance(), ImGui::GetIO() };
    ctx.now = Util::MillisecondsNow();
    ctx.currentFeature = ctx.state.currentFeature;

    // 1) Collect timing and input state before any ImGui drawing.
    UpdateRenderTiming(ctx);
    UpdateMenuInputMode(ctx);
    HandleMenuShortcuts(ctx);

    // 2) Prepare one-shot notifications and start a new ImGui frame only when needed.
    UpdateVersionAndStartupNotifications(ctx);
    BeginMenuFrameIfNeeded(ctx);
    OptiInput::EndFrame(_isVisible);

    // 3) Draw lightweight overlay windows first, preserving the original order.
    ctx.menuResScale = MenuResolutionScale(ctx.io);
    RenderSplashWindow(ctx);
    RenderNotifications(ctx);
    UpdateFrameTimeAverages(ctx);
    RenderPerformanceOverlay(ctx);

    // 4) Draw the full settings menu last so popups and child windows keep their existing behavior.
    RenderMainMenuWindow(ctx);

    if (ctx.newFrame)
        ImGui::EndFrame();

    return ctx.newFrame;
}

void MenuCommon::Init(HWND InHwnd, bool isUWP)
{
    // Reset shutdown flag in case of re-init
    State::Instance().isShuttingDown = false;

    HWND oldHandle = nullptr;

    if (_handle != nullptr)
    {
        oldHandle = _handle;
        LOG_DEBUG("Old Handle: {:X}, ImGui Handle: {:X}", (size_t) oldHandle,
                  (size_t) ImGui::GetMainViewport()->PlatformHandleRaw);
    }

    _handle = InHwnd;
    _isVisible = false;
    _isUWP = isUWP;
    lastPosition = { -1000.0f, -1000.0f };

    LOG_DEBUG("Handle: {0:X}", (size_t) _handle);

    // In case d3d12 wasn't yet used up to this point, try to update GPU info late here
    IdentifyGpu::updateD3d12Capabilities();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
    io.BackendFlags &= 30;
    io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
    io.WantSetMousePos = _isVisible;

    io.IniFilename = io.LogFilename = nullptr;

    bool initResult = false;

    if (io.BackendPlatformUserData == nullptr)
    {
        if (!isUWP)
        {
            initResult = ImGui_ImplWin32_Init(InHwnd);
            LOG_DEBUG("ImGui_ImplWin32_Init result: {0}", initResult);
        }
        else
        {
            initResult = ImGui_ImplUwp_Init(InHwnd);
            ImGui_BindUwpKeyUp(KeyUp);
            LOG_DEBUG("ImGui_ImplUwp_Init result: {0}", initResult);
        }
    }

    if (io.Fonts->Fonts.empty() && Config::Instance()->UseHQFont.value_or_default())
    {
        ImFontAtlas* atlas = io.Fonts;
        atlas->Clear();

        // This automatically becomes the next default font
        ImFontConfig fontConfig;

        if (Config::Instance()->FontSize.has_value())
            fontSize = Config::Instance()->FontSize.value();

        if (Config::Instance()->TTFFontPath.has_value())
        {
            io.FontDefault =
                atlas->AddFontFromFileTTF(wstring_to_string(Config::Instance()->TTFFontPath.value()).c_str(), fontSize,
                                          &fontConfig, io.Fonts->GetGlyphRangesDefault());
        }
        else
        {
            io.FontDefault = atlas->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,
                                                                         fontSize, &fontConfig);
        }
    }

    D18Ui::AddChineseFont(io.Fonts, fontSize);

    if (!Config::Instance()->OverlayMenu.value_or_default())
    {
        _hdrTonemapApplied = false;
    }

    DWORD hwndPid = 0;
    DWORD hwndTid = GetWindowThreadProcessId(_handle, &hwndPid);

    LOG_DEBUG("HWND: {:X}, IsWindow: {}, HWND PID: {}, Current PID: {}, HWND TID: {}, Current TID: {}",
              (ULONG64) _handle, IsWindow(_handle), hwndPid, GetCurrentProcessId(), hwndTid, GetCurrentThreadId());

    OptiInput::InitializeOptions inputOptions {};
    inputOptions.TargetHwnd = _handle;
    inputOptions.IsUwp = isUWP;
    inputOptions.PollingOnly = Config::Instance()->NgxOnlyMode.value_or_default() &&
        DlssNr::ReProfile::Known(State::Instance().gameExe.c_str());
    inputOptions.UseWndProcSubclass = !inputOptions.PollingOnly;
    OptiInput::Initialize(inputOptions);

    ApplyThemeStyle();
    _isInited = true;
}

void MenuCommon::Shutdown()
{
    if (!MenuCommon::_isInited)
        return;

    // if (_oWndProc != nullptr)
    //{
    //     auto handle = (HWND) ImGui::GetMainViewport()->PlatformHandleRaw;
    //     SetLastError(0);
    //     auto restoreResult = SetWindowLongPtr(handle, GWLP_WNDPROC, (LONG_PTR) _oWndProc);
    //     auto error = GetLastError();

    //    if (restoreResult == 0 && error != 0)
    //    {
    //        LOG_ERROR("Failed to restore old WndProc. Error: {:X}", error);
    //    }

    //    _oWndProc = nullptr;
    //}

    if (!_isUWP)
        ImGui_ImplWin32_Shutdown();
    else
        ImGui_ImplUwp_Shutdown();

    ImGui::DestroyContext();

    _handle = nullptr;
    _isInited = false;
    _isVisible = false;
}

void MenuCommon::HideMenu()
{
    if (!_isVisible)
        return;

    _isVisible = false;

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    _showMipmapCalcWindow = false;
    _showHudlessWindow = false;

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
}
