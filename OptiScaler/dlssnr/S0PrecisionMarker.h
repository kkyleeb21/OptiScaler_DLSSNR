#pragma once
#include "BuildProfile.h"
#include "S0CaptureIdentity.h"
#include <filesystem>

namespace S0Precision {
inline std::filesystem::path MarkerPath(const std::filesystem::path& root){
 const auto& id=S0CaptureIdentity::Current();if(!id.valid)return {};
 return root/(L"D18_S0_VK_PRECISION_"+std::to_wstring(id.pid)+L"_"+std::to_wstring(id.creation)+L".on");
}
inline bool MarkerRequested(const std::filesystem::path& root,bool diagnostics){
 if(!DlssNr::BuildProfile::Diagnostic||!diagnostics)return false;
 static uint64_t next=0;static bool requested=false;
 const auto now=GetTickCount64();if(now<next)return requested;next=now+250;
 const auto path=MarkerPath(root);const auto a=path.empty()?INVALID_FILE_ATTRIBUTES:GetFileAttributesW(path.c_str());
 requested=a!=INVALID_FILE_ATTRIBUTES&&!(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));return requested;
}
}
