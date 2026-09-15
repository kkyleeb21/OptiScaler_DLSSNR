#pragma once
#include <atomic>

namespace DlssNr {
// UI publishes only a value. Each API adapter owns draining, retirement and creation.
// A successfully created hint is not proof of the runtime's internal model choice.
struct SrPresetState {
    std::atomic<unsigned> requested{0}, prepared{0}, created{0};
    std::atomic<bool> hasCreated{false};
    bool Pending() const { return requested.load() != prepared.load(); }
    void Request(unsigned hint) { requested.store(hint); }
};
}
