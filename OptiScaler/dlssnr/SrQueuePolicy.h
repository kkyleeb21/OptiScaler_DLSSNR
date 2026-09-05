#pragma once
#include <cstdint>

namespace DlssNr::Submission {
// Age is a bootstrap freshness check, not evidence that a COM-pinned identity expired.
// Established singleton ownership remains subject to Track's device/queue checks and
// actual Execute+Signal. Unknown or ambiguous lists must still be observed first.
constexpr bool AcceptSrQueueHistory(bool known, bool ambiguous, bool sameOwner,
                                    uint64_t observedTick, uint64_t now) {
    return known && !ambiguous && now >= observedTick &&
           (sameOwner || now - observedTick <= 1500);
}
}
