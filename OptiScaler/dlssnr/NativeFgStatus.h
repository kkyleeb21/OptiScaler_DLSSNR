#pragma once
#include <mutex>
#include <cstdint>
#include <windows.h>
namespace DlssNr::NativeFg {
struct Snapshot { uint64_t tick=0; uint32_t presented=0; bool ok=false; bool menuPaused=false;
    bool Fresh() const { return tick && GetTickCount64()-tick<1500; }
};
inline std::mutex mutex;
inline Snapshot latest;
inline void Observe(bool ok,uint32_t presented) {
    std::lock_guard<std::mutex> lock(mutex);
    latest.tick=GetTickCount64(); latest.presented=presented; latest.ok=ok;
}
inline void SetMenuPaused(bool paused) { std::lock_guard<std::mutex> lock(mutex); latest.menuPaused=paused; }
inline Snapshot Read() { std::lock_guard<std::mutex> lock(mutex); return latest; }
}
