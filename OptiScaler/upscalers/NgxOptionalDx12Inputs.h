#pragma once
#include <nvsdk_ngx_defs.h>

struct ID3D12Resource;

// Adapted from wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass, 5019c979 (GPL-3.0).
// Disabled optional slots must not retain resources from another API.
template <typename Parameters>
void SetOptionalDx12Inputs(Parameters* parameters, ID3D12Resource* exposure,
                          ID3D12Resource* reactive, bool autoExposure, bool disableReactive)
{
    parameters->Set(NVSDK_NGX_Parameter_ExposureTexture,
                    static_cast<void*>(autoExposure ? nullptr : exposure));
    parameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask,
                    static_cast<void*>(disableReactive ? nullptr : reactive));
}

// Restore caller values, including null, before submission/copy-back and on
// ordinary early exits or C++ unwinding. This owns no GPU resource or wait.
template <typename Parameters>
class ScopedOptionalDx12Inputs
{
    Parameters* parameters_;
    void* exposure_;
    void* reactive_;

  public:
    ScopedOptionalDx12Inputs(Parameters* parameters, void* originalExposure, void* originalReactive,
                             ID3D12Resource* exposure, ID3D12Resource* reactive,
                             bool autoExposure, bool disableReactive)
        : parameters_(parameters), exposure_(originalExposure), reactive_(originalReactive)
    {
        SetOptionalDx12Inputs(parameters_, exposure, reactive, autoExposure, disableReactive);
    }
    ScopedOptionalDx12Inputs(const ScopedOptionalDx12Inputs&) = delete;
    ScopedOptionalDx12Inputs& operator=(const ScopedOptionalDx12Inputs&) = delete;
    ~ScopedOptionalDx12Inputs()
    {
        parameters_->Set(NVSDK_NGX_Parameter_ExposureTexture, exposure_);
        parameters_->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, reactive_);
    }
};
