#include "../../OptiScaler/framegen/VulkanFgReadiness.h"
#include <cassert>
#include <cstdio>
int main(){
    VulkanFg::Readiness g;
    for(unsigned n=0;n<1000;++n){assert(!g.Observe(true));assert(!g.Observe(false));}
    for(unsigned n=1;n<32;++n)assert(!g.Observe(true));assert(g.Observe(true));
    assert(!g.Observe(false));
    for(unsigned n=1;n<32;++n)assert(!g.Observe(true));assert(g.Observe(true));
    g.Reset();assert(!g.Observe(true));
    puts("alternating input never activates; stable admission, loss, recovery and explicit reset passed");
}
