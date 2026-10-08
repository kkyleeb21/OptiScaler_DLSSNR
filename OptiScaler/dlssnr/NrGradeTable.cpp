#include "pch.h"
#include "NrGradeTable.h"
#include "NativeSampler.h"
#include <Config.h>
#include <mutex>
#include <atomic>

namespace DlssNr::Grade {
namespace {
Session session;
std::mutex mutex;
std::atomic<Status> published{Status::Disabled};
struct Memory {
    struct Page {void* address=nullptr;DWORD protection=0;bool pending=false;};
    std::array<Page,2> pages{};
    const wchar_t* path=L"nvngx_dlssnr.dll";
    uintptr_t Current() {return reinterpret_cast<uintptr_t>(GetModuleHandleW(path));}
    bool Pin(uintptr_t base) {
        HMODULE held=nullptr;
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(base),&held)!=0 && reinterpret_cast<uintptr_t>(held)==base;
    }
    void Unpin(uintptr_t base) {FreeLibrary(reinterpret_cast<HMODULE>(base));}
    bool Readable(uintptr_t base,uintptr_t rva,size_t size) {
        auto cursor=base+rva;const auto end=cursor+size;
        while(cursor<end) {
            MEMORY_BASIC_INFORMATION m{};
            if(VirtualQuery(reinterpret_cast<void*>(cursor),&m,sizeof(m))!=sizeof(m) ||
                reinterpret_cast<uintptr_t>(m.AllocationBase)!=base || m.State!=MEM_COMMIT ||
                (m.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
            cursor=std::min(end,reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize);
        }
        return true;
    }
    bool Identity(uintptr_t base) {
        const auto module=reinterpret_cast<HMODULE>(base);
        if(!DlssNrNative::Sampler::isSupportedRuntimeImage(module)) return false;
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        if(nt->OptionalHeader.SizeOfImage<TableRva+sizeof(Table)) return false;
        // Same 310.8 major/minor admission as the forwarder, from the loaded resource,
        // so replacing the disk file cannot change the identity of this module.
        const auto resource=FindResourceW(module,MAKEINTRESOURCEW(1),RT_VERSION);
        const auto bytes=resource?SizeofResource(module,resource):0;
        const auto data=resource?static_cast<const unsigned char*>(LockResource(LoadResource(module,resource))):nullptr;
        constexpr wchar_t key[]=L"VS_VERSION_INFO";
        constexpr size_t fixedOffset=(6+sizeof(key)+3)&~size_t(3);
        if(!data || bytes<fixedOffset+sizeof(VS_FIXEDFILEINFO) ||
           std::memcmp(data+6,key,sizeof(key))!=0) return false;
        VS_FIXEDFILEINFO version{};std::memcpy(&version,data+fixedOffset,sizeof(version));
        if(version.dwSignature!=VS_FFI_SIGNATURE || HIWORD(version.dwFileVersionMS)!=310 ||
           LOWORD(version.dwFileVersionMS)!=8) return false;
        // Also freeze the no-match block (mask + default vector), without changing it.
        if(!Readable(base,DefaultRva,60) || !Readable(base,TableRva,sizeof(Table))) return false;
        uint32_t expected[15]{0,0,0x3f800000};
        return std::memcmp(reinterpret_cast<const void*>(base+DefaultRva),expected,sizeof(expected))==0;
    }
    bool Read(uintptr_t base,Table& table) {
        if(!Readable(base,TableRva,sizeof(table))) return false;
        std::memcpy(table.data(),reinterpret_cast<const void*>(base+TableRva),sizeof(table));return true;
    }
    bool RestoreProtection() {
        bool restored=true;
        for(auto& page:pages) if(page.pending) {
            DWORD ignored=0;
            // Retry immediately once; keep failed pages recorded for the next cleanup attempt.
            if(VirtualProtect(page.address,0x1000,page.protection,&ignored) ||
               VirtualProtect(page.address,0x1000,page.protection,&ignored)) page.pending=false;
            else restored=false;
        }
        return restored;
    }
    bool Write(uintptr_t base,const Table& table) {
        if(!RestoreProtection() || !Readable(base,TableRva,sizeof(table))) return false;
        // The frozen table straddles two 4 KiB pages; preserve each page's own protection.
        const auto first=(base+TableRva)&~uintptr_t(0xfff);
        for(size_t i=0;i<pages.size();++i) {
            auto& page=pages[i];page.address=reinterpret_cast<void*>(first+i*0x1000);
            if(!VirtualProtect(page.address,0x1000,PAGE_READWRITE,&page.protection)) {
                RestoreProtection();return false;
            }
            page.pending=true;
        }
        auto* rows=reinterpret_cast<Row*>(base+TableRva);
        for(size_t i=0;i<table.size();++i) {
            if(std::memcmp(rows+i,&table[i],sizeof(Row))==0) continue;
            // Aligned scalar stores never expose torn/NaN floats to another evaluator.
            // Mask goes to zero during the update, then is published after the coefficients.
            *reinterpret_cast<volatile uint32_t*>(&rows[i].mask)=0;
            for(size_t j=0;j<14;++j)
                *reinterpret_cast<volatile float*>(&rows[i].values[j])=table[i].values[j];
            *reinterpret_cast<volatile uint32_t*>(&rows[i].style)=table[i].style;
            *reinterpret_cast<volatile uint32_t*>(&rows[i].enabled)=table[i].enabled;
            *reinterpret_cast<volatile uint32_t*>(&rows[i].mask)=table[i].mask;
        }
        const bool same=std::memcmp(rows,table.data(),sizeof(table))==0;
        const bool protectedAgain=RestoreProtection();
        return same && protectedAgain;
    }
} memory;
}
Values ReadValues(const Config& c) {
    return Sanitize({c.DlssNrGradeBlack.value_or_default(),c.DlssNrGradeWhite.value_or_default(),
        c.DlssNrGradeExposure.value_or_default(),c.DlssNrGradeGamma.value_or_default(),
        c.DlssNrGradeContrast.value_or_default(),c.DlssNrGradeSaturation.value_or_default(),
        c.DlssNrGradeSaturationGamma.value_or_default(),c.DlssNrGradeTintA.value_or_default(),
        c.DlssNrGradeTintB.value_or_default(),c.DlssNrGradeCurve1.value_or_default(),
        c.DlssNrGradeCurve2.value_or_default(),c.DlssNrGradeCurve3.value_or_default(),
        c.DlssNrGradeCurve4.value_or_default(),c.DlssNrGradeCurve5.value_or_default()});
}
void SetValues(Config& c,const Values& values) {
    const auto v=Sanitize(values);
    c.DlssNrGradeBlack=v[0];c.DlssNrGradeWhite=v[1];c.DlssNrGradeExposure=v[2];c.DlssNrGradeGamma=v[3];
    c.DlssNrGradeContrast=v[4];c.DlssNrGradeSaturation=v[5];c.DlssNrGradeSaturationGamma=v[6];
    c.DlssNrGradeTintA=v[7];c.DlssNrGradeTintB=v[8];c.DlssNrGradeCurve1=v[9];c.DlssNrGradeCurve2=v[10];
    c.DlssNrGradeCurve3=v[11];c.DlssNrGradeCurve4=v[12];c.DlssNrGradeCurve5=v[13];
    published.store(Status::Waiting,std::memory_order_relaxed);
}
void BeforeEvaluate(const Config& c,float firstPassTone,const wchar_t* runtimePath) {
    std::lock_guard<std::mutex> lock(mutex);
    memory.path=runtimePath;
    const bool enabled=c.DlssNrGradeEnabled.value_or_default();
    session.Update(enabled,enabled?ReadValues(c):Defaults,firstPassTone,memory);
    published.store(session.status,std::memory_order_relaxed);
}
void RestoreIfDisabled(const Config& c) {
    if(!c.DlssNrGradeEnabled.value_or_default()) BeforeEvaluate(c,1);
}
void Shutdown(bool processDetach) {
    if(processDetach) {
        // Loader-lock path: no mutex wait, version lookup, module discovery or FreeLibrary.
        // The retained reference keeps the table mapped until process teardown.
        if(session.active && memory.Write(session.base,session.original)) session.active=false;
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    session.Restore(memory);published.store(session.status,std::memory_order_relaxed);
}
Status CurrentStatus() {return published.load(std::memory_order_relaxed);}
} // namespace DlssNr::Grade
