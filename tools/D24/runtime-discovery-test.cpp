#include "RuntimeDiscoveryPolicy.h"
#include <cassert>
int main() {
    using DlssNr::AllowRuntimeCandidate;
    assert(AllowRuntimeCandidate(L"nvngx_dlss.dll",true,false));
    assert(AllowRuntimeCandidate(L"NVNGX_DLSSD.DLL",true,false));
    assert(!AllowRuntimeCandidate(L"nvngx_dlssg.dll",true,false));
    assert(!AllowRuntimeCandidate(L"sl.interposer.dll",true,false));
    assert(AllowRuntimeCandidate(L"nvngx_dlssg.dll",true,true));
    assert(AllowRuntimeCandidate(L"nvngx_dlssg.dll",false,false));
    assert(!AllowRuntimeCandidate(L"nvngx_dlss.dll.old",true,false));
}
