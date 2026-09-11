#include "../../OptiScaler/hooks/ReflexMarkerRouting.h"
#include <cassert>
#include <vector>
#include <iostream>
int main()
{
    for(bool preserve : {false,true}) for(bool forwarded : {false,true}) for(int nativeStatus : {0,-1})
    {
        std::vector<int> calls;
        auto result = D18Reflex::RouteMarker(preserve,
            [&] { calls.push_back(1); return nativeStatus; },
            [&] { calls.push_back(2); return forwarded; }, 0);
        if(preserve) { assert((calls==std::vector<int>{1,2})); assert(result==nativeStatus); }
        else if(forwarded) { assert((calls==std::vector<int>{2})); assert(result==0); }
        else { assert((calls==std::vector<int>{2,1})); assert(result==nativeStatus); }
    }
    std::cout << "8 routing cases PASS: native exactly once, forwarding retained, errors preserved, fallback\n";
}
