#include "pch.h"
#include "Diagnostics.h"

#include <Windows.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <mutex>

namespace DlssNr::Diagnostics
{
namespace
{
constexpr uint32_t kSchema = 1;
constexpr uint32_t kCapacity = 4096;
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
std::atomic<const char*> g_lastType { "none" };
std::atomic<const char*> g_lastReason { "" };
std::atomic<uint64_t> g_lastFrame { 0 };
std::atomic<uint32_t> g_lastResult { 0 };
std::atomic<uint64_t> g_recorded { 0 };

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
    CopyText(g_ring.header->backend, "DX12");
    char exe[MAX_PATH] {}; GetModuleFileNameA(nullptr, exe, MAX_PATH);
    const char* name = std::max(strrchr(exe, '\\') ? strrchr(exe, '\\') + 1 : exe,
                                strrchr(exe, '/') ? strrchr(exe, '/') + 1 : exe);
    CopyText(g_ring.header->game, name);
}

void Write(const Event& e)
{
    g_lastType.store(e.type ? e.type : "unknown", std::memory_order_relaxed);
    g_lastReason.store(e.reason ? e.reason : "", std::memory_order_relaxed);
    g_lastFrame.store(e.frame, std::memory_order_relaxed);
    g_lastResult.store(e.result, std::memory_order_relaxed);
    g_recorded.fetch_add(1, std::memory_order_relaxed);
    std::call_once(g_ring.init, Initialise);
    if (g_ring.header == nullptr) return;
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
    if (seq > kCapacity) InterlockedIncrement64(&g_ring.header->dropped);
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
    Write(event);
    const LONG64 now = static_cast<LONG64>(GetTickCount64());
    if (g_ring.header != nullptr && now - g_ring.header->lastTriggerMs >= static_cast<LONG64>(kTriggerCooldownMs))
        InterlockedExchange64(&g_ring.header->lastTriggerMs, now);
}

uint64_t Dropped()
{
    return g_ring.header == nullptr ? 0 : static_cast<uint64_t>(g_ring.header->dropped);
}

Snapshot Latest()
{
    return { g_lastType.load(std::memory_order_relaxed), g_lastReason.load(std::memory_order_relaxed),
             g_lastFrame.load(std::memory_order_relaxed), g_lastResult.load(std::memory_order_relaxed),
             g_recorded.load(std::memory_order_relaxed), Dropped() };
}
} // namespace DlssNr::Diagnostics
