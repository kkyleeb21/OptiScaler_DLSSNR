#pragma once
#include <atomic>
#include <cstdint>

namespace DlssNr::FgPause {
enum class Installation : uint8_t { NotInstalled, Installing, Ready, Busy, NoModule, PathUnavailable,
    VersionUnavailable, UnsupportedVersion, ExportsUnavailable, LeaseFailed, ThreadFailed,
    BeginFailed, UpdateFailed, AttachFailed, CommitFailed };
inline std::atomic<Installation> installation{Installation::NotInstalled};
inline std::atomic<long> installError{0};
inline std::atomic<uint32_t> observedPaths{0}; // 1=native getter thunk, 2=existing plugin callback; UI only.
enum class Status : uint8_t { OptionOff, NoSignal, Running, Paused };
// One lock-free word: observation (2), viewport (32), multiple viewports (1), generation (29).
// 0=unknown, 1=off before any on, 2=on, 3=off after on. No timing inference.
inline std::atomic<uint64_t> signal{0};
static_assert(std::atomic<uint64_t>::is_always_lock_free);
inline constexpr uint64_t multipleViewportBit=uint64_t{1}<<34;
inline constexpr uint64_t payloadMask=(uint64_t{1}<<35)-1;
inline void Reset() {
    auto old=signal.load();
    while(!signal.compare_exchange_weak(old,((old & ~payloadMask)+(uint64_t{1}<<35)) & ~payloadMask)) {}
}
inline void Accept(uint64_t before, uint32_t viewport, uint32_t mode, bool valid, bool success) {
    if(!valid || !success || mode>3)return; // eOff / eOn / eAuto / eDynamic only.
    auto old=signal.load();
    do {
        if((old & ~payloadMask)!=(before & ~payloadMask))return; // Returned across a rebuild.
        const auto observed=unsigned(old & 3);
        if(old & multipleViewportBit)return; // Sticky until lifecycle/option reset.
        if(observed && uint32_t(old>>2)!=viewport) {
            if(signal.compare_exchange_weak(old,old|multipleViewportBit))return;
            continue;
        }
        const bool same=observed && uint32_t(old>>2)==viewport;
        const bool seenOn=same && observed>=2;
        const uint64_t next=(old & ~payloadMask)|(uint64_t{viewport}<<2)|(mode?2u:seenOn?3u:1u);
        if(signal.compare_exchange_weak(old,next))return;
    } while(true);
}
inline bool MultipleViewports() {return (signal.load() & multipleViewportBit)!=0;}
inline Status Read(bool option, bool eligible, bool nrEnabled) {
    if(!option)return Status::OptionOff;
    if(!eligible)return Status::NoSignal;
    const auto state=signal.load();
    if(state & multipleViewportBit)return Status::NoSignal;
    const auto observed=unsigned(state & 3);
    if(observed<2)return Status::NoSignal; // Never suspend before the first accepted on.
    return observed==3 && nrEnabled?Status::Paused:Status::Running;
}
}
