#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cmath>
#include <optional>
namespace DlssNr::ComposeLimits {
// Zero in the appended shader slot means auto, including old/native callers.
inline float Explicit(std::optional<float> value) {
    return value && ((std::bit_cast<uint32_t>(*value)&0x7f800000u)!=0x7f800000u) ? std::clamp(*value, 1.f, 8.f) : 0.f;
}
inline float Darken(float brighten, std::optional<float> value) {
    const float explicitValue=Explicit(value);
    return explicitValue>0 ? explicitValue : (std::max)(brighten,1.f);
}
}
