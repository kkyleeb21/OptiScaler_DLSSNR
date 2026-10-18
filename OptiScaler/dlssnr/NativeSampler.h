#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include "RuntimeProfile.h"
namespace DlssNrNative::Sampler {

inline bool isSupportedRuntimeImage(HMODULE module) {
    const auto* profile=DlssNr::Runtime::FromModule(module);
    return profile && profile->SamplerSupported();
}

// One active Runtime, with pending protection owned by its loader reference. Keeping
// that reference until cleanup prevents unload/reload at the same address (ABA).
// A changed/null argument must still clean live old pages before any new work.
// Only an address lookup proving a page no longer belongs to that image may drop it.
struct ProtectionState {
    struct Page { uintptr_t address=0; DWORD original=0; bool pending=false; };
    HMODULE module=nullptr;
    Page pages[6]{};
    size_t count=0;
    bool Pending() const { for(const auto& page:pages)if(page.pending)return true;return false; }
    void Discard() {
        auto old=module;module=nullptr;count=0;
        for(auto& page:pages)page={};
        // A forced unmap can invalidate our reference; never release a stale handle
        // (or a different image now occupying an old page).
        HMODULE mapped=nullptr;
        if(old && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCWSTR>(old),&mapped) && mapped==old)
            FreeLibrary(old);
    }
    bool Restore() {
        bool ok=true;
        for(size_t i=count;i-->0;)if(pages[i].pending) {
            DWORD ignored=0;
            if(VirtualProtect(reinterpret_cast<void*>(pages[i].address),0x1000,pages[i].original,&ignored))
                pages[i].pending=false;
            else ok=false;
        }
        return ok;
    }
    bool Begin(HMODULE current) {
        // Inspect the recorded addresses, not the caller's discovery result. A null
        // or different current module says nothing about the old image's lifetime.
        bool lostPage=false;
        for(size_t i=0;i<count;++i)if(pages[i].pending) {
            HMODULE mapped=nullptr;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR>(pages[i].address),&mapped) || mapped!=module)
                {pages[i].pending=false;lostPage=true;}
        }
        // Failed cleanup retains the old module, original protection and reference,
        // blocking even null/no-op calls and all writes to a replacement module.
        if(!Restore())return false;
        if(lostPage || module!=current)Discard();
        if(!current)return true;
        if(!module) {
            HMODULE pinned=nullptr;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                  reinterpret_cast<LPCWSTR>(current),&pinned))return false;
            if(pinned!=current) {FreeLibrary(pinned);return false;}
            module=pinned;
        }
        count=0;for(auto& page:pages)page={};
        return true;
    }
    bool Add(uintptr_t address,size_t length) {
        const auto last=(address+length-1)&~uintptr_t(0xfff);
        for(auto p=address&~uintptr_t(0xfff);p<=last;p+=0x1000) {
            bool found=false;for(size_t i=0;i<count;++i)found|=pages[i].address==p;
            if(!found) {if(count==_countof(pages))return false;pages[count++]={p,0,false};}
        }
        return true;
    }
    bool Acquire() {
        // Begin must have cleared every pending page; never capture residual RWX.
        if(Pending())return false;
        for(size_t i=0;i<count;++i) {
            auto& page=pages[i];
            if(!VirtualProtect(reinterpret_cast<void*>(page.address),0x1000,
                               PAGE_EXECUTE_READWRITE,&page.original)) {Restore();return false;}
            page.pending=true;
        }
        return true;
    }
};
inline std::mutex protectionMutex;
inline ProtectionState protectionState;
struct ProtectionCall {
    bool ready;
    explicit ProtectionCall(HMODULE module):ready(protectionState.Begin(module)) {}
    ~ProtectionCall() {if(!protectionState.Pending())protectionState.Discard();}
};

// New profiles guard the entire selection block plus its stack/argument transfers.
inline bool ApplyModernTransaction(HMODULE module,const DlssNr::Runtime::Profile& profile,bool linearResolve,int colorMode) {
    const auto& layout=profile.sampler;
    if(colorMode<0 || colorMode>2 || layout.length!=59)return false;
    auto* base=reinterpret_cast<unsigned char*>(module);
    DlssNr::Runtime::View view{base,0x2000,true};
    view.size=view.Read<uint32_t>(view.Pe()+24+56);
    unsigned char point[59],linear[59];
    std::memcpy(point,layout.selection,59);
    // JMP 0x22058 then padding replaces the complete 8-byte flag check.
    // MOV EDX,2 (or 0) is shared by all inputs and feeds both calls via [RSP+0xa8].
    const unsigned char jump[]={0xeb,0x34,0x90,0x90,0x90,0x90,0x90,0x90};
    std::memcpy(point,jump,sizeof(jump));std::memcpy(linear,point,59);linear[55]=0;
    const unsigned char resolve[]={0x45,0x8b,0xce},resolveLinear[]={0x45,0x33,0xc9};
    const unsigned char first[]={0x89,0x94,0x24,0xa8,0,0,0,0x48,0x8b,0x0f,0x48,0x8b,0x01,0x44,0x8b,0xca};
    const unsigned char retry[]={0x44,0x8b,0x8c,0x24,0xa8,0,0,0};
    if(!view.Range(layout.first-13,sizeof(first)) || !view.Range(layout.retry,sizeof(retry)) ||
       std::memcmp(base+layout.first-13,first,sizeof(first)) || std::memcmp(base+layout.retry,retry,sizeof(retry)))return false;
    struct Site {uint32_t rva;size_t n;const unsigned char* wanted;unsigned char before[59];bool changed=false;};
    Site sites[]={{layout.resolve,3,linearResolve?resolveLinear:resolve,{}},
                  {layout.color,59,colorMode==1?linear:colorMode==2?point:reinterpret_cast<const unsigned char*>(layout.selection),{}}};
    // All guards precede any protection change or write, including Follow/restore.
    for(auto& site:sites) {
        if(!view.Range(site.rva,site.n))return false;
        auto* at=base+site.rva;
        if(site.n==3) {if(std::memcmp(at,resolve,3)&&std::memcmp(at,resolveLinear,3))return false;}
        else if(std::memcmp(at,layout.selection,59)&&std::memcmp(at,point,59)&&std::memcmp(at,linear,59))return false;
        std::memcpy(site.before,at,site.n);site.changed=std::memcmp(at,site.wanted,site.n)!=0;
    }
    for(auto& site:sites)if(site.changed)
        if(!protectionState.Add(reinterpret_cast<uintptr_t>(base+site.rva),site.n))return false;
    if(!protectionState.Acquire())return false;
    bool ok=true;
    for(auto& site:sites)if(site.changed) {
        std::memcpy(base+site.rva,site.wanted,site.n);
        ok=bool(FlushInstructionCache(GetCurrentProcess(),base+site.rva,site.n))&&ok;
        ok=(std::memcmp(base+site.rva,site.wanted,site.n)==0)&&ok;
    }
    if(!ok)for(auto& site:sites)if(site.changed) {
        std::memcpy(base+site.rva,site.before,site.n);FlushInstructionCache(GetCurrentProcess(),base+site.rva,site.n);
    }
    return protectionState.Restore()&&ok;
}

