#include <dlssnr/Dx11InputDiscoveryPolicy.h>
using DlssNr::InputDiscovery::Resolve;
constexpr bool AllCases() {
    for(unsigned mask=0;mask<32;++mask) {
        bool diagnostic=mask&1,known=mask&2,native=mask&4,probe=mask&8,numeric=mask&16;
        auto p=Resolve(diagnostic,known,native,probe,numeric);
        if(p.nativeRendering!=(known&&native))return false;
        if(!known&&(p.nativeRendering||p.numericCapture))return false;
        if(!known&&p.allowed!=(diagnostic&&probe))return false;
        if(p.numericCapture!=(!p.nativeRendering&&diagnostic&&known&&probe&&numeric))return false;
        if(!diagnostic&&p.allowed!=(known&&native))return false;
    }
    return true;
}
static_assert(AllCases(),"Discovery must never enable an unknown game's native renderer or numeric profile");
int main(){return AllCases()?0:1;}
