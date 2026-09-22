#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <string>

// Optional extension. The original CaptureCommand/Status and Settings ABI do not change.
namespace DlssNrNative::Full8 {
inline constexpr uint32_t Target=8, MaxWidth=3840, MaxHeight=2160;
inline constexpr uint64_t RequestBytes=4ull*1024*1024*1024;
struct Command {
 uint32_t size=sizeof(Command),version=1;
 uint64_t request=0;
 uint32_t armed=0,target=Target,profile=1,pid=0;
 uint64_t creation=0;
 uint32_t diagnostics=0,reserved=0;
};
inline uint64_t Creation(){FILETIME c{},e{},k{},u{};
 return GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u)?(uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime:0;}
inline std::wstring Name(uint32_t pid,uint64_t creation){
 return L"D18_CAPTURE_FULL8_"+std::to_wstring(pid)+L"_"+std::to_wstring(creation)+L".on";}
inline bool RegularAttributes(DWORD a){return a!=INVALID_FILE_ATTRIBUTES&&!(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));}
inline bool Marker(const std::filesystem::path& root,uint32_t pid,uint64_t creation){
 return pid&&creation&&RegularAttributes(GetFileAttributesW((root/Name(pid,creation)).c_str()));}
inline bool AtModule(){
 HMODULE module=nullptr;wchar_t path[32768]{};
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
    reinterpret_cast<LPCWSTR>(&AtModule),&module))return false;
 const DWORD count=GetModuleFileNameW(module,path,32768);
 return count&&count<32768&&Marker(std::filesystem::path(path).parent_path(),GetCurrentProcessId(),Creation());
}
inline bool DiagnosticsEnabled(uint32_t value){return value==1||value==2;}
inline bool ValidCommand(const Command& c,uint32_t pid,uint64_t creation){
 return c.size==sizeof(Command)&&c.version==1&&c.request>0&&c.armed<=1&&c.target==Target&&c.profile==1&&
        pid&&creation&&c.pid==pid&&c.creation==creation&&c.reserved==0&&c.diagnostics<=2&&
        (!c.armed||DiagnosticsEnabled(c.diagnostics));
}
inline bool StatusMatches(uint32_t expected,uint32_t actual,uint32_t selected,uint32_t saved,bool complete){
 return (expected==32||expected==Target)&&actual==expected&&selected<=expected&&saved<=selected&&(!complete||(saved==expected&&selected==expected));
}
inline bool FullColourBounds(uint32_t width,uint32_t height,uint32_t outputWidth,uint32_t outputHeight,uint32_t bpp){
 return width&&height&&width<=MaxWidth&&height<=MaxHeight&&width==outputWidth&&height==outputHeight&&
        (bpp==4||bpp==8||bpp==16);
}
inline bool FitsBudget(uint64_t written,uint64_t bytes){return written<=RequestBytes&&bytes<=RequestBytes-written;}
}
