#pragma once
#include <mutex>
#include <type_traits>

namespace DlssNr {
// One small CPU-only copy. The renderer never waits for a reader: contention
// keeps the previous complete snapshot until the next publication attempt.
template<class T> class PublishedSnapshot
{
    static_assert(std::is_trivially_copyable_v<T>);
    mutable std::mutex mutex_;
    T value_{};
public:
    bool TryPublish(const T& next)
    {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        value_ = next;
        return true;
    }
    T Read() const
    {
        std::lock_guard lock(mutex_);
        return value_;
    }
};
}
