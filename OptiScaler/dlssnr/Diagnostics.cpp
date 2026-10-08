#include "pch.h"
#include "Diagnostics.h"
#include "FgChain.h"
#include "FgChainAccumulator.h"
#include <Config.h>
#include <State.h>

#include <Windows.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <array>
#include <cstdio>

namespace DlssNr::Diagnostics
{
namespace
{
constexpr uint32_t kSchema = 1;
constexpr uint32_t kCapacity = 131072;
constexpr uint64_t kTriggerCooldownMs = 5000;

#pragma pack(push, 1)
struct Header
{
    char magic[8];
    uint32_t schema;
    uint32_t headerBytes;
    uint32_t recordBytes;
    uint32_t capacity;
    uint64_t session;
    volatile LONG64 nextSequence;
    volatile LONG64 dropped;
    volatile LONG64 lastTriggerMs;
    char api[16];
    char backend[16];
    char game[64];
};

struct Record
{
    uint64_t sequence;
    uint64_t monotonicMs;
    uint64_t frame;
    uint64_t featureGeneration;
    uint64_t queue;
    uint64_t commandList;
    uint64_t fenceTarget;
    uint64_t fenceCompleted;
    uint32_t width, height, networkWidth, networkHeight, guideWidth, guideHeight;
    uint32_t result;
    uint32_t flags;
    float ratio, exposure, whitePoint, mvScaleX, mvScaleY;
    char type[32];
    char reason[96];
    volatile LONG committed;
};
#pragma pack(pop)
static_assert(sizeof(Header) == 152);
static_assert(sizeof(Record) == 248);

struct Ring
{
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    Header* header = nullptr;
    Record* records = nullptr;
    std::once_flag init;
};

Ring g_ring;
std::mutex g_writeMutex;
Snapshot g_latest { "none", "", 0, 0, 0, 0 };

template <size_t N> void CopyText(char (&dst)[N], const char* src)
{
    if (src == nullptr) src = "";
    const size_t count = std::min(N - 1, std::strlen(src));
    std::memcpy(dst, src, count);
    dst[count] = 0;
}

void Initialise()
{
    wchar_t path[MAX_PATH] {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash != nullptr) *(slash + 1) = 0;
    wcscat_s(path, L"D18Diagnostics.ring");

    const DWORD bytes = sizeof(Header) + sizeof(Record) * kCapacity;
    g_ring.file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_ring.file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size {}; size.QuadPart = bytes;
    if (!SetFilePointerEx(g_ring.file, size, nullptr, FILE_BEGIN) || !SetEndOfFile(g_ring.file)) return;
    g_ring.mapping = CreateFileMappingW(g_ring.file, nullptr, PAGE_READWRITE, 0, bytes, nullptr);
    if (g_ring.mapping == nullptr) return;
    auto* base = static_cast<unsigned char*>(MapViewOfFile(g_ring.mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes));
    if (base == nullptr) return;
    g_ring.header = reinterpret_cast<Header*>(base);
    g_ring.records = reinterpret_cast<Record*>(base + sizeof(Header));
    // A ring is one process session. Reusing committed slots would mix two runs and make the first
    // anomaly nondeterministic, so initialise the fixed allocation without preserving old records.
    std::memset(base, 0, bytes);
    std::memcpy(g_ring.header->magic, "D18DIAG", 7);
    g_ring.header->schema = kSchema;
    g_ring.header->headerBytes = sizeof(Header);
    g_ring.header->recordBytes = sizeof(Record);
    g_ring.header->capacity = kCapacity;
    g_ring.header->session = (static_cast<uint64_t>(GetCurrentProcessId()) << 32) ^ GetTickCount64();
    CopyText(g_ring.header->api, "observed");
    CopyText(g_ring.header->backend, "unknown");
    char exe[MAX_PATH] {}; GetModuleFileNameA(nullptr, exe, MAX_PATH);
    const char* name = std::max(strrchr(exe, '\\') ? strrchr(exe, '\\') + 1 : exe,
                                strrchr(exe, '/') ? strrchr(exe, '/') + 1 : exe);
    CopyText(g_ring.header->game, name);
}

void WriteLocked(const Event& e, bool trigger = false)
{
    // A call may return after diagnostics was turned off; reject its stale mode snapshot.
    if(Config::Instance()->DlssNrDiagnostics.value_or_default()==0)return;
    // P3 fixed retention budget: at most 257 writes/second, including overflow telemetry.
    // This bounds five minutes below 78k records even in Trace or a transition storm.
    static uint64_t budgetSecond=UINT64_MAX, suppressed=0;
    static uint32_t budgetWrites=0;
    const uint64_t second=GetTickCount64()/1000;
    if(second!=budgetSecond) {
        const auto missed=suppressed;budgetSecond=second;budgetWrites=0;suppressed=0;
        if(missed){Event loss{};loss.type="diagnostic_budget";loss.frame=missed;
            loss.reason="records_suppressed_not_calls";WriteLocked(loss);}
    }
    if(budgetWrites++>=256){++suppressed;return;}
    CopyText(g_latest.type, e.type);
    CopyText(g_latest.reason, e.reason);
    g_latest.frame = e.frame;
    g_latest.result = e.result;
    std::call_once(g_ring.init, Initialise);
    if (g_ring.header == nullptr)
    {
        CopyText(g_latest.type, "diagnostic_io_unavailable");
        CopyText(g_latest.reason, "Cannot create/map D18Diagnostics.ring; no metadata persisted.");
        return;
    }
    ++g_latest.recorded;
    const auto api = State::Instance().api;
    const char* apiName = api == API::DX11 ? "DX11" : api == API::DX12 ? "DX12" : api == API::Vulkan ? "Vulkan" : "unknown";
    CopyText(g_ring.header->api, apiName);
    CopyText(g_ring.header->backend, apiName);
    const uint64_t seq = static_cast<uint64_t>(InterlockedIncrement64(&g_ring.header->nextSequence));
    Record& r = g_ring.records[(seq - 1) % kCapacity];
    InterlockedExchange(&r.committed, 0);
    r.sequence = seq; r.monotonicMs = GetTickCount64(); r.frame = e.frame;
    r.featureGeneration = e.featureGeneration; r.queue = e.queue; r.commandList = e.commandList;
    r.fenceTarget = e.fenceTarget; r.fenceCompleted = e.fenceCompleted;
    r.width = e.width; r.height = e.height; r.networkWidth = e.networkWidth;
    r.networkHeight = e.networkHeight; r.guideWidth = e.guideWidth; r.guideHeight = e.guideHeight;
    r.result = e.result; r.flags = e.flags; r.ratio = e.ratio; r.exposure = e.exposure;
    r.whitePoint = e.whitePoint; r.mvScaleX = e.mvScaleX; r.mvScaleY = e.mvScaleY;
    CopyText(r.type, e.type); CopyText(r.reason, e.reason);
    MemoryBarrier(); InterlockedExchange(&r.committed, 1);
    if (seq > kCapacity)
        g_latest.dropped = static_cast<uint64_t>(InterlockedIncrement64(&g_ring.header->dropped));
    const LONG64 now = static_cast<LONG64>(GetTickCount64());
    if (trigger && now - g_ring.header->lastTriggerMs >= static_cast<LONG64>(kTriggerCooldownMs))
        InterlockedExchange64(&g_ring.header->lastTriggerMs, now);
}

void Write(const Event& e, bool trigger = false)
{
    std::lock_guard lock(g_writeMutex);
    WriteLocked(e, trigger);
}

constexpr std::array<const char*, 7> kFgNames {"sl_options", "sl_state", "sl_loaded",
    "ngx_create", "ngx_evaluate", "reflex_marker", "reflex_async"};
struct FgObservation { FgSample value {}; uint64_t updated = 0; bool seen = false; };
std::array<FgObservation, 7> g_fg {};
uint64_t g_fgSerial = 0, g_fgHeartbeat = 0, g_fgNrAttempt = 0, g_fgNrSource = 0;
uint64_t g_fgNrUpdated = 0, g_fgNrEvaluated = 0, g_fgNrComposed = 0;
uint32_t g_fgNrLast = 0;
Mode g_fgMode = Mode::Off;
void FgSession(Mode mode)
{
    if (mode == Mode::Off) { g_fgMode = mode; return; }
    if (g_fgMode == Mode::Off) {
        g_fg = {}; g_fgHeartbeat = 0; g_fgNrAttempt = g_fgNrSource = g_fgNrUpdated = 0;
        g_fgNrEvaluated = g_fgNrComposed = 0; g_fgNrLast = 0;
    }
    g_fgMode = mode;
}
uint32_t Age(uint64_t now, uint64_t updated)
{
    return static_cast<uint32_t>(std::min<uint64_t>(now - updated, UINT32_MAX - 1));
}
// A snapshot is seven consecutive schema-1 records, joined by featureGeneration.
// This reuses the existing ring lock/layout. Each source retains its own identity and age.
void FgEmit(Mode mode, uint64_t now, int changed)
{
    const auto serial = ++g_fgSerial;
    for (size_t i = 0; i < g_fg.size(); ++i) {
        const auto& s = g_fg[i]; const auto& v = s.value;
        Event e {}; e.type = "fg_signal"; e.frame = g_fgNrAttempt;
        e.featureGeneration = serial; e.queue = s.updated; e.commandList = v.context;
        e.fenceTarget = g_fgNrEvaluated; e.fenceCompleted = g_fgNrComposed;
        e.width = s.seen ? Age(now, s.updated) : UINT32_MAX;
        e.height = v.mode; e.networkWidth = v.count; e.networkHeight = v.flags;
        e.guideWidth = v.aux0; e.guideHeight = v.aux1; e.result = v.result;
        e.flags = v.known | (static_cast<uint32_t>(v.state) << 8) |
            (changed == static_cast<int>(i) ? 1u << 16 : 0u);
        char reason[96] {};
        std::snprintf(reason, sizeof(reason), "%s;t=%s;nr=%u;na=%u;ns=%llu", kFgNames[i],
            changed < 0 ? "heartbeat" : kFgNames[changed], g_fgNrUpdated ? g_fgNrLast + 1 : 0,
            g_fgNrUpdated ? Age(now, g_fgNrUpdated) : UINT32_MAX,
            static_cast<unsigned long long>(g_fgNrSource));
        e.reason = reason; WriteLocked(e);
    }
}
} // namespace

void Record(Mode mode, const Event& event, bool traceOnly)
{
    if (mode == Mode::Off || (traceOnly && mode != Mode::Trace)) return;
    Write(event);
}

void Trigger(Mode mode, const Event& event)
{
    if (mode == Mode::Off) return;
    Write(event, true);
}

uint64_t Dropped()
{
    std::lock_guard lock(g_writeMutex);
    return g_latest.dropped;
}

Snapshot Latest()
{
    std::lock_guard lock(g_writeMutex);
    return g_latest;
}

void ObserveFg(Mode mode, const FgSample& value)
{
    if (mode == Mode::Off) return;
    const auto index = static_cast<size_t>(value.source);
    if (index >= g_fg.size()) return;
    std::lock_guard lock(g_writeMutex);
    FgSession(mode);
    auto& s = g_fg[index]; const auto& old = s.value;
    // Presented totals and marker frame IDs are counters, not transitions of FG mode.
    const bool changed = !s.seen || old.context != value.context || old.known != value.known ||
        old.state != value.state || old.mode != value.mode || old.count != value.count ||
        old.flags != value.flags || old.result != value.result ||
        (index != static_cast<size_t>(FgSource::ReflexMarker) &&
         index != static_cast<size_t>(FgSource::ReflexAsync) && old.aux0 != value.aux0) ||
        ((index == static_cast<size_t>(FgSource::NgxCreate) || index == static_cast<size_t>(FgSource::NgxEvaluate)) &&
         old.aux1 != value.aux1);
    const uint64_t now = GetTickCount64();
    s.value = value; s.updated = now; s.seen = true;
    if (changed) FgEmit(mode, now, static_cast<int>(index));
    else if (now - g_fgHeartbeat >= 1000) { g_fgHeartbeat = now; FgEmit(mode, now, -1); }
}

void ObserveFgNr(Mode mode, uint64_t attempt, uint64_t source, uint32_t evaluated, uint32_t composed)
{
    if (mode == Mode::Off) return;
    std::lock_guard lock(g_writeMutex);
    FgSession(mode);
    if (mode == Mode::Off) return;
    const uint64_t now = GetTickCount64();
    g_fgNrAttempt = attempt; g_fgNrSource = source; g_fgNrUpdated = now;
    g_fgNrLast = evaluated != 0; g_fgNrEvaluated += evaluated != 0; g_fgNrComposed += composed != 0;
    if (now - g_fgHeartbeat >= 1000) { g_fgHeartbeat = now; FgEmit(mode, now, -1); }
}

void FgCoverage(Mode mode, const char* reason, uint32_t result)
{
    if (mode == Mode::Off) return;
    std::lock_guard lock(g_writeMutex);
    static uint32_t emitted = 0;
    if (emitted++ >= 32) return;
    Event e {}; e.type = "fg_probe_coverage"; e.reason = reason; e.result = result;
    WriteLocked(e);
}
namespace {
ChainAccumulator g_chain;
std::atomic<bool> g_chainEnabled{false};
void ChainEmit(const ChainWindow& s,uint64_t begin,uint64_t end,uint32_t req,uint32_t sub,bool transition) {
    Event e{};e.type=transition?"fg_chain_change":"fg_chain_second";e.reason=s.route;
    e.frame=begin;e.featureGeneration=end;
    e.queue=s.first[0];e.commandList=s.first[1];e.width=uint32_t(s.first[2]);e.height=uint32_t(s.first[3]);
    e.fenceTarget=s.last[0];e.fenceCompleted=s.last[1];e.networkWidth=uint32_t(s.last[2]);e.networkHeight=uint32_t(s.last[3]);
    e.guideWidth=uint32_t(std::min<uint64_t>(s.count,UINT32_MAX));e.guideHeight=uint32_t(std::min<uint64_t>(s.failures,UINT32_MAX));
    e.result=s.result;e.flags=(s.changed?1u:0u)|(s.count?2u:0u)|(req<<8)|(sub<<12);
    // Per-window presented-count sum; exact for the bounded expected call volume.
    e.ratio=float(s.sum);
    WriteLocked(e);
}
void ChainReset(Mode mode) {
    if(mode==Mode::Off)g_chainEnabled.store(false);
    else if(!g_chainEnabled.exchange(true))g_chain={};
}
}
Mode ChainMode() {
    const auto& state=State::Instance();
    const auto mode=(state.api==API::DX12 || state.swapchainApi==API::DX12) &&
        state.activeFgInput==FGInput::NoFG && state.activeFgOutput==FGOutput::NoFG ?
        static_cast<Mode>(std::min(Config::Instance()->DlssNrDiagnostics.value_or_default(),2u)):Mode::Off;
    if(mode==Mode::Off)g_chainEnabled.store(false);
    return mode;
}
void ChainSample(Mode mode,const char* route,std::array<uint64_t,4> value,uint32_t result,
                 bool failed,uint64_t sum,bool immediate,uint64_t critical) {
    if(mode==Mode::Off)return;
    std::lock_guard lock(g_writeMutex);ChainReset(mode);
    g_chain.Add(GetTickCount64(),route,value,result,failed,sum,immediate,critical,ChainEmit);
}
void ChainPair(Mode mode,uint32_t requested,uint32_t submitted) {
    if(mode==Mode::Off)return;
    std::lock_guard lock(g_writeMutex);ChainReset(mode);
    g_chain.Pair(GetTickCount64(),requested,submitted,ChainEmit);
}
void ChainFlush(Mode mode) {
    if(mode==Mode::Off)return;
    std::lock_guard lock(g_writeMutex);ChainReset(mode);
    g_chain.Flush(GetTickCount64(),ChainEmit);
    Event e{};e.type="fg_chain_limits";e.frame=g_chain.overflow;e.queue=g_chain.transitionsSuppressed;
    e.reason="slot_overflow_and_suppressed_transitions";WriteLocked(e);
}
} // namespace DlssNr::Diagnostics
