#pragma once
#include "BuildProfile.h"
#include "S0CaptureIdentity.h"
#include <filesystem>

namespace DlssNr::CaptureFull8 {
inline std::filesystem::path MarkerPath(const std::filesystem::path& root){
 const auto& id=S0CaptureIdentity::Current();if(!id.valid)return {};
 return root/(L"D18_CAPTURE_FULL8_"+std::to_wstring(id.pid)+L"_"+std::to_wstring(id.creation)+L".on");
}
inline bool Selected(const std::filesystem::path& root,bool diagnostics){
 if(!BuildProfile::PixelCapture||!diagnostics)return false;
 const auto path=MarkerPath(root);if(path.empty())return false;
 const auto attributes=GetFileAttributesW(path.c_str());
 return attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
}
}
