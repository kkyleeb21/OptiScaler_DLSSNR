#include "../../OptiScaler/hooks/HookTransaction.h"
#include <cassert>
#include <string>
int main() {
    // Error at each real transaction boundary: no later mutation/commit after
    // a failed prerequisite, and abort only a transaction that we started.
    for (int failure = 0; failure <= 4; ++failure) {
        std::string calls;
        auto step = [&](int i, char c) { calls += c; return failure == i ? 487L : 0L; };
        auto result = HookLifecycle::Transact([&]{return step(1,'B');},
            [&]{return step(2,'U');}, [&]{return step(3,'M');},
            [&]{return step(4,'C');}, [&]{calls += 'A';});
        const char* expected[] = {"BUMC", "B", "BUA", "BUMA", "BUMC"};
        assert(calls == expected[failure]);
        assert(bool(result) == (failure == 0));
        assert(result.code == (failure == 0 ? 0 : 487));
    }
}
