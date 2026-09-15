#pragma once
#include "SysUtils.h"

#include <NVNGX_Parameter.h>

#include <upscalers/IFeature_Dx11.h>

#include <with_dx12/dx11_with_dx12.h>

// API-neutral size/flag contract for native adapters without an IFeature object.
struct Dx11FgFrame
{
    unsigned renderWidth=0, renderHeight=0, displayWidth=0, displayHeight=0, targetWidth=0, targetHeight=0;
    bool invertedDepth=false, hdr=false, jitteredMv=false, lowResMv=true, withDx12=false;
    unsigned RenderWidth() const { return renderWidth; }
    unsigned RenderHeight() const { return renderHeight; }
    unsigned DisplayWidth() const { return displayWidth; }
    unsigned DisplayHeight() const { return displayHeight; }
    unsigned TargetWidth() const { return targetWidth; }
    unsigned TargetHeight() const { return targetHeight; }
    bool DepthInverted() const { return invertedDepth; }
    bool IsHdr() const { return hdr; }
    bool JitteredMV() const { return jitteredMv; }
    bool LowResMV() const { return lowResMv; }
    bool IsWithDx12() const { return withDx12; }
};

class UpscalerInputsDx11wDx12
{
  private:
    inline static ID3D12Device* _dx12Device = nullptr;
    inline static ID3D12CommandQueue* _dx12CommandQueue = nullptr;

  public:
    static void Init(ID3D11Device* dx11Device, ID3D11DeviceContext* dx11Context, ID3D12Device* dx12Device,
                     ID3D12CommandQueue* dx12CommandQueue);
    static void Reset();
    static bool SubmitNative(NVSDK_NGX_Parameter* parameters, const Dx11FgFrame& frame, bool* commandsRecorded = nullptr);

    // Input parameters are D3D11 resources. This bridge copies/prepares them through Dx11WithDx12 before FG use.
    static void UpscaleStart(NVSDK_NGX_Parameter* InParameters, IFeature_Dx11* feature);
    static void UpscaleEnd(NVSDK_NGX_Parameter* InParameters, IFeature_Dx11* feature);
};
