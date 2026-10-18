#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace DlssNr::Runtime {
// State bits: original/registered compatible = 1, replacement = 2. A byte
// sequence shared by both states carries both bits and does not decide the state.
struct Bytes { const char* data; size_t size; unsigned states; };
struct Guard { uint32_t offset; int64_t rva; uint32_t skip; bool append; const Bytes* values; size_t count; };
struct GradeLayout { uint32_t tables[2],defaults[2]; size_t count; };
struct SamplerLayout { uint32_t resolve,color; const char* selection; size_t length; uint32_t first,retry; };
struct Profile {
    const char* id;
    uint32_t versionMS,versionLS;
    bool legacy3108,experimental;
    const Guard* guards; size_t guardCount;
    GradeLayout grade;
    SamplerLayout sampler;
    bool GradeSupported() const { return grade.count!=0; }
    bool ModelSupported() const { return !legacy3108; }
    bool SamplerSupported() const { return sampler.resolve!=0; }
    bool Dx11Supported() const { return legacy3108; }
    bool NativeDimensions() const { return !legacy3108; }
};
}
#include "RuntimeProfileData.h"
namespace DlssNr::Runtime {
// The same parser validates raw bytes in the CPU checker and already-mapped images
// in production. It never loads a DLL, invokes an export or hashes an entire file.
struct View {
    const unsigned char* data; size_t size; bool mapped=false;
    bool Range(size_t off,size_t length) const {
        if(off>size || length>size-off)return false;
        if(!mapped)return true;
        auto p=reinterpret_cast<uintptr_t>(data+off),end=p+length;
        while(p<end) {
            MEMORY_BASIC_INFORMATION m{};
            if(VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof(m))!=sizeof(m) ||
               m.AllocationBase!=data || m.State!=MEM_COMMIT || (m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
            p=(std::min)(end,reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize);
        }
        return true;
    }
    template<class T> T Read(size_t off) const {
        T value{};if(Range(off,sizeof(T)))std::memcpy(&value,data+off,sizeof(T));return value;
    }
    uint32_t Pe() const { return Read<uint32_t>(60); }
    bool Valid() const {
        const auto pe=Pe();
        if(Read<uint16_t>(0)!=0x5a4d || pe<64 || pe>0x1000 || !Range(pe,264) ||
           Read<uint32_t>(pe)!=0x4550 || Read<uint16_t>(pe+4)!=0x8664 ||
           Read<uint16_t>(pe+20)!=240 || Read<uint16_t>(pe+24)!=0x20b)return false;
        const auto count=Read<uint16_t>(pe+6);
        return count>0 && count<=96 && Range(pe+264,size_t(count)*40);
    }
    size_t Rva(uint32_t rva,size_t length) const {
        if(mapped)return Range(rva,length)?rva:SIZE_MAX;
        const auto pe=Pe(),headers=Read<uint32_t>(pe+24+60);
        if(rva<headers && length<=headers-rva && Range(rva,length))return rva;
        const auto count=Read<uint16_t>(pe+6);
        for(unsigned i=0;i<count;++i) {
            const auto off=pe+264+i*40,va=Read<uint32_t>(off+12),raw=Read<uint32_t>(off+20),n=Read<uint32_t>(off+16);
            if(rva>=va && rva-va<=n && length<=n-(rva-va)) {
                const auto at=size_t(raw)+rva-va;
                if(Range(at,length))return at;
            }
        }
        return SIZE_MAX;
    }
    bool HasSection(uint32_t rva) const {
        for(unsigned i=0;i<Read<uint16_t>(Pe()+6);++i)
            if(Read<uint32_t>(Pe()+264+i*40+12)==rva)return true;
        return false;
    }
    bool AppendSection(const Guard& g,size_t length) const {
        if(g.rva<0 || g.skip>length)return false;
        const auto n=length-g.skip;
        const auto imageSize=Read<uint32_t>(Pe()+24+56);
        if(uint64_t(g.rva)+n>imageSize)return false;
        for(unsigned i=0;i<Read<uint16_t>(Pe()+6);++i) {
            const auto at=Pe()+264+i*40;
            if(Read<uint32_t>(at+12)!=uint32_t(g.rva))continue;
            const auto virtualSize=Read<uint32_t>(at+8),rawSize=Read<uint32_t>(at+16);
            const auto raw=Read<uint32_t>(at+20),flags=Read<uint32_t>(at+36);
            // 310.8's .d16 has 0xac meaningful bytes in a 0x200 raw section;
            // validate the full replacement, including its mapped raw padding.
            return raw==uint64_t(g.offset)+g.skip && rawSize==n && virtualSize>0 &&
                   uint64_t(g.rva)+virtualSize<=imageSize && (flags&0xe0000000u)==0x60000000u &&
                   (mapped?Range(size_t(g.rva),n):Range(raw,n));
        }
        return false;
    }
    bool Version(uint32_t& ms,uint32_t& ls) const {
        const auto dir=Read<uint32_t>(Pe()+24+112+16),bytes=Read<uint32_t>(Pe()+24+112+20);
        if(!dir || bytes<16)return false;
        uint32_t sub=0;
        // RT_VERSION -> resource id -> language. Every relative offset is bounded.
        for(unsigned level=0;level<3;++level) {
            if(sub>bytes || bytes-sub<16)return false;
            const auto at=Rva(dir+sub,16);if(at==SIZE_MAX)return false;
            const auto count=unsigned(Read<uint16_t>(at+12))+Read<uint16_t>(at+14);
            if(count>4096 || size_t(count)*8>bytes-sub-16)return false;
            bool found=false;
            for(unsigned i=0;i<count;++i) {
                const auto e=Rva(dir+sub+16+i*8,8);if(e==SIZE_MAX)return false;
                if(level==0 && Read<uint32_t>(e)!=16)continue;
                const auto next=Read<uint32_t>(e+4);
                if((level<2)!=bool(next&0x80000000u))return false;
                sub=next&0x7fffffffu;found=true;break;
            }
            if(!found)return false;
        }
        if(sub>bytes || bytes-sub<16)return false;
        const auto e=Rva(dir+sub,16);if(e==SIZE_MAX)return false;
        const auto n=Read<uint32_t>(e+4);const auto at=Rva(Read<uint32_t>(e),n);
        constexpr wchar_t key[]=L"VS_VERSION_INFO";
        constexpr size_t fixed=(6+sizeof(key)+3)&~size_t(3);
        if(at==SIZE_MAX || n<fixed+52 || std::memcmp(data+at+6,key,sizeof(key)) || Read<uint32_t>(at+fixed)!=0xfeef04bd)return false;
        ms=Read<uint32_t>(at+fixed+8);ls=Read<uint32_t>(at+fixed+12);return true;
    }
};
inline const Profile* Identify(const View& view) {
    if(!view.Valid())return nullptr;
    uint32_t ms=0,ls=0;if(!view.Version(ms,ls))return nullptr;
    for(const auto& p:Profiles) {
        if(ms!=p.versionMS || ls!=p.versionLS)continue;
        unsigned states=3;
        for(size_t i=0;i<p.guardCount && states;++i) {
            const auto& g=p.guards[i];
            if(view.mapped && g.rva<0)continue; // Loader-owned CFG pointer: file check only.
            unsigned matches=0;
            for(size_t j=0;j<g.count;++j) {
                auto v=g.values[j];
                if(!v.size) {
                    // Absence belongs only to the original state; an in-range RVA
                    // alone never proves that the appended section is in the PE table.
                    if(g.append && !view.HasSection(uint32_t(g.rva)) &&
                       (view.mapped || view.size==g.offset))matches|=v.states;
                    continue;
                }
                if(g.append && !view.AppendSection(g,v.size))continue;
                if(view.mapped && g.skip) {if(g.skip>v.size)continue;v.data+=g.skip;v.size-=g.skip;}
                const auto off=view.mapped?size_t(g.rva):g.offset;
                if(view.Range(off,v.size) && std::memcmp(view.data+off,v.data,v.size)==0)matches|=v.states;
            }
            states&=matches;
        }
        if(states)return &p;
    }
    return nullptr;
}
inline const Profile* FromFile(const wchar_t* path) {
    if(!path || !*path)return nullptr;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return nullptr;
    LARGE_INTEGER size{};const Profile* profile=nullptr;
    if(GetFileSizeEx(file,&size) && size.QuadPart>0 && size.QuadPart<=512ll*1024*1024) {
        HANDLE mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr);
        if(mapping) {
            auto* bytes=static_cast<const unsigned char*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0));
            if(bytes){profile=Identify({bytes,size_t(size.QuadPart),false});UnmapViewOfFile(bytes);}
            CloseHandle(mapping);
        }
    }
    CloseHandle(file);return profile;
}
inline const Profile* FromModule(HMODULE module) {
    if(!module)return nullptr;
    // Validate the header before deriving the mapped size.
    View header{reinterpret_cast<const unsigned char*>(module),0x2000,true};
    if(!header.Valid())return nullptr;
    const auto size=header.Read<uint32_t>(header.Pe()+24+56);
    if(size<0x2000 || size>512u*1024*1024)return nullptr;
    return Identify({header.data,size,true});
}
inline uint32_t Network(const Profile* profile,uint32_t size,float ratio,uint32_t alignment) {
    if(profile && profile->NativeDimensions()) {
        ratio=ratio>=.5f?(std::min)(ratio,1.f):.5f;
        if(ratio>=.9999f)return size; // Runtime float32 constant at RVA 0xdad8c.
    }
    const auto rounded=uint32_t(std::floor(size*ratio+.5f));
    return profile && profile->NativeDimensions()?(std::max)(1u,rounded):(std::max)(alignment,rounded&~(alignment-1));
}
} // namespace DlssNr::Runtime
