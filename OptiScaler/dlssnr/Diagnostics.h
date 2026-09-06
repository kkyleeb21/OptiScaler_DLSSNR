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
    const char* type;
    const char* reason;
    uint64_t frame;
    uint32_t result;
    uint64_t recorded;
    uint64_t dropped;
};

// Off returns before touching the ring. Summary records lifecycle/failure events; Trace also records
// per-frame contracts. The ring is a fixed-size memory mapped file, so the render path performs no
// formatting, file growth, flush, wait, or image capture.
void Record(Mode mode, const Event& event, bool traceOnly = false);
void Trigger(Mode mode, const Event& event);
uint64_t Dropped();
Snapshot Latest();
} // namespace DlssNr::Diagnostics
