#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace DlssNr {
// Caller serializes writes. The file must be opened in binary/truncate mode.
struct BoundedDiagnosticFile {
    uint64_t bytes=0, lastFlush=0;
    bool closed=false;
    static constexpr char marker[]="event=diagnostic_budget_reached\n";
    bool Write(FILE* file, const char* text, size_t size, uint64_t now,
               uint64_t limit=16ull*1024*1024) {
        if(!file || closed) return false;
        const size_t reserve=sizeof(marker)-1;
        if(limit<reserve || bytes>limit-reserve || size>limit-reserve-bytes) {
            closed=true;
            if(bytes<=limit && reserve<=limit-bytes) bytes+=fwrite(marker,1,reserve,file);
            fflush(file);return false;
        }
        const auto written=fwrite(text,1,size,file);bytes+=written;
        if(written!=size){closed=true;fflush(file);return false;}
        if(now-lastFlush>=1000){fflush(file);lastFlush=now;}
        return true;
    }
};
}
