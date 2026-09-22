#pragma once
#include <windows.h>
#include <dlssnr/BuildProfile.h>
#include <filesystem>
#include <string>
#include <cstddef>
#include <cstdint>
#include "../research_gate.h"
namespace D18PhfExperiment {
static_assert(sizeof(DlssNrConstants)>=176); // GPU allocation has alignment padding.
static_assert(offsetof(DlssNrConstants,RelativeColour)+sizeof(uint32_t)==176);
static_assert(offsetof(DlssNrConstants,PreserveHighFrequency)==76);
inline std::wstring markerName(DWORD pid,uint64_t creation){
 return L"D18_S0_R0_"+std::to_wstring(pid)+L"_"+std::to_wstring(creation)+L".on";
}
inline uint64_t processCreation(){
 FILETIME c{},e{},k{},u{};
 if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))return 0;
 return (uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
}
inline bool apply(DlssNrConstants& c,bool enabled) noexcept {
 return d18S0ApplyR0(c,enabled);
}
class Switch {
 std::filesystem::path path_;uint64_t creation_=0,last_=0;bool first_=true,enabled_=false;
public:
 explicit Switch(const std::filesystem::path& root):creation_(processCreation()){
  path_=root/markerName(GetCurrentProcessId(),creation_);
 }
 bool refresh(uint64_t now,bool busy){
  if(!DlssNr::BuildProfile::Diagnostic){enabled_=false;return false;}
  if(busy||(!first_&&now-last_<250))return false;
  const DWORD attrs=creation_?GetFileAttributesW(path_.c_str()):INVALID_FILE_ATTRIBUTES;
  const bool next=attrs!=INVALID_FILE_ATTRIBUTES&&!(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
  const bool changed=first_||next!=enabled_;first_=false;last_=now;enabled_=next;return changed;
 }
 bool enabled() const noexcept{return enabled_;}
 uint64_t creation() const noexcept{return creation_;}
 const std::filesystem::path& path() const noexcept{return path_;}
};
}
