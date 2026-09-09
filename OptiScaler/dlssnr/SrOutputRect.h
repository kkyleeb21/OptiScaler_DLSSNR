#pragma once
#include "DlssNrAbi.h"

namespace DlssNr {
// Limited compatibility probe: do not expand arbitrary padded/nonzero-origin views.
inline DlssNrAbi::Rect ResolveSrOutputRect(bool yysls, bool rr, DlssNrAbi::Rect reported,
    uint32_t renderWidth, uint32_t renderHeight, uint64_t allocationWidth, uint32_t allocationHeight)
{
    if (yysls && !rr && reported.x == 0 && reported.y == 0 &&
        reported.width == renderWidth && reported.height == renderHeight &&
        renderWidth != 0 && renderHeight != 0 && allocationWidth <= UINT32_MAX &&
        allocationWidth > renderWidth && allocationHeight > renderHeight &&
        allocationWidth * renderHeight == uint64_t(allocationHeight) * renderWidth)
        return {0, 0, static_cast<uint32_t>(allocationWidth), allocationHeight};
    return reported;
}
}
