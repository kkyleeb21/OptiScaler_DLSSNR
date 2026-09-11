#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace DlssNr::Retirement {
inline constexpr std::size_t MaxBatches = 4;
inline bool CanRetire(std::size_t pending) { return pending < MaxBatches; }
inline bool FenceReached(uint64_t completed, uint64_t target) {
    return completed != (std::numeric_limits<uint64_t>::max)() && completed >= target;
}
// Caller serializes access. Pending batches never block collection of other completed batches.
template<class Batches, class Ready, class Release>
std::size_t CollectReady(Batches& batches, Ready ready, Release release) {
    std::size_t collected = 0;
    for (std::size_t i = 0; i < batches.size();) {
        if (!ready(batches[i])) { ++i; continue; }
        release(batches[i]);
        batches.erase(batches.begin() + i);
        ++collected;
    }
    return collected;
}
}
