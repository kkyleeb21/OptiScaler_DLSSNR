#pragma once
#include <d3d12.h>

namespace DlssNr {
// Compare against the exact typed clone format chosen by the existing backend.
// Flags differ intentionally: the clone is an SRV-readable COPY_DEST resource.
inline bool GuideCopyCompatible(const D3D12_RESOURCE_DESC& clone,
                                const D3D12_RESOURCE_DESC& source, DXGI_FORMAT typedFormat)
{
    return clone.Dimension == source.Dimension && clone.Width == source.Width &&
           clone.Height == source.Height && clone.DepthOrArraySize == source.DepthOrArraySize &&
           clone.MipLevels == source.MipLevels && clone.Format == typedFormat &&
           clone.SampleDesc.Count == source.SampleDesc.Count &&
           clone.SampleDesc.Quality == source.SampleDesc.Quality;
}
constexpr bool RetainedResourcesCompatible(bool outputSizeChanged, bool outputFormatChanged,
                                           bool usedGuideChanged)
{
    return !outputSizeChanged && !outputFormatChanged && !usedGuideChanged;
}
}
