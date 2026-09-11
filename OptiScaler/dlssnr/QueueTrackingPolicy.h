#pragma once
#include "RetirementPolicy.h"
#include <utility>

namespace DlssNr::Retirement {
// Caller holds the map mutex. External owners include live uses, submissions and retired batches.
// Destruction is deferred until after the caller releases that mutex.
template<class Map, class Garbage, class Ready>
void DetachUnusedQueues(Map& queues, Garbage& garbage, Ready ready) {
    for (auto it=queues.begin(); it!=queues.end();) {
        if (it->second.use_count()!=1 || !ready(*it->second)) { ++it; continue; }
        garbage.push_back(std::move(it->second));
        it=queues.erase(it);
    }
}
inline bool QueueIdle(bool failed, bool hasFence, uint64_t completed, uint64_t target) {
    return !failed && (!hasFence ? target==0 : FenceReached(completed,target));
}
}
