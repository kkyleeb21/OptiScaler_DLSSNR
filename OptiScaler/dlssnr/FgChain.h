#pragma once
#include "Diagnostics.h"
#include <array>
namespace DlssNr::Diagnostics {
Mode ChainMode();
void ChainSample(Mode mode,const char* route,std::array<uint64_t,4> value={},
                 uint32_t result=0,bool failed=false,uint64_t sum=0,
                 bool immediate=false,uint64_t critical=0);
void ChainPair(Mode mode,uint32_t requested,uint32_t submitted);
void ChainFlush(Mode mode);
}
