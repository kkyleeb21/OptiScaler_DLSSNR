#pragma once
#include <cstdint>
namespace DlssNr {
// DX12 candidate deliberately exposes only Classic and Hybrid. Already
// tone-mapped input and unsupported selections preserve the shipped path.
constexpr uint32_t EffectiveDx12HighlightEncoding(bool linearHdr, uint32_t requested) {
    return linearHdr && requested == 1 ? 1u : 0u;
}
inline bool UpdateDx12HighlightEncoding(uint32_t& current, bool linearHdr, uint32_t requested) {
    const auto effective = EffectiveDx12HighlightEncoding(linearHdr, requested);
    if (current == effective) return false;
    current = effective;
    return true;
}
}
