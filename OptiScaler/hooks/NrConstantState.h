#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>

namespace NrState {
// CPU-only model: unknown words remain unknown, never silently zero-filled for restoration.
struct Constants {
    std::vector<uint32_t> values;
    std::vector<bool> known;
    bool valid=true;
    bool Write(uint32_t offset, uint32_t count, const uint32_t* source) {
        if (!count) return true;
        if (!source || offset >= 64 || count > 64-offset) {valid=false;return false;}
        const auto extent=offset+count;
        if(values.size()<extent) {values.resize(extent);known.resize(extent,false);}
        for(uint32_t i=0;i<count;++i) {values[offset+i]=source[i];known[offset+i]=true;}
        return true;
    }
    bool Complete(uint32_t expected) const {
        return valid && expected>0 && expected<=64 && values.size()==expected &&
            std::all_of(known.begin(),known.end(),[](bool value){return value;});
    }
};
}
