#pragma once
#include <array>
#include <cstddef>

namespace DlssNr
{
// Only resources allocated by this batch are rolled back. Never adopt an existing or recorded
// resource: its lifetime remains owned by the backend's submission/fence retirement mechanism.
template<class Resource, size_t Capacity = 32> class AllocationBatch
{
    std::array<Resource**, Capacity> slots{};
    size_t count = 0;
    bool committed = false;
public:
    AllocationBatch() = default;
    AllocationBatch(const AllocationBatch&) = delete;
    AllocationBatch& operator=(const AllocationBatch&) = delete;
    template<class Allocate> bool Ensure(Resource*& slot, Allocate allocate)
    {
        if (slot) return true;
        if (count == Capacity) return false;
        slot = allocate();
        if (!slot) return false;
        slots[count++] = &slot;
        return true;
    }
    void Commit() { committed = true; }
    ~AllocationBatch()
    {
        if (!committed)
            while (count) { auto slot = slots[--count]; (*slot)->Release(); *slot = nullptr; }
    }
};
}
