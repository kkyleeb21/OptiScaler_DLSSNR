#pragma once
#include <d3d12.h>
#include "DlssNrAbi.h"

namespace DlssNr {
// Check before recording a transition, allocating NR scratch, or constructing views.
// Unsupported outputs retain SR/RR; no speculative copy-back path is introduced.
inline const char* CheckDx12OutputContract(const D3D12_RESOURCE_DESC& desc, const DlssNrAbi::Rect& active)
{
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize != 1 ||
        desc.SampleDesc.Count != 1 || desc.Width > UINT32_MAX)
        return "NR output requires a single-sample 2D texture";
    if (!(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
        return "NR output has no UAV capability; original SR/RR retained";
    if (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)
        return "NR output cannot be read as an SRV; original SR/RR retained";
    if (!DlssNrAbi::Fits(active, desc.Width, desc.Height))
        return "NR output active rectangle is outside allocation";
    return nullptr;
}
}
