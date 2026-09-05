#pragma once
#include <cstdint>
#include <mutex>
namespace DlssNr::NativeSr {
struct Status {
    uint64_t successfulFrames=0, lastTick=0;
    const void* handle=nullptr;
    bool succeeded=false;
    bool Live(uint64_t now) const {return succeeded && lastTick && now>=lastTick && now-lastTick<=1500;}
};
inline std::mutex mutex;
inline Status status;
inline void Record(const void* handle,bool success,uint64_t tick) {
    std::lock_guard lock(mutex);
    status.handle=handle;status.succeeded=success;status.lastTick=tick;
    if(success) ++status.successfulFrames;
}
inline void Released(const void* handle) {
    std::lock_guard lock(mutex);
    if(status.handle==handle){status.lastTick=0;status.succeeded=false;status.handle=nullptr;}
}
inline Status Read(){std::lock_guard lock(mutex);return status;}
}
