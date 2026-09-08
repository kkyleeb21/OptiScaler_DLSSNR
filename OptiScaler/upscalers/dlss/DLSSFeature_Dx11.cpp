#include <pch.h>
#include "DLSSFeature_Dx11.h"
#include <Config.h>
#include <dlssnr/NativeControl.h>

#include <dxgi.h>

namespace
{
// A colocated optional addon explicitly opts this installation into native DX11 NR.
HMODULE D24Module()
{
    static HMODULE module = []() -> HMODULE {
        wchar_t path[MAX_PATH] = {};
        HMODULE core = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&D24Module), &core))
            return nullptr;
        GetModuleFileNameW(core, path, MAX_PATH);
        auto addon = std::filesystem::path(path).parent_path() / L"D24Native.dll";
        return LoadLibraryExW(addon.c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    }();
    return module;
}
}

bool DLSSFeatureDx11::InitInternal(ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters)
{
    if (NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_ERROR("nvngx.dll not loaded!");

        SetInit(false);
        return false;
    }

    NVSDK_NGX_Result nvResult;
    bool initResult = false;

    do
    {
        if (!_dlssInited)
        {
            _dlssInited = NVNGXProxy::InitDx11(Device);

            if (!_dlssInited)
                return false;

            _moduleLoaded =
                (NVNGXProxy::D3D11_Init_ProjectID() != nullptr || NVNGXProxy::D3D11_Init_Ext() != nullptr) &&
                (NVNGXProxy::D3D11_Shutdown() != nullptr || NVNGXProxy::D3D11_Shutdown1() != nullptr) &&
                (NVNGXProxy::D3D11_GetParameters() != nullptr || NVNGXProxy::D3D11_AllocateParameters() != nullptr) &&
                NVNGXProxy::D3D11_DestroyParameters() != nullptr && NVNGXProxy::D3D11_CreateFeature() != nullptr &&
                NVNGXProxy::D3D11_ReleaseFeature() != nullptr && NVNGXProxy::D3D11_EvaluateFeature() != nullptr;

            // delay between init and create feature
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        LOG_INFO("Creating DLSS feature");

        if (NVNGXProxy::D3D11_CreateFeature() != nullptr)
        {
            ProcessInitParams(InParameters);

            _p_dlssHandle = &_dlssHandle;
            nvResult = NVNGXProxy::D3D11_CreateFeature()(InContext, NVSDK_NGX_Feature_SuperSampling, InParameters,
                                                         &_p_dlssHandle);

            if (nvResult != NVSDK_NGX_Result_Success)
            {
                LOG_ERROR("NVNGXProxy::D3D11_CreateFeature result: {0:X}", (unsigned int) nvResult);
                break;
            }
        }
        else
        {
            LOG_ERROR("NVNGXProxy::D3D11_CreateFeature is nullptr");
            break;
        }

        ReadVersion();

        initResult = true;

    } while (false);

    SetInit(initResult);

    return initResult;
}

bool DLSSFeatureDx11::EvaluateInternal(ID3D11DeviceContext* InDeviceContext, NVSDK_NGX_Parameter* InParameters)
{
    if (!_moduleLoaded)
    {
        LOG_ERROR("nvngx.dll or _nvngx.dll is not loaded!");
        return false;
    }

    NVSDK_NGX_Result nvResult;

    if (NVNGXProxy::D3D11_EvaluateFeature() != nullptr)
    {
        ProcessEvaluateParams(InParameters);

        nvResult = NVNGXProxy::D3D11_EvaluateFeature()(InDeviceContext, _p_dlssHandle, InParameters, NULL);

        if (nvResult != NVSDK_NGX_Result_Success)
        {
            LOG_ERROR("_EvaluateFeature result: {0:X}", (unsigned int) nvResult);
            return false;
        }

        LOG_TRACE("_EvaluateFeature ok!");
        if (auto module = D24Module())
        {
            using Process = int (*)(void*, ID3D11DeviceContext*, NVSDK_NGX_Parameter*, unsigned);
            if (auto process = reinterpret_cast<Process>(GetProcAddress(module, "D24Process")))
            {
                unsigned flags = 0;
                if (IsHdr()) flags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
                if (DepthInverted()) flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
                if (LowResMV()) flags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
                if(DlssNr::NativeControl::Apply(module)){
                    process(this, InDeviceContext, InParameters, flags);
                    DlssNr::NativeControl::Observe(module);
                }
            }
        }
        else DlssNr::NativeControl::Unavailable(-100);
    }
    else
    {
        LOG_ERROR("_EvaluateFeature is nullptr");
        return false;
    }

    return true;
}

DLSSFeatureDx11::DLSSFeatureDx11(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters), DLSSFeature(InHandleId, InParameters)
{
    if (NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_INFO("nvngx.dll not loaded, now loading");
        NVNGXProxy::InitNVNGX();
    }

    LOG_INFO("binding complete!");
}

DLSSFeatureDx11::~DLSSFeatureDx11()
{
    if (State::Instance().isShuttingDown)
        return;

    if (auto module = D24Module())
        if (auto release = reinterpret_cast<int (*)(void*)>(GetProcAddress(module, "D24Release")))
            release(this);

    if (NVNGXProxy::D3D11_ReleaseFeature() != nullptr && _p_dlssHandle != nullptr)
        NVNGXProxy::D3D11_ReleaseFeature()(_p_dlssHandle);
}
