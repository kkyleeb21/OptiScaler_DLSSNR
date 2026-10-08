#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <mutex>
#include <bit>

class Config;
namespace DlssNr::Grade {
inline constexpr uintptr_t TableRva = 0xb0de4, DefaultRva = 0xb0da8;
using Values = std::array<float, 14>;
inline constexpr Values Defaults{0,1,0,0,0,0,0,0,0,0,0,0,0,0};
inline constexpr Values Minimum{0,.5f,-2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
inline constexpr Values Maximum{.5f,2,2,1,1,1,1,1,1,1,1,1,1,1};
struct Row { uint32_t enabled, style, mask; Values values; };
using Table = std::array<Row, 8>;
static_assert(sizeof(float)==4 && sizeof(Row)==0x44 && offsetof(Row,values)==12);
// Exact little-endian 310.8 bytes, including all inactive slots and rows.
inline constexpr std::array<std::array<uint32_t,17>,8> ExpectedWords{{
    {1,1,0x34,0,0x3f800000,0xbdcccccd,0,0xbe800000,0xbdcccccd,0,0,0,0,0,0,0,0},
    {1,2,0x20,0,0x3f800000,0,0,0,0xbe19999a,0,0,0,0,0,0,0,0},
    {0,0,0,0,0x3f800000,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0x3f800000,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0x3f800000,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0x3f800000,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0x3f800000,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0x3f800000,0,0,0,0,0,0,0,0,0,0,0,0}
}};
inline Table Original() { Table t; std::memcpy(t.data(),ExpectedWords.data(),sizeof(t)); return t; }
inline Values Sanitize(Values v) {
    for(size_t i=0;i<v.size();++i) v[i]=std::isfinite(v[i])?std::clamp(v[i],Minimum[i],Maximum[i]):Defaults[i];
    return v;
}
inline float Tone(float t) { return std::isfinite(t)?std::clamp(t,0.f,1.f):0.f; }
inline Values Preset(unsigned preset) {
    auto v=Defaults;
    if(preset==1) { v[2]=-.1f;v[4]=-.25f;v[5]=-.1f; }
    if(preset==2) v[5]=-.15f;
    return v;
}
inline constexpr float MinimumTone=.05f, MinimumWhiteGap=1e-4f;
inline Table Pack(Values v,float localTone) {
    v=Sanitize(v); const float t=(std::max)(Tone(localTone),MinimumTone);
    uint32_t mask=0;Values table=Defaults;
    for(size_t i=0;i<v.size();++i) {
        if(v[i]!=Defaults[i]) mask|=1u<<i;
        table[i]=Defaults[i]+(v[i]-Defaults[i])/t;
    }
    auto rows=Original();
    for(unsigned i=0;i<3;++i) rows[i]={1,i==2?0u:i+1,mask,table};
    return rows;
}
inline bool Legal(const Table& table) {
    for(unsigned row=0;row<3;++row) {
        const auto& v=table[row].values;
        for(float value:v) if((std::bit_cast<uint32_t>(value)&0x7f800000u)==0x7f800000u)return false;
        if(v[1]-v[0]<MinimumWhiteGap)return false;
    }
    return true;
}
enum class Status { Disabled, Waiting, Applied, Unsupported, LowTone, InvalidValues, WriteFailed };
// One small state machine also used by the CPU fixture. Memory owns the OS operations.
// A retained module reference prevents unload while the table is modified.
struct Session {
    uintptr_t base=0;
    Table original{},last{};
    bool active=false,pending=false;
    Status status=Status::Disabled;
    uint64_t disabledBypasses=0,reads=0,writes=0;
    template<class Memory> bool Restore(Memory& memory) {
        if(!base) {status=Status::Disabled;return true;}
        if(active || pending) {
            pending=true;++writes;
            if(!memory.Write(base,original)) {status=Status::WriteFailed;return false;}
            active=false;pending=false;
        }
        memory.Unpin(base);base=0;status=Status::Disabled;
        return true;
    }
    template<class Memory> void Update(bool enabled,Values v,float tone,Memory& memory) {
        // Crucially before Current/Identity/Read/Write: dormant means zero runtime access.
        if(!enabled) {if(!base){++disabledBypasses;status=Status::Disabled;}else Restore(memory);return;}
        const auto wanted=Pack(v,tone);
        const auto invalid=Tone(tone)<MinimumTone?Status::LowTone:
            !Legal(wanted)?Status::InvalidValues:Status::Applied;
        if(invalid!=Status::Applied) {
            // A failed write may have changed bytes: recover the last legal state first.
            if(pending) {
                ++writes;
                if(!memory.Write(base,last)) {status=Status::WriteFailed;return;}
                pending=false;
            }
            status=invalid;return;
        }
        const auto current=memory.Current();
        if(base && current!=base && !Restore(memory)) return;
        if(!current) {status=Status::Waiting;return;}
        if(!base) {
            if(!memory.Pin(current)) {status=Status::Waiting;return;}
            Table candidate{};
            ++reads;
            if(!memory.Identity(current) || !memory.Read(current,candidate) ||
               std::memcmp(candidate.data(),ExpectedWords.data(),sizeof(candidate))!=0) {
                memory.Unpin(current);status=Status::Unsupported;return;
            }
            base=current;original=candidate;last=candidate;
        }
        if(pending || std::memcmp(last.data(),wanted.data(),sizeof(wanted))!=0) {
            // Write may succeed but protection restoration fail: retain backup/reference for retry.
            active=true;pending=true;++writes;
            if(!memory.Write(base,wanted)) {status=Status::WriteFailed;return;}
            last=wanted;pending=false;
        }
        status=Status::Applied;
    }
};
Values ReadValues(const Config& config);
void SetValues(Config& config,const Values& values);
[[nodiscard]] std::unique_lock<std::mutex> BeforeEvaluate(const Config& config,float firstPassTone,const wchar_t* runtimePath=L"nvngx_dlssnr.dll");
void RequestUpdate(); // Menu only publishes intent; never waits for the table/Evaluate lock.
void RestoreIfDisabled(const Config& config);
void Shutdown(bool processDetach=false);
Status CurrentStatus();
} // namespace DlssNr::Grade
