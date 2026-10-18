#include "pch.h"
#include "GradeMemory.h"
#include "NativeSampler.h"
#include <Config.h>
#include <mutex>
#include <atomic>

namespace DlssNr::Grade {
namespace {
Session session;
std::mutex mutex;
std::atomic<Status> published{Status::Disabled};
std::atomic<uint64_t> requestGeneration{0},publishedGeneration{0};
struct Memory {
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
    const Runtime::Profile* Profile(uintptr_t base) {return Runtime::FromModule(reinterpret_cast<HMODULE>(base));}
    bool Protect(uintptr_t address,uint32_t protection,uint32_t& old) {
        DWORD prior=0;const bool ok=VirtualProtect(reinterpret_cast<void*>(address),0x1000,protection,&prior)!=0;
        old=prior;return ok;
    }
};
TableMemory<Memory> memory;
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
    RequestUpdate();
}
std::unique_lock<std::mutex> BeforeEvaluate(const Config& c,float firstPassTone,const wchar_t* runtimePath) {
    std::unique_lock<std::mutex> lock(mutex);
    const auto generation=requestGeneration.load();
    memory.path=runtimePath;
    const bool enabled=c.DlssNrGradeEnabled.value_or_default();
    session.Update(enabled,enabled?ReadValues(c):Defaults,firstPassTone,memory);
    publishedGeneration.store(generation);
    published.store(generation==requestGeneration.load()?session.status:Status::Waiting,std::memory_order_relaxed);
    return lock; // Caller retains ownership through the actual Evaluate.
}
void RequestUpdate() {
    ++requestGeneration;published.store(Status::Waiting,std::memory_order_relaxed);
}
void RestoreIfDisabled(const Config& c) {
    if(!c.DlssNrGradeEnabled.value_or_default() || !c.DlssNrEnabled.value_or_default()) {
        std::lock_guard<std::mutex> lock(mutex);
        session.Update(false,Defaults,1,memory);
        published.store(session.status,std::memory_order_relaxed);
    }
}
void Shutdown(bool processDetach) {
    if(processDetach) {
        // Loader-lock path: no mutex wait, version lookup, module discovery or FreeLibrary.
        // The retained reference keeps the table mapped until process teardown.
        if((session.active || session.pending) && memory.Write(session.base,session.original)) {session.active=false;session.pending=false;}
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    session.Restore(memory);published.store(session.status,std::memory_order_relaxed);
}
Status CurrentStatus() {
    const auto status=published.load(std::memory_order_relaxed);
    return status==Status::Applied && publishedGeneration.load()!=requestGeneration.load()?Status::Waiting:status;
}
} // namespace DlssNr::Grade
