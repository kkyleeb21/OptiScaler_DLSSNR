#pragma once
#include <windows.h>
#include <string>
namespace S0CaptureIdentity {
struct Identity { unsigned long pid=0; unsigned long long creation=0; bool valid=false; };
inline const Identity& Current() {
    static const Identity value=[](){Identity v;v.pid=GetCurrentProcessId();FILETIME c{},e{},k{},u{};
        v.valid=GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u)!=FALSE;
        if(v.valid)v.creation=(static_cast<unsigned long long>(c.dwHighDateTime)<<32)|c.dwLowDateTime;
        v.valid=v.valid&&v.pid!=0&&v.creation!=0;return v;}();return value;
}
inline std::string Fields(unsigned long long request,unsigned long long revision) {
    const auto& v=Current();return "\"capture_identity_schema\":\"d18-process-request-v1\",\"pid\":"+std::to_string(v.pid)+
        ",\"process_creation\":\""+std::to_string(v.creation)+"\",\"identity_valid\":"+(v.valid?"true":"false")+
        ",\"request_id\":"+std::to_string(request)+",\"request_revision\":"+std::to_string(revision);
}
}
