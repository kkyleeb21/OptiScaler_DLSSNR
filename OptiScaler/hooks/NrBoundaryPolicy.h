#pragma once
#include <cstdint>
namespace NrState {
inline bool MissingParametersAllowed(uint64_t missing,uint64_t samplerTables,bool noSamplersProven) {
    return missing==0 || (noSamplersProven && (missing & ~samplerTables)==0);
}
}
