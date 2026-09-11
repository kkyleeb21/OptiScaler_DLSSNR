#pragma once
#include "DlssNrAbi.h"

namespace DlssNr {
// A managed SR feature owns its display extent independently of mutable NGX
// query parameters. Only accept it for a full, zero-origin matching allocation.
inline bool HasFullSrOutputContract(bool rr, DlssNrAbi::Rect reported,
    uint64_t allocationWidth, uint32_t allocationHeight,
    uint32_t featureWidth, uint32_t featureHeight)
{
    return !rr && reported.x == 0 && reported.y == 0 &&
        reported.width != 0 && reported.height != 0 &&
        featureWidth == allocationWidth && featureHeight == allocationHeight &&
        featureWidth > reported.width && featureHeight > reported.height &&
        uint64_t(featureWidth) * reported.height == uint64_t(featureHeight) * reported.width;
}
// Limited compatibility probe: do not expand arbitrary padded/nonzero-origin views.
inline DlssNrAbi::Rect ResolveSrOutputRect(bool yysls, bool rr, DlssNrAbi::Rect reported,
    uint32_t renderWidth, uint32_t renderHeight, uint64_t allocationWidth, uint32_t allocationHeight,
    uint32_t featureWidth = 0, uint32_t featureHeight = 0)
{
    if (HasFullSrOutputContract(rr, reported, allocationWidth, allocationHeight, featureWidth, featureHeight))
        return {0, 0, featureWidth, featureHeight};
    if (yysls && !rr && reported.x == 0 && reported.y == 0 &&
        reported.width == renderWidth && reported.height == renderHeight &&
        renderWidth != 0 && renderHeight != 0 && allocationWidth <= UINT32_MAX &&
        allocationWidth > renderWidth && allocationHeight > renderHeight &&
        allocationWidth * renderHeight == uint64_t(allocationHeight) * renderWidth)
        return {0, 0, static_cast<uint32_t>(allocationWidth), allocationHeight};
    return reported;
}
}
