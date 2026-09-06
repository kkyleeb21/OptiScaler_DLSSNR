#pragma once
#include <cstdint>

namespace DlssNrAbi {
constexpr uint32_t Version = 0xD1800002u;
struct Rect { uint32_t x = 0, y = 0, width = 0, height = 0; };
struct Frame {
    uint32_t version = Version;
    uint32_t size = sizeof(Frame);
    Rect color, output, depth, motion;
};
inline bool Fits(const Rect& r, uint64_t w, uint32_t h) {
    return r.width && r.height && r.x < w && r.y < h &&
           r.width <= w-r.x && r.height <= h-r.y;
}
static_assert(sizeof(Frame) == 72);
}
