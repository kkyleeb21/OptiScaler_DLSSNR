#pragma once
#include "NrGradeTable.h"
#include "RuntimeProfile.h"
namespace DlssNr::Grade {
// OS supplies discovery, pinning, readable-range checks and per-page protection.
// CPU fixtures use the same transaction on a byte image, with no DLL loading.
template<class Os> struct TableMemory : Os {
    struct Page {uintptr_t address=0;uint32_t protection=0;bool pending=false;};
    std::array<Page,4> pages{};
    Runtime::GradeLayout layout{};
    bool DefaultsMatch(uintptr_t base) {
        if(layout.count==0 || layout.count>2)return false;
        const uint32_t expected[15]{0,0,0x3f800000};
        for(size_t i=0;i<layout.count;++i)
            if(!this->Readable(base,layout.defaults[i],sizeof(expected)) ||
               !this->Readable(base,layout.tables[i],sizeof(Table)) ||
               std::memcmp(reinterpret_cast<const void*>(base+layout.defaults[i]),expected,sizeof(expected)))return false;
        return layout.count>0 && layout.count<=2;
    }
    bool Identity(uintptr_t base) {
        const auto* profile=this->Profile(base);
        if(!profile || !profile->GradeSupported())return false;
        layout=profile->grade;
        return DefaultsMatch(base);
    }
    bool Read(uintptr_t base,Table& table) {
        if(!DefaultsMatch(base))return false;
        for(size_t i=0;i<layout.count;++i)
            if(std::memcmp(reinterpret_cast<const void*>(base+layout.tables[i]),ExpectedWords.data(),sizeof(Table)))return false;
        std::memcpy(table.data(),reinterpret_cast<const void*>(base+layout.tables[0]),sizeof(table));return true;
    }
    bool RestoreProtection() {
        bool restored=true;
        for(auto& page:pages)if(page.pending) {
            uint32_t ignored=0;
            if(this->Protect(page.address,page.protection,ignored) || this->Protect(page.address,page.protection,ignored))page.pending=false;
            else restored=false;
        }
        return restored;
    }
    bool Write(uintptr_t base,const Table& table) {
        if(!RestoreProtection() || !DefaultsMatch(base))return false;
        size_t count=0;
        for(size_t t=0;t<layout.count;++t) {
            const auto first=(base+layout.tables[t])&~uintptr_t(0xfff);
            const auto last=(base+layout.tables[t]+sizeof(Table)-1)&~uintptr_t(0xfff);
            for(auto address=first;address<=last;address+=0x1000) {
                bool found=false;for(size_t i=0;i<count;++i)found|=pages[i].address==address;
                if(!found)pages[count++]={address,0,false};
            }
        }
        for(size_t i=0;i<count;++i) {
            auto& page=pages[i];
            if(!this->Protect(page.address,0x04,page.protection)){RestoreProtection();return false;}
            page.pending=true;
        }
        bool same=true;
        for(size_t t=0;t<layout.count;++t) {
            auto* rows=reinterpret_cast<Row*>(base+layout.tables[t]);
            for(size_t i=0;i<table.size();++i) {
                if(!std::memcmp(rows+i,&table[i],sizeof(Row)))continue;
                // Caller holds the common grade/Evaluate mutex across both tables.
                *reinterpret_cast<volatile uint32_t*>(&rows[i].mask)=0;
                for(size_t j=0;j<14;++j)*reinterpret_cast<volatile float*>(&rows[i].values[j])=table[i].values[j];
                *reinterpret_cast<volatile uint32_t*>(&rows[i].style)=table[i].style;
                *reinterpret_cast<volatile uint32_t*>(&rows[i].enabled)=table[i].enabled;
                *reinterpret_cast<volatile uint32_t*>(&rows[i].mask)=table[i].mask;
            }
            same &= std::memcmp(rows,table.data(),sizeof(table))==0;
        }
        const bool protectedAgain=RestoreProtection();return same && protectedAgain;
    }
};
}
