#pragma once
#include <Windows.h>
#include <string>
#include <dlssnr/CaptureConstants.h>

namespace DlssNr::CaptureContract {
inline uint64_t RunId() {
    static const uint64_t id=[] { FILETIME t{}; GetSystemTimeAsFileTime(&t);
        return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime; }();
    return id;
}
inline std::string Hex(const DlssNrConstants& c) {
    constexpr char digits[]="0123456789abcdef";
    std::string result(capture::kNamedConstantBytes*2,'0');
    const auto* bytes=reinterpret_cast<const unsigned char*>(&c);
    for(size_t i=0;i<capture::kNamedConstantBytes;++i){result[i*2]=digits[bytes[i]>>4];result[i*2+1]=digits[bytes[i]&15];}
    return result;
}
inline void Append(std::string& json,const std::string& fields) {
    if(!json.empty()&&json.back()=='}'){json.pop_back();json+=","+fields+"}";}
}
inline void Constants(std::string& json,const DlssNrConstants& encode,const DlssNrConstants& resolve) {
    Append(json,"\"constant_abi\":\"DlssNrConstants-named-176-v1\",\"resolve_constants_complete\":true,\"resolve_constants_bytes\":176,\"encode_constants_hex\":\""+
        Hex(encode)+"\",\"resolve_constants_hex\":\""+Hex(resolve)+"\"");
}
}
