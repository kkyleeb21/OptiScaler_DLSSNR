#pragma once
#include <windows.h>
#include "BuildProfile.h"
#include <filesystem>
#include <string>
// A marker for another process incarnation cannot activate this core. No marker is shipped.
inline std::filesystem::path S0VkMarkerPath(const std::filesystem::path& coreDirectory) {
    FILETIME creation{},exitTime{},kernel{},user{};
    if(!GetProcessTimes(GetCurrentProcess(),&creation,&exitTime,&kernel,&user)) return {};
    const unsigned long long stamp=(static_cast<unsigned long long>(creation.dwHighDateTime)<<32)|creation.dwLowDateTime;
    return coreDirectory/(L"D18_S0_VK_R0_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(stamp)+L".on");
}
inline bool S0VkR0Requested(const std::filesystem::path& coreDirectory) {
    if(!DlssNr::BuildProfile::Diagnostic)return false;
    static ULONGLONG nextCheck=0;static bool requested=false;
    const auto now=GetTickCount64();if(now<nextCheck)return requested;nextCheck=now+250;
    const auto marker=S0VkMarkerPath(coreDirectory);
    const auto attributes=marker.empty()?INVALID_FILE_ATTRIBUTES:GetFileAttributesW(marker.c_str());
    requested=attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
    return requested;
}
