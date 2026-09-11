#include "../../OptiScaler/framegen/VulkanFgSubmissionOrder.h"
#include <cassert>
#include <cstdio>
int main()
{
    using VulkanFg::SubmissionOrder;
    SubmissionOrder same;same.Submit(1,true,true,true);assert(same.Ready(1));
    SubmissionOrder separate;separate.Submit(1,true,false,true);separate.Submit(1,false,true,true);assert(separate.Ready(1));
    assert(!separate.Ready(2));
    SubmissionOrder reversed;reversed.Submit(1,false,true,true);reversed.Submit(1,true,false,true);assert(!reversed.Ready(1));
    SubmissionOrder missing;missing.Submit(1,false,true,true);assert(!missing.Ready(1));
    SubmissionOrder cross;cross.Submit(2,true,false,true);cross.Submit(1,false,true,true);assert(!cross.Ready(1));
    SubmissionOrder failed;failed.Submit(1,true,false,false);failed.Submit(1,false,true,true);assert(!failed.Ready(1));
    auto duplicate=separate;duplicate.Submit(1,true,false,true);assert(!duplicate.Ready(1));
    auto repeatSr=separate;repeatSr.Submit(1,false,true,true);assert(!repeatSr.Ready(1));
    SubmissionOrder unrelated;unrelated.Submit(9,false,false,false);assert(!unrelated.invalid);assert(!unrelated.Ready(1));
    puts("submission order: same command, ordered commands and seven rejection cases passed");
}
