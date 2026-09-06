#pragma once

#include <cstdint>

namespace DlssNr::Diagnostics
{
enum class Mode : uint32_t { Off = 0, Summary = 1, Trace = 2 };

struct Event
{
    const char* type = "unknown";
    const char* reason = "";
    uint64_t frame = 0;
    uint64_t featureGeneration = 0;
    uint64_t queue = 0;
    uint64_t commandList = 0;
    uint64_t fenceTarget = 0;
    uint64_t fenceCompleted = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t networkWidth = 0;
    uint32_t networkHeight = 0;
    uint32_t guideWidth = 0;
    uint32_t guideHeight = 0;
    uint32_t result = 0;
    float ratio = 1.0f;
    float exposure = 1.0f;
    float whitePoint = 1.0f;
    float mvScaleX = 0.0f;
    float mvScaleY = 0.0f;
    uint32_t flags = 0;
};

struct Snapshot
{
    char type[32];
    char reason[96];
    uint64_t frame;
    uint32_t result;
    uint64_t recorded;
    uint64_t dropped;
};

// Off returns before touching the ring. Summary records lifecycle/failure events; Trace also records
// per-frame contracts. After lazy initialization the ring has fixed capacity. Writes are serialized;
// no GPU wait, explicit flush, per-record file growth or image capture is added.
void Record(Mode mode, const Event& event, bool traceOnly = false);
void Trigger(Mode mode, const Event& event);
uint64_t Dropped();
Snapshot Latest();
} // namespace DlssNr::Diagnostics