inline bool ApplyModern(HMODULE module,const DlssNr::Runtime::Profile& profile,bool linearResolve,int colorMode) {
    std::lock_guard<std::mutex> lock(protectionMutex);
    ProtectionCall call(module);
    if(!call.ready || !module)return false;
    return ApplyModernTransaction(module,profile,linearResolve,colorMode);
}

// Runtime 310.8 normally selects POINT for both the network answer and the model Color input. These
// exact signatures make the two sampling experiments reversible without modifying the file on disk.
inline bool Apply(HMODULE module, bool linearResolve, int colorMode) {
    std::lock_guard<std::mutex> lock(protectionMutex);
    ProtectionCall call(module);
    if(!call.ready)return false;
    if (!module) {
        return true;
    }

    const auto* profile=DlssNr::Runtime::FromModule(module);
    if (!profile) return false;
    if (!profile->SamplerSupported()) return false;
    if (!profile->legacy3108) return ApplyModernTransaction(module,*profile,linearResolve,colorMode);
    const bool linearColorInput=colorMode!=0; // Existing 310.8 boolean interpretation.

    struct SamplerPatch {
        uintptr_t rva;
        unsigned char point[6];
        unsigned char linear[6];
        size_t length;
        bool useLinear;
    };

    const SamplerPatch patches[] = {
        { 0x1CAF2, { 0x45, 0x8B, 0xCE, 0, 0, 0 }, { 0x45, 0x33, 0xC9, 0, 0, 0 }, 3,
          linearResolve },
        { 0x21DB4, { 0x41, 0xB9, 0x02, 0, 0, 0 }, { 0x41, 0xB9, 0, 0, 0, 0 }, 6,
          linearColorInput },
        { 0x21DDC, { 0x41, 0xB9, 0x02, 0, 0, 0 }, { 0x41, 0xB9, 0, 0, 0, 0 }, 6,
          linearColorInput },
    };

    auto *base = reinterpret_cast<unsigned char *>(module);
    if (!isSupportedRuntimeImage(module)) {
        return false;
    }

    struct PatchTransaction {
        unsigned char original[6] {};
        bool changed = false;
    };
    PatchTransaction transaction[_countof(patches)] {};

    // Validate every byte range and retain its exact starting state before making any page writable.
    for (const auto &patch : patches) {
        const auto *address = base + patch.rva;
        if (std::memcmp(address, patch.point, patch.length) != 0 &&
            std::memcmp(address, patch.linear, patch.length) != 0) {
            return false;
        }
    }

    for (size_t i = 0; i < _countof(patches); ++i) {
        const auto &patch = patches[i];
        auto *address = base + patch.rva;
        const auto *wanted = patch.useLinear ? patch.linear : patch.point;
        std::memcpy(transaction[i].original, address, patch.length);
        transaction[i].changed = std::memcmp(address, wanted, patch.length) != 0;
        if (!transaction[i].changed) {
            continue;
        }

        if(!protectionState.Add(reinterpret_cast<uintptr_t>(address),patch.length))return false;
    }
    if(!protectionState.Acquire())return false;

    bool committed = true;
    for (size_t i = 0; i < _countof(patches); ++i) {
        const auto &patch = patches[i];
        if (!transaction[i].changed) {
            continue;
        }
        auto *address = base + patch.rva;
        const auto *wanted = patch.useLinear ? patch.linear : patch.point;
        std::memcpy(address, wanted, patch.length);
        if (!FlushInstructionCache(GetCurrentProcess(), address, patch.length) ||
            std::memcmp(address, wanted, patch.length) != 0) {
            committed = false;
        }
    }

    if (!committed) {
        for (size_t i = 0; i < _countof(patches); ++i) {
            if (transaction[i].changed) {
                std::memcpy(base + patches[i].rva, transaction[i].original, patches[i].length);
                FlushInstructionCache(GetCurrentProcess(), base + patches[i].rva, patches[i].length);
            }
        }
    }

    const bool protectionsRestored = protectionState.Restore();
    return committed && protectionsRestored;
}


}
