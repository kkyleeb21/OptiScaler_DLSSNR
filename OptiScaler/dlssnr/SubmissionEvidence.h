#pragma once
#include <cstdint>

namespace DlssNr::Retirement {
// A queue address never establishes whether this particular recording executed.
constexpr bool ObservedLastUse(uint64_t beforeRecording, uint64_t observed, bool hasQueue)
{
    return hasQueue && observed > beforeRecording;
}
}
