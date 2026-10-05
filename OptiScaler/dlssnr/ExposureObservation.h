#pragma once
#include <nvsdk_ngx.h>
#include <mutex>
#include <unordered_map>

namespace DlssNr::ExposureObservation {
struct Creation { bool known=false; unsigned flags=0; int driver=-1; };
inline std::mutex mutex;
inline std::unordered_map<unsigned,Creation> creations;
// Capture the public incoming flags before a replacement backend can rewrite them.
inline void Created(NVSDK_NGX_Result result,const NVSDK_NGX_Handle* handle,
                    NVSDK_NGX_Feature feature,bool known,unsigned flags) {
    if(result!=NVSDK_NGX_Result_Success || !handle ||
       (feature!=NVSDK_NGX_Feature_SuperSampling && feature!=NVSDK_NGX_Feature_RayReconstruction))return;
    std::lock_guard lock(mutex);
    if(creations.size()>=128 && !creations.contains(handle->Id))return; // missing observation, never invented false
    creations[handle->Id]={known,flags,feature==NVSDK_NGX_Feature_RayReconstruction?1:0};
}
inline Creation Read(unsigned handle) {
    std::lock_guard lock(mutex);auto it=creations.find(handle);
    return it==creations.end()?Creation{}:it->second;
}
inline void Forget(unsigned handle){std::lock_guard lock(mutex);creations.erase(handle);}
}
