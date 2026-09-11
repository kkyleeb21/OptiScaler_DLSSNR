#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>

namespace D18Monitor
{
// Fixed storage; 30 seconds at up to 1000 application frames/s.
struct Samples
{
    struct Entry { double time = 0, interval = 0; unsigned count = 0; bool valid = false; };
    std::array<Entry, 32768> entries {};
    size_t next = 0, size = 0;
    double last = 0;
    void reset() { next = size = 0; last = 0; }
    void add(double now, unsigned count, bool valid)
    {
        if (last && (now <= last || now - last > 30)) reset();
        if (last) { entries[next] = {now, now - last, count, valid}; next = (next + 1) % entries.size(); size = std::min(size + 1, entries.size()); }
        last = now;
    }
    double fps(double now) const
    {
        if (!last || now - last > 2) return -1;
        double seconds = 0; unsigned count = 0;
        for (size_t i = 0; i < size; ++i)
        {
            const auto& e = entries[(next + entries.size() - 1 - i) % entries.size()];
            if (now - e.time > 1) break;
            if (!e.valid) return -1;
            seconds += e.interval; count += e.count;
        }
        return seconds >= .25 ? count / seconds : -1;
    }
    double low(double now) const
    {
        if (!last || now - last > 2) return -1;
        std::array<double, 32768> times {}; size_t n = 0;
        for (size_t i = 0; i < size; ++i)
        {
            const auto& e = entries[(next + entries.size() - 1 - i) % entries.size()];
            if (now - e.time > 30) break;
            if (!e.valid) return -1;
            times[n++] = e.interval;
        }
        if (n < 100) return -1;
        const size_t tail = (n + 99) / 100;
        std::partial_sort(times.begin(), times.begin() + tail, times.begin() + n, std::greater<double>());
        double sum = 0; for (size_t i = 0; i < tail; ++i) sum += times[i];
        return tail / sum;
    }
};
}
