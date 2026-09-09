#include <Windows.h>
#include "../../OptiScaler/include/detours/detours.h"
#include "../../OptiScaler/hooks/HookTransaction.h"
#include <cassert>
volatile int seed = 11;
__declspec(noinline) int Target(int x) { return x + seed + seed + seed; }
int (*original)(int) = Target;
int Hook(int x) { return original(x) + 100; }
int main() {
    const auto transact = [](bool attach) {
        return HookLifecycle::Transact([]{return DetourTransactionBegin();},
            []{return DetourUpdateThread(GetCurrentThread());},
            [=]{return attach ? DetourAttach(reinterpret_cast<PVOID*>(&original),Hook)
                              : DetourDetach(reinterpret_cast<PVOID*>(&original),Hook);},
            []{return DetourTransactionCommit();}, []{return DetourTransactionAbort();});
    };
    assert(Target(1) == 34);
    for (int i = 0; i < 3; ++i) {
        assert(transact(true));
        assert(Target(1) == 134);
        assert(transact(false));
        assert(Target(1) == 34);
    }
}
