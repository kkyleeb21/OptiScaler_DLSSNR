#pragma once
#include <cstdint>

namespace DlssNr::S0Timing {
struct RecordingProof {
    uint64_t epoch = 0;
    unsigned inExecute = 0, submissions = 0;
    unsigned writtenMask = 0;
    bool sealed = false, invalid = false, signalFailed = false;
    void BeforeExecute() { ++inExecute; }
    void AfterExecute(bool signalOk) {
        if (!inExecute) { invalid = true; return; }
        --inExecute; ++submissions;
        if (!signalOk) signalFailed = true;
        if (submissions > 1) invalid = true;
    }
    void SuccessfulReset() { sealed = true; }
    bool SafeToRetire(bool fenceReached) const {
        return sealed && inExecute == 0 && !signalFailed &&
            (submissions == 0 || fenceReached);
    }
    bool UniqueComplete(bool fenceReached) const {
        return SafeToRetire(fenceReached) && submissions == 1 && !invalid && writtenMask == 63;
    }
};
struct ArmBudget {
    static constexpr unsigned Target = 32, Attempts = 256, Warmup = 30;
    bool active = false;
    unsigned warmup = 0, attempts = 0, saved = 0;
    uint64_t began = 0, lastSample = 0, warmupGeneration = 0;
    void Start(uint64_t now) { *this = {}; active = true; began = now; }
    bool Expired(uint64_t now) const { return now < began || now - began >= 30000; }
    bool Admit(uint64_t now, bool eligible, unsigned pending, unsigned totalSaved) {
        if (!active || !eligible || Expired(now) || saved >= Target ||
            attempts >= Attempts || pending >= 4 || totalSaved >= 128) return false;
        if (warmup < Warmup) { ++warmup; return false; }
        if (lastSample && (now < lastSample || now - lastSample < 100)) return false;
        lastSample = now; ++attempts; return true;
    }
};
}
