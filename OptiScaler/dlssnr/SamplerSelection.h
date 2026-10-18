#pragma once
#include "RuntimeProfile.h"
#include <optional>
#include <string>
namespace DlssNr::Sampler {
// Preserve the forwarder's existing 0/1 ABI; 2 adds an explicit point override.
inline constexpr int Follow=0,Linear=1,Point=2;
inline std::optional<std::string> Parse(const std::string& value) {
    if(value=="Runtime" || value=="Linear" || value=="Point")return value;
    return std::nullopt;
}
inline int Color(const Runtime::Profile* profile,bool legacyLinear,bool customFilter,const std::string& choice) {
    if(profile && !profile->legacy3108)return choice=="Linear"?Linear:choice=="Point"?Point:Follow;
    return legacyLinear && !customFilter ? Linear : Follow;
}
// A point override is nonzero in the transport ABI, but is never linear.
inline bool IsLinear(const Runtime::Profile* profile,int mode,float ratio) {
    if(!profile || profile->legacy3108)return mode!=0;
    ratio=ratio>=.5f?(std::min)(ratio,1.f):.5f;
    return mode==Linear || (mode==Follow && ratio<.9999f);
}
inline const char* Name(int mode) {return mode==Linear?"LINEAR":mode==Point?"POINT":"RUNTIME";}
}
