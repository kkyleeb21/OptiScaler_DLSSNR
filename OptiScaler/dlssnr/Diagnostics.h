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

// P0 metadata only. No renderer may use these observations to decide whether to run NR.
enum class FgSource : uint32_t { SlOptions, SlState, SlLoaded, NgxCreate, NgxEvaluate,
    ReflexMarker, ReflexAsync, Count };
enum class FgState : uint32_t { Unobserved, Off, On, Failed, Auxiliary };
enum FgKnown : uint32_t { FgMode = 1, FgCount = 2, FgFlags = 4, FgAux0 = 8,
    FgAux1 = 16, FgReturn = 32, FgObserved = 64 };
struct FgSample {
    FgSource source = FgSource::SlOptions;
    FgState state = FgState::Unobserved;
    uint64_t context = 0;
    uint32_t known = 0, mode = 0, count = 0, flags = 0, aux0 = 0, aux1 = 0, result = 0;
};
void ObserveFg(Mode mode, const FgSample& sample);
void ObserveFgNr(Mode mode, uint64_t attempt, uint64_t source, uint32_t evaluated, uint32_t composed);
void FgCoverage(Mode mode, const char* reason, uint32_t result = 0);
} // namespace DlssNr::Diagnostics
