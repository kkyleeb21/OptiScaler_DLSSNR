#pragma once
#include "SysUtils.h"
#include <d3d12.h>
#include <memory>

class D3D12Hooks
{
  private:
    inline static std::mutex hookMutex;
    inline static std::mutex agilityMutex;

    static bool RestoreDescriptorHeaps(ID3D12GraphicsCommandList* cmdList);
    static bool RestorePipelineState(ID3D12GraphicsCommandList* cmdList);
    static bool RestoreComputeRootState(ID3D12GraphicsCommandList* cmdList);
    static bool RestoreGraphicsRootState(ID3D12GraphicsCommandList* cmdList);

  public:
    static void Hook();
    static void HookAgility(HMODULE module);
    static void HookDevice(ID3D12Device* device);
    static void Unhook();
    static void SetRootSignatureTracking(bool enable);
    static bool RootSignatureTrackingEnabled();
    static bool CanRestoreNrState(ID3D12GraphicsCommandList* cmdList);
    static std::shared_ptr<void> CaptureNativeNrBoundary(ID3D12GraphicsCommandList* cmdList);
    static bool RestoreNativeNrBoundary(ID3D12GraphicsCommandList* cmdList, const std::shared_ptr<void>& snapshot);
    // Read-only, bounded diagnostics. Does not install hooks or change the restoration decision.
    enum class NrDiagnosticPhase { BeforeNativeSr, AfterNativeSr, BeforeNr };
    static void LogNrState(ID3D12GraphicsCommandList* cmdList, NrDiagnosticPhase phase);
    static bool CanRestoreRootSignature(ID3D12GraphicsCommandList* cmdList);
    static void HookToCommandListLate(ID3D12GraphicsCommandList* commandList);
    static bool PrepareNrNativeList(ID3D12GraphicsCommandList* commandList);
    static void RestoreRoot(ID3D12GraphicsCommandList* cmdList);
};
