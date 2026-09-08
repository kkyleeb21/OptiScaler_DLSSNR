#pragma once
#include <string>
#include <cwctype>
namespace DlssNr {
inline bool IsSrOrRrRuntime(std::wstring name)
{
    for(auto& c:name) c=static_cast<wchar_t>(std::towlower(c));
    return name==L"nvngx_dlss.dll" || name==L"nvngx_dlssd.dll";
}
inline bool AllowRuntimeCandidate(const std::wstring& filename, bool inStreamline, bool optiFg)
{
    return !inStreamline || optiFg || IsSrOrRrRuntime(filename);
}
}
