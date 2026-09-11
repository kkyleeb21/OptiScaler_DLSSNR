#include "../../OptiScaler/dlssnr/QueueTrackingPolicy.h"
#include <memory>
#include <map>
#include <vector>
#include <cassert>
#include <cstdio>
struct Queue { bool failed=false; bool fence=true; uint64_t completed=1,target=1; };
int main() {
    using namespace DlssNr::Retirement;
    std::map<int,std::shared_ptr<Queue>> queues;
    for(int i=0;i<6;++i) queues[i]=std::make_shared<Queue>();
    auto live=queues[0]; auto retired=queues[1];
    queues[2]->completed=0; queues[3]->completed=UINT64_MAX; queues[4]->failed=true;
    std::weak_ptr<Queue> unused=queues[5];
    std::vector<std::shared_ptr<Queue>> garbage;
    auto ready=[](const Queue& q){return QueueIdle(q.failed,q.fence,q.completed,q.target);};
    DetachUnusedQueues(queues,garbage,ready);
    assert(queues.size()==5 && garbage.size()==1 && !unused.expired());
    garbage.clear(); assert(unused.expired());
    live.reset(); retired.reset(); queues[2]->completed=1;
    DetachUnusedQueues(queues,garbage,ready);
    assert(queues.size()==2 && garbage.size()==3);
    garbage.clear(); DetachUnusedQueues(queues,garbage,ready); assert(garbage.empty());
    assert(QueueIdle(false,false,0,0) && !QueueIdle(false,false,0,1));
    for(int i=0;i<1000;++i){queues[6]=std::make_shared<Queue>();DetachUnusedQueues(queues,garbage,ready);garbage.clear();assert(queues.size()==2);}
    puts("PASS: live/retired/inflight/removed/failed preserved; idle released after unlock; queue churn bounded");
}
