#pragma once
#include <cstdint>

namespace DlssNr {
// One temporal chain. Source is the NGX upscaler handle id, never a rotating output texture.
// Zero means a caller with no source identity (the existing owned bridge contract).
enum class HistoryReset : uint8_t { None, FirstUse, SourceChanged, RecordingGap, Disabled, Released, Renderer, Additional };
class NrContinuity {
    uint64_t source_ = 0;
    bool valid_ = false;
    HistoryReset pending_ = HistoryReset::FirstUse;
public:
    HistoryReset ResetFor(uint64_t source) const {
        if (!valid_) return pending_;
        if (source != source_) return HistoryReset::SourceChanged;
        return pending_;
    }
    void Finish(uint64_t source, bool composed) {
        if (composed) { source_ = source; valid_ = true; pending_ = HistoryReset::None; }
        else if (valid_ && source == source_ && pending_ == HistoryReset::None)
            pending_ = HistoryReset::RecordingGap;
    }
    void Disable() { pending_ = HistoryReset::Disabled; }
    void Release(uint64_t source) {
        if (source && valid_ && source == source_) { valid_ = false; pending_ = HistoryReset::Released; }
    }
};
}
